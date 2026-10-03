#include "download_manager.h"

#include "file_util.h"
#include "http_client.h"
#include "progress.h"
#include "progress_display.h"

#include <algorithm>
#include <atomic>
#include <iostream>
#include <mutex>
#include <thread>

namespace
{

    constexpr int64_t kChunkSize = 256 * 1024;

    struct WorkerArgs
    {
        std::string url;
        std::string save_path;
        int index = 0;

        ProgressTracker *tracker = nullptr;
        std::atomic<bool> *failed = nullptr;
        std::mutex *log_mutex = nullptr;
        std::mutex *error_mutex = nullptr;
        std::mutex *progress_mutex = nullptr;
        std::string *error = nullptr;
        ProgressDisplay *display = nullptr;
    };

    void setError(WorkerArgs &args, const std::string &msg)
    {
        args.failed->store(true);
        std::lock_guard<std::mutex> lock(*args.error_mutex);
        if (args.error->empty())
        {
            *args.error = msg;
        }
    }

    bool downloadSegmentChunks(WorkerArgs &args, Segment &seg)
    {
        int64_t seg_size = seg.end - seg.start + 1;

        if (seg.finished)
        {
            std::lock_guard<std::mutex> lock(*args.log_mutex);
            std::cout << "[线程 " << args.index << "] 分片已完成，跳过\n";
            return true;
        }

        if (seg.downloaded >= seg_size)
        {
            seg.finished = true;
            std::string local_error;
            if (!args.tracker->updateSegment(args.save_path, args.index, seg_size, true,
                                             *args.progress_mutex, local_error))
            {
                setError(args, "[线程 " + std::to_string(args.index) + "] " + local_error);
                return false;
            }
            std::lock_guard<std::mutex> lock(*args.log_mutex);
            std::cout << "[线程 " << args.index << "] 分片已完成，跳过\n";
            return true;
        }

        if (seg.downloaded > 0)
        {
            std::lock_guard<std::mutex> lock(*args.log_mutex);
            std::cout << "[线程 " << args.index << "] 断点续传，从 offset "
                      << (seg.start + seg.downloaded) << " 继续\n";
        }
        else
        {
            std::lock_guard<std::mutex> lock(*args.log_mutex);
            std::cout << "[线程 " << args.index << "] 开始下载 bytes "
                      << seg.start << "-" << seg.end << " (" << seg_size << " bytes)\n";
        }

        int64_t cursor = seg.start + seg.downloaded;
        std::string local_error;

        while (cursor <= seg.end)
        {
            if (args.failed->load())
            {
                return false;
            }

            int64_t chunk_end = std::min(cursor + kChunkSize - 1, seg.end);
            std::vector<char> buffer;

            int64_t got = HttpClient::downloadRange(args.url, cursor, chunk_end, buffer, local_error);
            if (got < 0)
            {
                setError(args, "[线程 " + std::to_string(args.index) + "] " + local_error);
                return false;
            }

            int64_t expected = chunk_end - cursor + 1;
            if (got != expected)
            {
                setError(args, "[线程 " + std::to_string(args.index) + "] 下载字节数不符: 期望 " +
                                   std::to_string(expected) + ", 实际 " + std::to_string(got));
                return false;
            }

            if (!FileUtil::writeAt(args.save_path, cursor, buffer.data(), buffer.size(), local_error))
            {
                setError(args, "[线程 " + std::to_string(args.index) + "] " + local_error);
                return false;
            }

            if (args.display)
            {
                args.display->add(got);
            }

            cursor += got;
            seg.downloaded = cursor - seg.start;
            bool done = (seg.downloaded >= seg_size);
            if (done)
            {
                seg.finished = true;
            }

            if (!args.tracker->updateSegment(args.save_path, args.index, seg.downloaded, seg.finished,
                                             *args.progress_mutex, local_error))
            {
                setError(args, "[线程 " + std::to_string(args.index) + "] " + local_error);
                return false;
            }
        }

        {
            std::lock_guard<std::mutex> lock(*args.log_mutex);
            std::cout << "[线程 " << args.index << "] 完成\n";
        }
        return true;
    }

    void downloadWorker(WorkerArgs args)
    {
        if (args.failed->load())
        {
            return;
        }

        Segment &seg = args.tracker->segment(args.index);
        downloadSegmentChunks(args, seg);
    }

