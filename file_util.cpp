#include "file_util.h"

#include <fstream>
#include <sys/stat.h>
#include <vector>

// 注意：头文件里声明的是 FileUtil 的静态成员函数
// 所以定义必须写 FileUtil:: 前缀，否则和头文件对不上，链接会失败

bool FileUtil::preallocate(const std::string &path, int64_t size, std::string &error)
{
    // 先检查文件是否已存在且大小正确（断点续传时不清空已下载的数据）
    if (exists(path))
    {
        int64_t current_size = fileSize(path);
        if (current_size == size)
        {
            return true; // 文件已存在且大小正确，保留
        }
        if (current_size > size)
        {
            error = "Local file is larger than remote: " + path;
            return false;
        }
        // current_size < size：文件不完整，需要重新分配
    }

    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file)
    {
        error = "Failed to create file: " + path;
        return false;
    }

    // 按 1MB 块写入 \0，撑到目标大小
    const int64_t block_size = 1024 * 1024; // 1MB
    std::vector<char> buffer(block_size, '\0');

    int64_t written = 0;
    while (written < size)
    {
        int64_t to_write = std::min(block_size, size - written);
        file.write(buffer.data(), to_write);
        if (!file)
        {
            error = "Failed to allocate file: " + path;
            return false;
        }
        written += to_write;
    }

    return true;
}

bool FileUtil::exists(const std::string &path)
{
    struct stat buffer;
    return (stat(path.c_str(), &buffer) == 0);
}

int64_t FileUtil::fileSize(const std::string &path)
{
    struct stat buffer;
    if (stat(path.c_str(), &buffer) != 0)
    {
        return -1; // 文件不存在或无法获取大小
    }
    return buffer.st_size;
}

bool FileUtil::writeAt(const std::string &path, int64_t offset, const char *data, size_t size, std::string &error)
{
    std::ofstream file(path, std::ios::binary | std::ios::in | std::ios::out);
    if (!file)
    {
        error = "Failed to open file: " + path;
        return false;
    }
    file.seekp(offset);
    file.write(data, size);
    if (!file)
    {
        error = "Failed to write to file: " + path;
        return false;
    }
    return true;
}
