#pragma once

#include "edgelink/channel.h"

#include <cstdint>
#include <functional>

namespace edgelink
{

class EventLoop;

/**
 * @brief 使用 eventfd 向 EventLoop 发送可累计的事件通知。
 *
 * 对象应在 fork 之前创建，使子进程继承同一个 eventfd。任一持有该文件
 * 描述符的进程都可以调用 notify，接收进程则通过 EventLoop 等待通知。
 */
class EventFdNotifier
{
public:
    using NotificationCallback = std::function<void(std::uint64_t)>;

    /**
     * @brief 创建非阻塞 eventfd 并注册到指定 EventLoop。
     * @param loop 负责等待 eventfd 可读事件的事件循环。
     */
    explicit EventFdNotifier(EventLoop* loop);

    /**
     * @brief 从 EventLoop 移除 Channel 并关闭 eventfd。
     */
    ~EventFdNotifier();

    /**
     * @brief 禁止复制，避免多个对象重复关闭同一个 eventfd。
     */
    EventFdNotifier(const EventFdNotifier&) = delete;

    /**
     * @brief 禁止复制赋值，保持 eventfd 所有权唯一。
     * @return 此函数已删除，不可调用。
     */
    EventFdNotifier& operator=(const EventFdNotifier&) = delete;

    /**
     * @brief 设置 EventLoop 收到通知后执行的回调。
     * @param callback 接收累计通知次数的回调函数。
     */
    void setCallback(NotificationCallback callback);

    /**
     * @brief 向 eventfd 计数器增加指定数值并唤醒接收端。
     * @param count 本次增加的通知次数，必须大于 0 且不能为 UINT64_MAX。
     * @return 完整写入 8 字节计数值时返回 true，否则返回 false。
     */
    bool notify(std::uint64_t count = 1) const;

    /**
     * @brief 获取底层 eventfd，供后续跨进程传递文件描述符使用。
     * @return 当前对象持有的 eventfd。
     */
    int fd() const;

private:
    /**
     * @brief 读取并清零 eventfd 计数器，再把累计值交给业务回调。
     */
    void handleRead();

    EventLoop* loop_;
    int eventFd_ = -1;
    Channel channel_;
    NotificationCallback callback_;
};

}  // namespace edgelink
