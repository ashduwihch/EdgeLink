#include "edgelink/shared_memory_buffer.h"

#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <fcntl.h>
#include <iostream>
#include <string>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

namespace
{

constexpr std::size_t TestDataSize = 1024 * 1024;

/**
 * @brief 生成一份内容可预测的 1 MiB 二进制测试数据。
 * @return 每个字节按照固定规律填充的字节数组。
 */
std::vector<std::uint8_t> makeTestData()
{
    std::vector<std::uint8_t> data(TestDataSize);

    for (std::size_t index = 0; index < data.size(); ++index)
    {
        data[index] = static_cast<std::uint8_t>(index % 251);
    }

    return data;
}

/**
 * @brief 在子进程中重新打开共享内存并校验完整数据。
 * @param name 父进程创建的 POSIX Shared Memory 名称。
 * @param expected 预期读到的二进制内容。
 * @return 名称、容量和内容全部正确时返回 true。
 */
bool readAndVerifyInChild(
    const std::string& name,
    const std::vector<std::uint8_t>& expected)
{
    edgelink::SharedMemoryBuffer reader(name);

    if (!reader.open() ||
        reader.capacity() != expected.size() ||
        reader.size() != expected.size())
    {
        return false;
    }

    std::vector<std::uint8_t> actual;
    return reader.read(actual) && actual == expected;
}

/**
 * @brief 检查指定 POSIX Shared Memory 名称是否仍然存在。
 * @param name 需要检查的共享内存名称。
 * @return 名称能够被 shm_open 打开时返回 true，否则返回 false。
 */
bool sharedMemoryExists(const std::string& name)
{
    int fd = shm_open(name.c_str(), O_RDWR | O_CLOEXEC, 0);

    if (fd < 0)
    {
        return false;
    }

    close(fd);
    return true;
}

}  // namespace

/**
 * @brief 验证父子进程可以通过 mmap 共享并校验 1 MiB 二进制数据。
 * @return 创建、写入、跨进程读取和清理全部成功返回 0，否则返回 1。
 */
int main()
{
    const std::string name = "/edgelink-shm-" + std::to_string(getpid());
    const std::vector<std::uint8_t> expected = makeTestData();
    edgelink::SharedMemoryBuffer writer(name);

    if (!writer.create(expected.size()))
    {
        std::cerr << "[FAIL] Could not create shared memory" << std::endl;
        return 1;
    }

    if (!writer.write(expected.data(), expected.size()))
    {
        std::cerr << "[FAIL] Could not write test data" << std::endl;
        return 1;
    }

    const std::uint8_t extraByte = 0;

    if (writer.write(&extraByte, writer.capacity() + 1))
    {
        std::cerr << "[FAIL] Oversized data was accepted" << std::endl;
        return 1;
    }

    pid_t childPid = fork();

    if (childPid < 0)
    {
        std::cerr << "[FAIL] fork failed" << std::endl;
        return 1;
    }

    if (childPid == 0)
    {
        _exit(readAndVerifyInChild(name, expected) ? 0 : 1);
    }

    int childStatus = 0;

    if (waitpid(childPid, &childStatus, 0) != childPid ||
        !WIFEXITED(childStatus) ||
        WEXITSTATUS(childStatus) != 0)
    {
        std::cerr << "[FAIL] Child process could not verify shared data" << std::endl;
        return 1;
    }

    writer.close();

    if (!writer.remove() || sharedMemoryExists(name))
    {
        std::cerr << "[FAIL] Shared memory name was not removed" << std::endl;
        return 1;
    }

    std::cout << "[PASS] transferred and verified " << expected.size()
              << " bytes through shared memory" << std::endl;
    std::cout << "[PASS] shared memory resources were cleaned up" << std::endl;
    return 0;
}
