#include "edgelink/event_loop.h"
#include "edgelink/eventfd_notifier.h"
#include "edgelink/ipc_message_codec.h"
#include "edgelink/message_bus.h"
#include "edgelink/shared_memory_buffer.h"
#include "edgelink/shared_memory_message_bridge.h"
#include "edgelink/timer.h"

#include <chrono>
#include <csignal>
#include <cstdint>
#include <iostream>
#include <string>
#include <sys/wait.h>
#include <unistd.h>
#include <variant>
#include <vector>

namespace
{

/**
 * @brief 检查所有TagValue类型都能经过IPC编码后无损还原。
 * @return 全部类型及损坏数据检查通过时返回true。
 */
bool verifyCodec()
{
    const std::vector<edgelink::TagValue> values = {
        std::monostate{},
        true,
        std::int8_t{-8},
        std::int16_t{-1600},
        std::int32_t{-320000},
        std::int64_t{-64000000},
        std::uint8_t{8},
        std::uint16_t{1600},
        std::uint32_t{320000},
        std::uint64_t{64000000},
        12.5F,
        26.75,
        std::string("motor room temperature")};

    for (const edgelink::TagValue& value : values)
    {
        edgelink::Message original;
        original.topic = "device.tag.updated";
        original.deviceId = "device-01";
        original.tagName = "temperature";
        original.value = value;
        original.quality = edgelink::Quality::Good;
        original.timestamp = edgelink::Message{}.timestamp +
            std::chrono::nanoseconds(123456789);

        std::vector<std::uint8_t> encoded;
        edgelink::Message decoded;

        if (!edgelink::IpcMessageCodec::encode(original, encoded) ||
            !edgelink::IpcMessageCodec::decode(encoded, decoded) ||
            decoded.topic != original.topic ||
            decoded.deviceId != original.deviceId ||
            decoded.tagName != original.tagName ||
            decoded.value != original.value ||
            decoded.quality != original.quality ||
            decoded.timestamp != original.timestamp)
        {
            return false;
        }

        encoded.pop_back();

        if (edgelink::IpcMessageCodec::decode(encoded, decoded))
        {
            return false;
        }
    }

    return true;
}

/**
 * @brief 在子进程中打开共享内存，并发送一条设备Tag更新消息。
 * @param sharedMemoryName 父进程创建的共享内存名称。
 * @param notifier fork前创建并由子进程继承的eventfd通知器。
 * @return 消息写入和通知成功时返回true。
 */
bool sendMessageInChild(
    const std::string& sharedMemoryName,
    const edgelink::EventFdNotifier& notifier)
{
    edgelink::SharedMemoryBuffer writer(sharedMemoryName);

    if (!writer.open())
    {
        return false;
    }

    edgelink::Message message;
    message.topic = "device.tag.updated";
    message.deviceId = "meter-01";
    message.tagName = "temperature";
    message.value = 26.5;
    message.quality = edgelink::Quality::Good;
    message.timestamp = std::chrono::system_clock::now();

    return edgelink::SharedMemoryMessageBridge::send(
        message,
        writer,
        notifier);
}

}  // namespace

/**
 * @brief 验证IPC数据经过共享内存和eventfd后能进入MessageBus订阅者。
 * @return 编解码、跨进程传输、发布订阅和资源清理全部成功返回0。
 */
int main()
{
    if (!verifyCodec())
    {
        std::cerr << "[FAIL] IPC Message codec verification failed" << std::endl;
        return 1;
    }

    const std::string sharedMemoryName =
        "/edgelink-message-bridge-" + std::to_string(getpid());

    edgelink::EventLoop eventLoop;
    edgelink::SharedMemoryBuffer sharedMemory(sharedMemoryName);

    if (!sharedMemory.create(4096))
    {
        std::cerr << "[FAIL] Could not create bridge shared memory" << std::endl;
        return 1;
    }

    edgelink::EventFdNotifier notifier(&eventLoop);
    edgelink::MessageBus messageBus;
    edgelink::SharedMemoryMessageBridge bridge(
        sharedMemory,
        notifier,
        messageBus);

    bool subscriberCalled = false;
    edgelink::Message receivedMessage;

    messageBus.subscribe("device.tag.updated", [&](const edgelink::Message& message)
    {
        subscriberCalled = true;
        receivedMessage = message;
        eventLoop.stop();
    });

    if (!bridge.start())
    {
        std::cerr << "[FAIL] Could not start IPC Message bridge" << std::endl;
        return 1;
    }

    bool timedOut = false;
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
        _exit(sendMessageInChild(sharedMemoryName, notifier) ? 0 : 1);
    }

    eventLoop.run();
    timeoutTimer.stop();

    int childStatus = 0;

    if (timedOut)
    {
        kill(childPid, SIGKILL);
    }

    bool childSucceeded = waitpid(childPid, &childStatus, 0) == childPid &&
                          WIFEXITED(childStatus) &&
                          WEXITSTATUS(childStatus) == 0;

    bridge.stop();
    sharedMemory.close();
    bool removed = sharedMemory.remove();

    bool messageCorrect = subscriberCalled &&
                          receivedMessage.deviceId == "meter-01" &&
                          receivedMessage.tagName == "temperature" &&
                          std::holds_alternative<double>(receivedMessage.value) &&
                          std::get<double>(receivedMessage.value) == 26.5 &&
                          receivedMessage.quality == edgelink::Quality::Good;

    if (timedOut ||
        !childSucceeded ||
        !messageCorrect ||
        bridge.forwardedMessageCount() != 1 ||
        bridge.failedMessageCount() != 0 ||
        !removed)
    {
        std::cerr << "[FAIL] IPC to MessageBus integration failed" << std::endl;
        return 1;
    }

    std::cout << "[PASS] child process sent meter-01/temperature=26.5" << std::endl;
    std::cout << "[PASS] shared memory -> eventfd -> MessageBus -> subscriber" << std::endl;
    return 0;
}
