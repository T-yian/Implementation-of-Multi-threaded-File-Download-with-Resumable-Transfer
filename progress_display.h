#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>

// 终端进度条：显示百分比、已下载/总大小
class ProgressDisplay
{
public:
    explicit ProgressDisplay(int64_t total_size);

    // 增加已下载字节数并刷新显示
    void add(int64_t bytes);

    // 下载结束，换行收尾
    void finish();

private:
    int64_t total_size_ = 0;
    std::atomic<int64_t> downloaded_{0};
    std::mutex mutex_;

    static std::string formatBytes(int64_t bytes);
    void renderLocked();
};