    bool downloadWithProgress(const DownloadConfig &config, ProgressTracker &tracker,
                              ProgressDisplay &display, std::string &error)
    {
        int pending = static_cast<int>(tracker.segmentCount()) - tracker.finishedCount();
        std::cout << "分片进度: " << tracker.finishedCount() << "/" << tracker.segmentCount()
                  << " 已完成，剩余 " << pending << " 个\n";

        for (size_t i = 0; i < tracker.segmentCount(); ++i)
        {
            const auto &seg = tracker.segment(i);
            std::cout << "  分片 [" << i << "]: bytes " << seg.start << "-" << seg.end;
            if (seg.finished)
            {
                std::cout << " [已完成]";
            }
            else if (seg.downloaded > 0)
            {
                std::cout << " [已下载 " << seg.downloaded << " bytes]";
            }
            std::cout << "\n";
        }

        std::atomic<bool> failed{false};
        std::mutex log_mutex;
        std::mutex error_mutex;
        std::mutex progress_mutex;
        std::vector<std::thread> threads;

        int thread_count = std::min(config.thread_count, static_cast<int>(tracker.segmentCount()));
        thread_count = std::max(1, thread_count);
        threads.reserve(static_cast<size_t>(thread_count));

        std::cout << "启动 " << thread_count << " 个下载线程\n";

        for (int i = 0; i < thread_count; ++i)
        {
            WorkerArgs args;
            args.url = config.url;
            args.save_path = config.save_path;
            args.index = i;
            args.tracker = &tracker;
            args.failed = &failed;
            args.log_mutex = &log_mutex;
            args.error_mutex = &error_mutex;
            args.progress_mutex = &progress_mutex;
            args.error = &error;
            args.display = &display;

            threads.emplace_back(downloadWorker, std::move(args));
        }

        for (auto &t : threads)
        {
            t.join();
        }

        if (failed.load())
        {
            return false;
        }

        // 若线程数 < 分片数，顺序处理剩余分片
        for (size_t i = static_cast<size_t>(thread_count); i < tracker.segmentCount(); ++i)
        {
            WorkerArgs args;
            args.url = config.url;
            args.save_path = config.save_path;
            args.index = static_cast<int>(i);
            args.tracker = &tracker;
            args.failed = &failed;
            args.log_mutex = &log_mutex;
            args.error_mutex = &error_mutex;
            args.progress_mutex = &progress_mutex;
            args.error = &error;
            args.display = &display;

            if (!downloadSegmentChunks(args, tracker.segment(i)))
            {
                return false;
            }
        }

        if (!tracker.allFinished())
        {
            error = "下载未全部完成";
            return false;
        }

        std::cout << "全部下载完成\n";
        return true;
    }

    bool prepareProgress(const DownloadConfig &config, const FileInfo &info,
                         ProgressTracker &tracker, std::string &error)
    {
        auto expected = DownloadManager::splitSegments(info.total_size, config.thread_count);
        if (expected.empty())
        {
            error = "分片计算失败";
            return false;
        }

        std::string meta = ProgressTracker::metaPath(config.save_path);
        if (FileUtil::exists(meta))
        {
            ProgressTracker loaded;
            if (!loaded.load(config.save_path, error))
            {
                return false;
            }

            if (loaded.matches(config, info) && loaded.segmentsMatch(expected))
            {
                tracker.adopt(loaded);
                std::cout << "检测到断点记录，继续上次下载\n";
                return true;
            }

            std::cout << "断点记录与当前任务不匹配，重新开始下载\n";
            ProgressTracker::removeMeta(config.save_path, error);
        }

        tracker.initNew(config, info, expected);
        if (!tracker.save(config.save_path, error))
        {
            return false;
        }
        std::cout << "已创建新的下载任务记录: " << meta << "\n";
        return true;
    }

    bool prepareLocalFile(const DownloadConfig &config, const FileInfo &info, std::string &error)
    {
        if (FileUtil::exists(config.save_path))
        {
            int64_t size = FileUtil::fileSize(config.save_path);
            if (size == info.total_size)
            {
                std::cout << "本地文件已存在且大小正确，保留已有数据\n";
                return true;
            }
            std::cout << "本地文件大小不匹配，重新预分配\n";
        }

        std::cout << "预分配文件: " << config.save_path << "\n";
        return FileUtil::preallocate(config.save_path, info.total_size, error);
    }

