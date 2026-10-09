#include "edgelink/shared_memory_buffer.h"

#include "edgelink/logger.h"

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <limits>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <utility>

namespace edgelink
{

namespace
{

constexpr std::uint32_t SharedMemoryMagic = 0x45444C4B;
constexpr std::uint32_t SharedMemoryVersion = 1;

struct SharedMemoryHeader
{
    std::uint32_t magic;
    std::uint32_t version;
    std::uint64_t capacity;
    std::uint64_t dataSize;
};

/**
 * @brief 检查名称是否符合 POSIX Shared Memory 的简单命名规则。
 * @param name 需要检查的共享内存名称。
 * @return 名称以单个斜杠开头且后面不再包含斜杠时返回 true。
 */
bool isValidSharedMemoryName(const std::string& name)
{
    return name.size() > 1 &&
           name.front() == '/' &&
           name.find('/', 1) == std::string::npos;
}

/**
 * @brief 将 mmap 返回的首地址解释为共享内存元数据头。
 * @param mapping mmap 返回的映射首地址。
 * @return 指向共享内存头部的指针。
 */
SharedMemoryHeader* headerFrom(void* mapping)
{
    return static_cast<SharedMemoryHeader*>(mapping);
}

/**
 * @brief 将只读 mmap 首地址解释为共享内存元数据头。
 * @param mapping mmap 返回的只读映射首地址。
 * @return 指向只读共享内存头部的指针。
 */
const SharedMemoryHeader* headerFrom(const void* mapping)
{
    return static_cast<const SharedMemoryHeader*>(mapping);
}

/**
 * @brief 计算共享内存中实际数据区域的起始地址。
 * @param mapping mmap 返回的映射首地址。
 * @return 跳过元数据头后的字节地址。
 */
std::uint8_t* dataFrom(void* mapping)
{
    return static_cast<std::uint8_t*>(mapping) + sizeof(SharedMemoryHeader);
}

/**
 * @brief 计算只读共享内存中实际数据区域的起始地址。
 * @param mapping mmap 返回的只读映射首地址。
 * @return 跳过元数据头后的只读字节地址。
 */
const std::uint8_t* dataFrom(const void* mapping)
{
    return static_cast<const std::uint8_t*>(mapping) + sizeof(SharedMemoryHeader);
}

/**
 * @brief 使用预期容量检查共享内存头的标识、版本和长度边界。
 * @param mapping mmap 返回的映射首地址。
 * @param expectedCapacity 根据实际映射长度计算出的数据区容量。
 * @return 所有元数据字段均合法时返回 true，否则返回 false。
 */
bool hasValidHeader(const void* mapping, std::size_t expectedCapacity)
{
    const SharedMemoryHeader* header = headerFrom(mapping);

    return header->magic == SharedMemoryMagic &&
           header->version == SharedMemoryVersion &&
           header->capacity == expectedCapacity &&
           header->dataSize <= expectedCapacity;
}

}  // namespace

/**
 * @brief 保存共享内存名称，等待后续 create 或 open。
 * @param name 以斜杠开头且不包含其他斜杠的共享内存名称。
 */
SharedMemoryBuffer::SharedMemoryBuffer(std::string name)
    : name_(std::move(name))
{
}

/**
 * @brief 释放映射和文件描述符，并删除本对象创建的共享内存名称。
 */
SharedMemoryBuffer::~SharedMemoryBuffer()
{
    close();

    if (ownsName_)
    {
        remove();
    }
}

/**
 * @brief 使用 shm_open、ftruncate 和 mmap 创建指定容量的共享内存。
 * @param capacity 能够保存的最大数据字节数。
 * @return 创建和映射成功返回 true，否则返回 false。
 */
bool SharedMemoryBuffer::create(std::size_t capacity)
{
    if (isOpen() || !isValidSharedMemoryName(name_) || capacity == 0)
    {
        Logger::error("Invalid shared memory create request: " + name_);
        return false;
    }

    if (capacity > std::numeric_limits<std::size_t>::max() - sizeof(SharedMemoryHeader))
    {
        Logger::error("Shared memory capacity is too large");
        return false;
    }

    mappingSize_ = sizeof(SharedMemoryHeader) + capacity;

    if (static_cast<std::uintmax_t>(mappingSize_) >
        static_cast<std::uintmax_t>(std::numeric_limits<off_t>::max()))
    {
        Logger::error("Shared memory mapping size exceeds ftruncate limit");
        mappingSize_ = 0;
        return false;
    }

    fd_ = shm_open(name_.c_str(), O_CREAT | O_EXCL | O_RDWR | O_CLOEXEC, 0600);

    if (fd_ < 0)
    {
        Logger::error("Failed to create shared memory " + name_ + ": " + std::string(std::strerror(errno)));
        mappingSize_ = 0;
        return false;
    }

    ownsName_ = true;

    if (ftruncate(fd_, static_cast<off_t>(mappingSize_)) < 0)
    {
        Logger::error("Failed to resize shared memory: " + std::string(std::strerror(errno)));
        close();
        remove();
        return false;
    }

    mapping_ = mmap(nullptr, mappingSize_, PROT_READ | PROT_WRITE, MAP_SHARED, fd_, 0);

    if (mapping_ == MAP_FAILED)
    {
        Logger::error("Failed to mmap shared memory: " + std::string(std::strerror(errno)));
        mapping_ = nullptr;
        close();
        remove();
        return false;
    }

    capacity_ = capacity;
    SharedMemoryHeader* header = headerFrom(mapping_);
    header->magic = SharedMemoryMagic;
    header->version = SharedMemoryVersion;
    header->capacity = capacity;
    header->dataSize = 0;

    if (msync(mapping_, sizeof(SharedMemoryHeader), MS_SYNC) < 0)
    {
        Logger::error("Failed to initialize shared memory header: " + std::string(std::strerror(errno)));
        close();
        remove();
        return false;
    }

    return true;
}

/**
 * @brief 打开已有 POSIX Shared Memory，并根据 fstat 结果完成 mmap 和格式校验。
 * @return 打开、映射和格式校验成功返回 true，否则返回 false。
 */
bool SharedMemoryBuffer::open()
{
    if (isOpen() || !isValidSharedMemoryName(name_))
    {
        Logger::error("Invalid shared memory open request: " + name_);
        return false;
    }

    fd_ = shm_open(name_.c_str(), O_RDWR | O_CLOEXEC, 0);

    if (fd_ < 0)
    {
        Logger::error("Failed to open shared memory " + name_ + ": " + std::string(std::strerror(errno)));
        return false;
    }

    struct stat status{};

    if (fstat(fd_, &status) < 0 || status.st_size < static_cast<off_t>(sizeof(SharedMemoryHeader)))
    {
        Logger::error("Shared memory size is invalid: " + name_);
        close();
        return false;
    }

    mappingSize_ = static_cast<std::size_t>(status.st_size);
    mapping_ = mmap(nullptr, mappingSize_, PROT_READ | PROT_WRITE, MAP_SHARED, fd_, 0);

    if (mapping_ == MAP_FAILED)
    {
        Logger::error("Failed to mmap existing shared memory: " + std::string(std::strerror(errno)));
        mapping_ = nullptr;
        close();
        return false;
    }

    const std::size_t mappedCapacity = mappingSize_ - sizeof(SharedMemoryHeader);

    if (!hasValidHeader(mapping_, mappedCapacity))
    {
        Logger::error("Shared memory header is invalid: " + name_);
        close();
        return false;
    }

    capacity_ = mappedCapacity;
    return true;
}

/**
 * @brief 先复制数据和长度，再使用 msync 将修改同步到底层共享映射。
 * @param data 数据起始地址，size 大于 0 时不能为 nullptr。
 * @param size 需要写入的字节数，不能超过容量。
 * @return 写入并同步成功返回 true，否则返回 false。
 */
bool SharedMemoryBuffer::write(const void* data, std::size_t size)
{
    if (!isOpen() || size > capacity_ || (size > 0 && data == nullptr))
    {
        return false;
    }

    if (size > 0)
    {
        std::memcpy(dataFrom(mapping_), data, size);
    }

    headerFrom(mapping_)->dataSize = size;

    if (msync(mapping_, sizeof(SharedMemoryHeader) + size, MS_SYNC) < 0)
    {
        Logger::error("Failed to sync shared memory data: " + std::string(std::strerror(errno)));
        return false;
    }

    return true;
}

/**
 * @brief 校验数据长度后，将共享内存内容复制到进程自己的 vector。
 * @param output 用来接收数据副本的字节数组。
 * @return 数据头有效且读取成功返回 true，否则返回 false。
 */
bool SharedMemoryBuffer::read(std::vector<std::uint8_t>& output) const
{
    if (!isOpen())
    {
        return false;
    }

    if (!hasValidHeader(mapping_, capacity_))
    {
        return false;
    }

    const SharedMemoryHeader* header = headerFrom(mapping_);
    const std::size_t dataSize = static_cast<std::size_t>(header->dataSize);
    output.resize(dataSize);

    if (dataSize > 0)
    {
        std::memcpy(output.data(), dataFrom(mapping_), dataSize);
    }

    return true;
}

/**
 * @brief 返回当前映射的数据容量。
 * @return 已映射时返回容量，否则返回 0。
 */
std::size_t SharedMemoryBuffer::capacity() const
{
    return isOpen() ? capacity_ : 0;
}

/**
 * @brief 返回共享内存头中记录的有效数据长度。
 * @return 数据头有效时返回数据长度，否则返回 0。
 */
std::size_t SharedMemoryBuffer::size() const
{
    if (!isOpen() || !hasValidHeader(mapping_, capacity_))
    {
        return 0;
    }

    const SharedMemoryHeader* header = headerFrom(mapping_);
    return header->dataSize <= capacity_
        ? static_cast<std::size_t>(header->dataSize)
        : 0;
}

/**
 * @brief 判断 mmap 是否已经成功建立。
 * @return 已映射返回 true，否则返回 false。
 */
bool SharedMemoryBuffer::isOpen() const
{
    return mapping_ != nullptr;
}

/**
 * @brief 使用 munmap 和 close 释放当前进程持有的共享内存资源。
 */
void SharedMemoryBuffer::close()
{
    if (mapping_ != nullptr)
    {
        munmap(mapping_, mappingSize_);
        mapping_ = nullptr;
    }

    if (fd_ >= 0)
    {
        ::close(fd_);
        fd_ = -1;
    }

    mappingSize_ = 0;
    capacity_ = 0;
}

/**
 * @brief 使用 shm_unlink 删除共享内存名称，并更新名称所有权状态。
 * @return 删除成功或名称已不存在时返回 true，否则返回 false。
 */
bool SharedMemoryBuffer::remove()
{
    if (shm_unlink(name_.c_str()) == 0 || errno == ENOENT)
    {
        ownsName_ = false;
        return true;
    }

    Logger::error("Failed to unlink shared memory " + name_ + ": " + std::string(std::strerror(errno)));
    return false;
}

}  // namespace edgelink
