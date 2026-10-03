#pragma once

// .meta 断点进度文件管理

#include "common.h"

#include <string>
#include <vector>
#include <mutex>

class ProgressTracker
{
public:
    // 从另一个 tracker 拷贝数据成员（url/size/thread_count/segments）
    // mutex 不可拷贝，所以不能整体赋值，用这个函数逐字段拷贝
    void adopt(const ProgressTracker &other);

    static std::string metaPath(const std::string &save_path);

    static bool removeMeta(const std::string &save_path, std::string &error);

    void initNew(const DownloadConfig &config, const FileInfo &info,
                 const std::vector<Segment> &segs);

    bool load(const std::string &save_path, std::string &error);

    bool save(const std::string &save_path, std::string &error) const;

    bool matches(const DownloadConfig &config, const FileInfo &info) const;

    bool segmentsMatch(const std::vector<Segment> &expected) const;

    bool updateSegment(const std::string &save_path, int index, int64_t downloaded,
                       bool finished, std::mutex &mutex, std::string &error);

    bool allFinished() const;

    int finishedCount() const;

    const Segment &segment(int index) const;
    Segment &segment(int index);
    size_t segmentCount() const { return segments.size(); }

private:
    std::string url;
    int64_t total_size = 0;
    int thread_count = 0;
    std::vector<Segment> segments;
    mutable std::mutex mutex;
};