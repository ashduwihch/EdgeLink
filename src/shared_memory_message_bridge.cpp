#include "edgelink/shared_memory_message_bridge.h"

#include "edgelink/eventfd_notifier.h"
#include "edgelink/ipc_message_codec.h"
#include "edgelink/logger.h"
#include "edgelink/shared_memory_buffer.h"

#include <vector>

namespace edgelink
{

/**
 * @brief 保存共享内存、通知器和消息总线引用，但暂不注册回调。
 * @param buffer 用来保存IPC二进制消息的共享内存。
 * @param notifier 负责通知数据已经写入的eventfd通知器。
 * @param messageBus 接收解码后Message的进程内消息总线。
 */
SharedMemoryMessageBridge::SharedMemoryMessageBridge(
    SharedMemoryBuffer& buffer,
    EventFdNotifier& notifier,
    MessageBus& messageBus)
    : buffer_(buffer), notifier_(notifier), messageBus_(messageBus)
{
}

/**
 * @brief 清除由当前对象注册的eventfd回调。
 */
SharedMemoryMessageBridge::~SharedMemoryMessageBridge()
{
    stop();
}

/**
 * @brief 把eventfd通知回调绑定到当前桥接对象。
 * @return 共享内存已打开时返回true，否则返回false。
 */
bool SharedMemoryMessageBridge::start()
{
    if (running_)
    {
        return true;
    }

    if (!buffer_.isOpen() || notifier_.fd() < 0)
    {
        return false;
    }

    notifier_.setCallback([this](std::uint64_t count)
    {
        handleNotification(count);
    });
    running_ = true;
    return true;
}

/**
 * @brief 清除通知回调，使后续eventfd事件不再访问桥接对象。
 */
void SharedMemoryMessageBridge::stop()
{
    if (!running_)
    {
        return;
    }

    notifier_.setCallback({});
    running_ = false;
}

/**
 * @brief 将Message编码后写入共享内存，成功后再发送eventfd通知。
 * @param message 需要跨进程发送的统一消息。
 * @param buffer 发送进程已经打开的共享内存。
 * @param notifier 发送进程可写的eventfd通知器。
 * @return 编码、写入和通知全部成功时返回true。
 */
bool SharedMemoryMessageBridge::send(
    const Message& message,
    SharedMemoryBuffer& buffer,
    const EventFdNotifier& notifier)
{
    std::vector<std::uint8_t> encodedMessage;

    if (!IpcMessageCodec::encode(message, encodedMessage))
    {
        return false;
    }

    return buffer.write(encodedMessage.data(), encodedMessage.size()) &&
           notifier.notify();
}

/**
 * @brief 返回成功进入MessageBus的消息数量。
 * @return 成功解码并发布的消息数量。
 */
std::size_t SharedMemoryMessageBridge::forwardedMessageCount() const
{
    return forwardedMessageCount_;
}

/**
 * @brief 返回共享内存读取或消息解码失败次数。
 * @return 失败消息数量。
 */
std::size_t SharedMemoryMessageBridge::failedMessageCount() const
{
    return failedMessageCount_;
}

/**
 * @brief 从共享内存读取最新消息，解码后发布到MessageBus。
 * @param count eventfd本次读取到的累计通知次数。
 */
void SharedMemoryMessageBridge::handleNotification(std::uint64_t count)
{
    // 当前SharedMemoryBuffer是单槽位，只保存最后一次写入的数据。
    // 即使eventfd累计了多次通知，也只能转发当前最新的一条消息。
    if (count == 0)
    {
        return;
    }

    std::vector<std::uint8_t> encodedMessage;
    Message message;

    if (!buffer_.read(encodedMessage) ||
        !IpcMessageCodec::decode(encodedMessage, message))
    {
        ++failedMessageCount_;
        Logger::error("Failed to decode shared-memory IPC message");
        return;
    }

    messageBus_.publish(message);
    ++forwardedMessageCount_;
}

}  // namespace edgelink
