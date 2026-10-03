#pragma once

#include <string>
#include <cstdint>

// 本地文件操作：预分配空间、按偏移写入
class FileUtil
{
public:
    // 创建/截断文件并预分配到指定大小（用 '\0' 填充）
    static bool preallocate(const std::string &path, int64_t size, std::string &error);

    // 将 data 写入文件的 offset 位置（用于分片写入）
    static bool writeAt(const std::string &path, int64_t offset, const char *data,
                        size_t size, std::string &error);

    // 检查文件是否存在
    static bool exists(const std::string &path);

    // 获取文件大小
    static int64_t fileSize(const std::string &path);
};