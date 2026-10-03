#include "progress_display.h"

#include <cmath>
#include <iostream>
#include <sstream>

ProgressDisplay::ProgressDisplay(int64_t total_size) : total_size_(total_size) {}

void ProgressDisplay::add(int64_t bytes)
{
    downloaded_.fetch_add(bytes);
    std::lock_guard<std::mutex> lock(mutex_);
    renderLocked();
}

void ProgressDisplay::finish()
{
    std::lock_guard<std::mutex> lock(mutex_);
    renderLocked();
    std::cout << "\n";
}

std::string ProgressDisplay::formatBytes(int64_t bytes)
{
    const char *units[] = {"B", "KB", "MB", "GB"};
    double value = static_cast<double>(bytes);
    int unit = 0;
    while (value >= 1024.0 && unit < 3)
    {
        value /= 1024.0;
        ++unit;
    }

    std::ostringstream oss;
    if (unit == 0)
    {
        oss << bytes << " " << units[unit];
    }
    else
    {
        oss << std::fixed;
        oss.precision(1);
        oss << value << " " << units[unit];
    }
    return oss.str();
}

void ProgressDisplay::renderLocked()
{
    int64_t done = downloaded_.load();
    if (total_size_ <= 0)
    {
        std::cout << "\r已下载: " << formatBytes(done) << std::flush;
        return;
    }

    double percent = static_cast<double>(done) * 100.0 / static_cast<double>(total_size_);
    if (percent > 100.0)
    {
        percent = 100.0;
    }

    const int bar_width = 30;
    int filled = static_cast<int>(std::round(percent / 100.0 * bar_width));
    if (filled > bar_width)
    {
        filled = bar_width;
    }

    std::ostringstream bar;
    bar << "[";
    for (int i = 0; i < bar_width; ++i)
    {
        bar << (i < filled ? '=' : ' ');
    }
    bar << "]";

    std::cout << "\r进度: " << static_cast<int>(percent) << "% " << bar.str() << " "
              << formatBytes(done) << " / " << formatBytes(total_size_) << std::flush;
}
