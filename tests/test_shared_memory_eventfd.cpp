#include "edgelink/event_loop.h"
#include "edgelink/eventfd_notifier.h"
#include "edgelink/shared_memory_buffer.h"
#include "edgelink/timer.h"

#include <csignal>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <string>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

namespace
{

constexpr std::size_t TestDataSize = 256 * 1024;
constexpr std::uint64_t ExpectedNotificationCount = 3;

/**
 * @brief 生成用于验证通知发生后读取结果的二进制数据。
 * @return 按固定规律填充的 256 KiB 字节数组。
 */
std::vector<std::uint8_t> makeTestData()
{
    std::vector<std::uint8_t> data(TestDataSize);

    for (std::size_t index = 0; index < data.size(); ++index)
    {
        data[index] = static_cast<std::uint8_t>((index * 7) % 253);
    }

    return data;
}

/**
 * @brief 在子进程中写入共享内存，并连续发送三次 eventfd 通知。
 * @param sharedMemoryName 需要打开的 POSIX Shared Memory 名称。
 * @param data 需要写入共享内存的测试数据。
 * @param notifier fork 前创建并由子进程继承的 eventfd 通知器。
 * @return 数据写入和三次通知全部成功时返回 true。
 */
bool writeAndNotifyInChild(
    const std::string& sharedMemoryName,
    const std::vector<std::uint8_t>& data,
    const edgelink::EventFdNotifier& notifier)
{
    edgelink::SharedMemoryBuffer writer(sharedMemoryName);

    if (!writer.open() || !writer.write(data.data(), data.size()))
    {
        return false;
    }

    for (std::uint64_t index = 0; index < ExpectedNotificationCount; ++index)
    {
        if (!notifier.notify())
        {
            return false;
        }
    }

    return true;
}

}  // namespace

/**
 * @brief 验证子进程写共享内存后可用 eventfd 唤醒父进程 EventLoop。
 * @return 数据、累计通知、进程退出和资源清理全部正确时返回 0。
 */
int main()
{
    const std::string sharedMemoryName =
        "/edgelink-eventfd-" + std::to_string(getpid());
    const std::vector<std::uint8_t> expected = makeTestData();

    edgelink::EventLoop eventLoop;
    edgelink::SharedMemoryBuffer sharedMemory(sharedMemoryName);

    if (!sharedMemory.create(expected.size()))
    {
        std::cerr << "[FAIL] Could not create shared memory" << std::endl;
        return 1;
    }

    edgelink::EventFdNotifier notifier(&eventLoop);

    if (notifier.fd() < 0 || notifier.notify(0))
    {
        std::cerr << "[FAIL] EventFdNotifier validation failed" << std::endl;
        return 1;
    }

    std::uint64_t receivedNotifications = 0;
    std::vector<std::uint8_t> receivedData;
    bool readSucceeded = false;
    bool timedOut = false;

    notifier.setCallback([&](std::uint64_t count)
    {
        receivedNotifications += count;

        if (receivedNotifications >= ExpectedNotificationCount)
        {
            readSucceeded = sharedMemory.read(receivedData);
            eventLoop.stop();
        }
    });

    edgelink::Timer timeoutTimer(&eventLoop);
    timeoutTimer.startOnce(3000, [&]()
    {
        timedOut = true;
        eventLoop.stop();
    });

    pid_t childPid = fork();

    if (childPid < 0)
    {
        std::cerr << "[FAIL] fork failed" << std::endl;
        return 1;
    }

    if (childPid == 0)
    {
        _exit(writeAndNotifyInChild(sharedMemoryName, expected, notifier) ? 0 : 1);
    }

    eventLoop.run();
    timeoutTimer.stop();

    int childStatus = 0;

    if (timedOut)
    {
        kill(childPid, SIGKILL);
    }

    bool childWaited = waitpid(childPid, &childStatus, 0) == childPid;
    bool childSucceeded = childWaited &&
                          WIFEXITED(childStatus) &&
                          WEXITSTATUS(childStatus) == 0;

    sharedMemory.close();
    bool removed = sharedMemory.remove();

    if (timedOut ||
        !childSucceeded ||
        receivedNotifications != ExpectedNotificationCount ||
        !readSucceeded ||
        receivedData != expected ||
        !removed)
    {
        std::cerr << "[FAIL] Shared memory eventfd notification failed" << std::endl;
        return 1;
    }

    std::cout << "[PASS] eventfd delivered " << receivedNotifications
              << " accumulated notifications" << std::endl;
    std::cout << "[PASS] EventLoop read " << receivedData.size()
              << " shared-memory bytes without busy polling" << std::endl;
    return 0;
}
