#pragma once

#include "edgelink/message_bus.h"

#include <cstddef>
#include <cstdint>

namespace edgelink
{

class EventFdNotifier;
class SharedMemoryBuffer;

/**
 * @brief 把共享内存中的IPC消息转发到进程内MessageBus。
 */
class SharedMemoryMessageBridge
{
public:
    /**
     * @brief 保存共享内存、通知器和消息总线的引用。
     * @param buffer 用来保存IPC二进制消息的共享内存。
     * @param notifier 负责通知数据已经写入的eventfd通知器。
     * @param messageBus 接收解码后Message的进程内消息总线。
     */
    SharedMemoryMessageBridge(
        SharedMemoryBuffer& buffer,
        EventFdNotifier& notifier,
        MessageBus& messageBus);

    /**
     * @brief 停止桥接回调，避免对象销毁后继续访问this。
     */
    ~SharedMemoryMessageBridge();

    /**
     * @brief 禁止复制，确保eventfd回调只绑定一个桥接对象。
     */
    SharedMemoryMessageBridge(const SharedMemoryMessageBridge&) = delete;

    /**
     * @brief 禁止复制赋值，保持桥接对象生命周期明确。
     * @return 此函数已删除，不可调用。
     */
    SharedMemoryMessageBridge& operator=(const SharedMemoryMessageBridge&) = delete;

    /**
     * @brief 注册eventfd回调，开始接收共享内存消息。
     * @return 共享内存已打开时返回true，否则返回false。
     */
    bool start();

    /**
     * @brief 清除eventfd回调并停止桥接。
     */
    void stop();

    /**
     * @brief 编码消息、写入共享内存并发送eventfd通知。
     * @param message 需要跨进程发送的统一消息。
     * @param buffer 发送进程已经打开的共享内存。
     * @param notifier 发送进程可写的eventfd通知器。
     * @return 编码、写入和通知全部成功时返回true。
     */
    static bool send(
        const Message& message,
        SharedMemoryBuffer& buffer,
        const EventFdNotifier& notifier);

    /**
     * @brief 获取成功转发到MessageBus的消息数量。
     * @return 成功解码并发布的消息数量。
     */
    std::size_t forwardedMessageCount() const;

    /**
     * @brief 获取读取或解码失败的消息数量。
     * @return 失败消息数量。
     */
    std::size_t failedMessageCount() const;

private:
    /**
     * @brief 响应eventfd通知，读取、解码并发布共享内存中的最新消息。
     * @param count eventfd本次读取到的累计通知次数。
     */
    void handleNotification(std::uint64_t count);

    SharedMemoryBuffer& buffer_;
    EventFdNotifier& notifier_;
    MessageBus& messageBus_;
    bool running_ = false;
    std::size_t forwardedMessageCount_ = 0;
    std::size_t failedMessageCount_ = 0;
};

}  // namespace edgelink
