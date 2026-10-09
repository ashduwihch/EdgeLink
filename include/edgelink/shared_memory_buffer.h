#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace edgelink
{

/**
 * @brief 使用 POSIX Shared Memory 和 mmap 实现的跨进程字节缓冲区。
 *
 * 创建者负责确定容量，其他进程通过相同名称打开共享内存。当前版本只负责
 * 数据共享，读写双方的事件通知将在后续通过 eventfd 完成。
 */
class SharedMemoryBuffer
{
public:
    /**
     * @brief 保存 POSIX Shared Memory 名称，但暂不创建或映射内存。
     * @param name 以斜杠开头且不包含其他斜杠的共享内存名称。
     */
    explicit SharedMemoryBuffer(std::string name);

    /**
     * @brief 解除内存映射、关闭文件描述符，并清理本对象创建的共享内存名称。
     */
    ~SharedMemoryBuffer();

    /**
     * @brief 禁止复制，避免两个对象重复释放同一份 mmap 和文件描述符。
     */
    SharedMemoryBuffer(const SharedMemoryBuffer&) = delete;

    /**
     * @brief 禁止复制赋值，避免共享系统资源的所有权不明确。
     * @return 此函数已删除，不可调用。
     */
    SharedMemoryBuffer& operator=(const SharedMemoryBuffer&) = delete;

    /**
     * @brief 创建并映射一块新的共享内存。
     * @param capacity 能够保存的最大数据字节数。
     * @return 创建和映射成功返回 true，否则返回 false。
     */
    bool create(std::size_t capacity);

    /**
     * @brief 打开并映射已经由另一个进程创建的共享内存。
     * @return 打开、映射和格式校验成功返回 true，否则返回 false。
     */
    bool open();

    /**
     * @brief 将一段二进制数据写入共享内存。
     * @param data 数据起始地址，size 大于 0 时不能为 nullptr。
     * @param size 需要写入的字节数，不能超过容量。
     * @return 写入并同步成功返回 true，否则返回 false。
     */
    bool write(const void* data, std::size_t size);

    /**
     * @brief 从共享内存复制当前保存的完整数据。
     * @param output 用来接收数据副本的字节数组。
     * @return 数据头有效且读取成功返回 true，否则返回 false。
     */
    bool read(std::vector<std::uint8_t>& output) const;

    /**
     * @brief 获取共享内存能够保存的最大数据字节数。
     * @return 已映射时返回容量，否则返回 0。
     */
    std::size_t capacity() const;

    /**
     * @brief 获取共享内存当前保存的有效数据字节数。
     * @return 数据头有效时返回数据长度，否则返回 0。
     */
    std::size_t size() const;

    /**
     * @brief 判断当前对象是否已经成功映射共享内存。
     * @return 已映射返回 true，否则返回 false。
     */
    bool isOpen() const;

    /**
     * @brief 解除 mmap 映射并关闭共享内存文件描述符。
     */
    void close();

    /**
     * @brief 从系统中删除共享内存名称，已有映射可继续使用到 close。
     * @return 删除成功或名称已经不存在时返回 true，否则返回 false。
     */
    bool remove();

private:
    std::string name_;
    int fd_ = -1;
    void* mapping_ = nullptr;
    std::size_t mappingSize_ = 0;
    std::size_t capacity_ = 0;
    bool ownsName_ = false;
};

}  // namespace edgelink
