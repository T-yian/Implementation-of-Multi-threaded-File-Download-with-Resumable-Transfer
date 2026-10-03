#include "progress.h"

#include "file_util.h"

#include <cctype>
#include <cstdio>
#include <fstream>
#include <sstream>

namespace
{

    std::string trim(const std::string &s)
    {
        size_t begin = 0;
        while (begin < s.size() && std::isspace(static_cast<unsigned char>(s[begin])))
        {
            ++begin;
        }
        size_t end = s.size();
        while (end > begin && std::isspace(static_cast<unsigned char>(s[end - 1])))
        {
            --end;
        }
        return s.substr(begin, end - begin);
    }

    bool parseKeyValue(const std::string &line, std::string &key, std::string &value)
    {
        auto pos = line.find('=');
        if (pos == std::string::npos)
        {
            return false;
        }
        key = trim(line.substr(0, pos));
        value = trim(line.substr(pos + 1));
        return !key.empty();
    }

} // namespace

// mutex 不可拷贝，所以不能 tracker = other，只能逐字段拷贝
void ProgressTracker::adopt(const ProgressTracker &other)
{
    std::lock_guard<std::mutex> lock(mutex);
    url = other.url;
    total_size = other.total_size;
    thread_count = other.thread_count;
    segments = other.segments;
}

std::string ProgressTracker::metaPath(const std::string &save_path)
{
    return save_path + ".meta";
}

bool ProgressTracker::removeMeta(const std::string &save_path, std::string &error)
{
    std::string path = metaPath(save_path);
    if (!FileUtil::exists(path))
    {
        return true;
    }
    if (remove(path.c_str()) != 0)
    {
        error = "无法删除 meta 文件: " + path;
        return false;
    }
    return true;
}

void ProgressTracker::initNew(const DownloadConfig &config, const FileInfo &info,
                              const std::vector<Segment> &segs)
{
    url = config.url;
    total_size = info.total_size;
    thread_count = config.thread_count;
    segments = segs;
    for (auto &seg : segments)
    {
        seg.downloaded = 0;
        seg.finished = false;
    }
}

bool ProgressTracker::load(const std::string &save_path, std::string &error)
{
    std::ifstream in(metaPath(save_path));
    if (!in)
    {
        error = "无法打开 meta 文件";
        return false;
    }

    segments.clear();
    Segment current;
    bool in_segment = false;
    std::string line;

    while (std::getline(in, line))
    {
        line = trim(line);
        if (line.empty() || line[0] == '#')
        {
            continue;
        }

        if (line[0] == '[')
        {
            if (in_segment)
            {
                segments.push_back(current);
            }
            current = Segment{};
            in_segment = true;
            continue;
        }

        std::string key, value;
        if (!parseKeyValue(line, key, value))
        {
            continue;
        }

        if (!in_segment)
        {
            if (key == "url")
            {
                url = value;
            }
            else if (key == "total_size")
            {
                total_size = std::stoll(value);
            }
            else if (key == "thread_count")
            {
                thread_count = std::stoi(value);
            }
        }
        else
        {
            if (key == "start")
            {
                current.start = std::stoll(value);
            }
            else if (key == "end")
            {
                current.end = std::stoll(value);
            }
            else if (key == "downloaded")
            {
                current.downloaded = std::stoll(value);
            }
            else if (key == "finished")
            {
                current.finished = (value == "1" || value == "true");
            }
        }
    }

    if (in_segment)
    {
        segments.push_back(current);
    }

    if (segments.empty())
    {
        error = "meta 文件中没有分片信息";
        return false;
    }
    return true;
}

bool ProgressTracker::save(const std::string &save_path, std::string &error) const
{
    // 直接由参数推出 .meta 路径（等价于 meta_path_，但让参数有意义）
    std::ofstream out(metaPath(save_path));
    if (!out)
    {
        error = "无法写入 meta 文件: " + metaPath(save_path);
        return false;
    }

    out << "# MultiThreadDownloader meta v1\n";
    out << "url=" << url << "\n";
    out << "total_size=" << total_size << "\n";
    out << "thread_count=" << thread_count << "\n";
    out << "segment_count=" << segments.size() << "\n";

    for (size_t i = 0; i < segments.size(); ++i)
    {
        const auto &seg = segments[i];
        out << "[" << i << "]\n";
        out << "start=" << seg.start << "\n";
        out << "end=" << seg.end << "\n";
        out << "downloaded=" << seg.downloaded << "\n";
        out << "finished=" << (seg.finished ? 1 : 0) << "\n";
    }
    return true;
}

bool ProgressTracker::matches(const DownloadConfig &config, const FileInfo &info) const
{
    return url == config.url && total_size == info.total_size &&
           thread_count == config.thread_count;
}

bool ProgressTracker::segmentsMatch(const std::vector<Segment> &expected) const
{
    if (segments.size() != expected.size())
    {
        return false;
    }
    for (size_t i = 0; i < segments.size(); ++i)
    {
        if (segments[i].start != expected[i].start || segments[i].end != expected[i].end)
        {
            return false;
        }
    }
    return true;
}

bool ProgressTracker::updateSegment(const std::string &save_path, int index,
                                    int64_t downloaded, bool finished,
                                    std::mutex &mutex, std::string &error)
{
    std::lock_guard<std::mutex> lock(mutex);
    if (index < 0 || index >= static_cast<int>(segments.size()))
    {
        error = "分片索引无效";
        return false;
    }
    segments[static_cast<size_t>(index)].downloaded = downloaded;
    segments[static_cast<size_t>(index)].finished = finished;
    return save(save_path, error);
}

bool ProgressTracker::allFinished() const
{
    std::lock_guard<std::mutex> lock(mutex);
    for (const auto &seg : segments)
    {
        if (!seg.finished)
        {
            return false;
        }
    }
    return true;
}

int ProgressTracker::finishedCount() const
{
    std::lock_guard<std::mutex> lock(mutex);
    int count = 0;
    for (const auto &seg : segments)
    {
        if (seg.finished)
        {
            ++count;
        }
    }
    return count;
}

Segment &ProgressTracker::segment(int index)
{
    return segments[static_cast<size_t>(index)];
}

const Segment &ProgressTracker::segment(int index) const
{
    return segments[static_cast<size_t>(index)];
}