    bool downloadWithoutRange(const DownloadConfig &config, const FileInfo &info,
                              ProgressDisplay &display, std::string &error)
    {
        std::cout << "使用普通 GET 下载完整文件...\n";

        std::vector<char> buffer;
        int64_t got = HttpClient::downloadFull(config.url, buffer, error);
        if (got < 0)
        {
            return false;
        }

        if (got != info.total_size)
        {
            error = "下载字节数不符: 期望 " + std::to_string(info.total_size) + ", 实际 " +
                    std::to_string(got);
            return false;
        }

        display.add(got);
        display.finish();

        return FileUtil::writeAt(config.save_path, 0, buffer.data(), buffer.size(), error);
    }

} // namespace

// 把总字节数尽量均分成 thread_count 个闭区间分片
// 前面的分片多 1 字节吃掉余数，各片长度最多差 1
std::vector<Segment> DownloadManager::splitSegments(int64_t total_size, int thread_count)
{
    std::vector<Segment> segments;
    if (total_size <= 0 || thread_count <= 0)
    {
        return segments;
    }

    thread_count = std::max(1, thread_count);
    int64_t per = total_size / thread_count;
    int64_t remainder = total_size % thread_count;

    int64_t cursor = 0;
    for (int i = 0; i < thread_count; ++i)
    {
        Segment seg;
        seg.start = cursor;
        int64_t length = per + (i < remainder ? 1 : 0);
        seg.end = cursor + length - 1;
        cursor = seg.end + 1;
        segments.push_back(seg);
    }
    return segments;
}

bool DownloadManager::download(const DownloadConfig &config, std::string &error)
{
    // 1. HEAD 探测：文件大小、是否支持 Range、文件名
    FileInfo info;
    if (!HttpClient::probe(config.url, info, error))
    {
        return false;
    }

    std::cout << "文件名: " << info.filename << "\n";
    std::cout << "大小:   " << info.total_size << " bytes\n";
    std::cout << "Range:  " << (info.supports_range ? "支持" : "不支持") << "\n";

    if (info.total_size <= 0)
    {
        error = "无法获取文件大小，当前版本需要已知大小的文件";
        return false;
    }

    // 2. 预分配本地文件（两种模式都需要）
    if (!prepareLocalFile(config, info, error))
    {
        return false;
    }

    // 3a. 不支持 Range：单线程整文件 GET，无分片、无 .meta
    if (!info.supports_range)
    {
        std::cout << "提示: 服务器不支持 Range，无法断点续传，使用单线程下载\n";
        ProgressDisplay display(info.total_size);
        if (!downloadWithoutRange(config, info, display, error))
        {
            return false;
        }
        std::cout << "下载完成!\n";
        return true;
    }

    // 3b. 支持 Range：加载或新建 .meta 进度
    ProgressTracker tracker;
    if (!prepareProgress(config, info, tracker, error))
    {
        return false;
    }

    // 4. 已经全部完成：删 .meta 直接结束
    if (tracker.allFinished())
    {
        ProgressTracker::removeMeta(config.save_path, error);
        std::cout << "文件已下载完成!\n";
        return true;
    }

    // 5. 续传时把已下载字节先加进进度条，进度从断点处开始显示
    ProgressDisplay display(info.total_size);
    for (size_t i = 0; i < tracker.segmentCount(); ++i)
    {
        const Segment &seg = tracker.segment(i);
        int64_t done = seg.finished ? (seg.end - seg.start + 1) : seg.downloaded;
        if (done > 0)
        {
            display.add(done);
        }
    }

    bool ok = downloadWithProgress(config, tracker, display, error);
    display.finish();
    if (ok)
    {
        // 6. 成功：删 .meta
        ProgressTracker::removeMeta(config.save_path, error);
        std::cout << "下载完成!\n";
    }
    else
    {
        // 6. 失败：保留 .meta，提示用同一条命令续传
        std::cout << "下载中断，进度已保存到 " << ProgressTracker::metaPath(config.save_path)
                  << "\n";
        std::cout << "重新运行相同命令即可继续下载\n";
    }
    return ok;
}