#include "edgelink/eventfd_notifier.h"

#include "edgelink/event_loop.h"
#include "edgelink/logger.h"

#include <cerrno>
#include <exception>
#include <limits>
#include <stdexcept>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <unistd.h>

#include <utility>

namespace edgelink
{

/**
 * @brief 创建 eventfd、配置可读回调并注册到 EventLoop。
 * @param loop 负责等待 eventfd 可读事件的事件循环。
 */
EventFdNotifier::EventFdNotifier(EventLoop* loop)
    : loop_(loop),
      eventFd_(eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC)),
      channel_(eventFd_)
{
    if (loop_ == nullptr)
    {
        if (eventFd_ >= 0)
        {
            close(eventFd_);
            eventFd_ = -1;
        }

        throw std::invalid_argument("EventFdNotifier requires an EventLoop");
    }

    if (eventFd_ < 0)
    {
        throw std::runtime_error("Failed to create eventfd");
    }

    channel_.setEvents(EPOLLIN);
    channel_.setReadCallback([this]()
    {
        handleRead();
    });
    channel_.setErrorCallback([]()
    {
        Logger::error("EventFdNotifier channel error");
    });

    if (!loop_->addChannel(&channel_))
    {
        close(eventFd_);
        eventFd_ = -1;
        throw std::runtime_error("Failed to add eventfd notifier channel");
    }
}

/**
 * @brief 注销 Channel 并关闭 eventfd 系统资源。
 */
EventFdNotifier::~EventFdNotifier()
{
    if (eventFd_ >= 0)
    {
        loop_->removeChannel(&channel_);
        close(eventFd_);
        eventFd_ = -1;
    }
}

/**
 * @brief 保存收到通知时需要执行的业务回调。
 * @param callback 接收累计通知次数的回调函数。
 */
void EventFdNotifier::setCallback(NotificationCallback callback)
{
    callback_ = std::move(callback);
}

/**
 * @brief 将一个 64 位数写入 eventfd，使内核计数器增加并产生可读事件。
 * @param count 本次增加的通知次数，必须大于 0 且不能为 UINT64_MAX。
 * @return 完整写入 8 字节计数值时返回 true，否则返回 false。
 */
bool EventFdNotifier::notify(std::uint64_t count) const
{
    if (eventFd_ < 0 || count == 0 || count == std::numeric_limits<std::uint64_t>::max())
    {
        return false;
    }

    while (true)
    {
        ssize_t bytesWritten = write(eventFd_, &count, sizeof(count));

        if (bytesWritten == static_cast<ssize_t>(sizeof(count)))
        {
            return true;
        }

        if (bytesWritten < 0 && errno == EINTR)
        {
            continue;
        }

        if (bytesWritten < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
        {
            Logger::error("eventfd counter would overflow");
            return false;
        }

        Logger::error("Failed to write eventfd notification");
        return false;
    }
}

/**
 * @brief 返回底层 eventfd 文件描述符。
 * @return 当前对象持有的 eventfd。
 */
int EventFdNotifier::fd() const
{
    return eventFd_;
}

/**
 * @brief 读取 eventfd 累计值，并在不持有内部资源锁的情况下执行回调。
 */
void EventFdNotifier::handleRead()
{
    std::uint64_t count = 0;

    while (true)
    {
        ssize_t bytesRead = read(eventFd_, &count, sizeof(count));

        if (bytesRead == static_cast<ssize_t>(sizeof(count)))
        {
            break;
        }

        if (bytesRead < 0 && errno == EINTR)
        {
            continue;
        }

        if (bytesRead < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
        {
            return;
        }

        Logger::error("Failed to read eventfd notification");
        return;
    }

    if (!callback_)
    {
        return;
    }

    try
    {
        callback_(count);
    }
    catch (const std::exception& exception)
    {
        Logger::error("EventFdNotifier callback failed: " + std::string(exception.what()));
    }
    catch (...)
    {
        Logger::error("EventFdNotifier callback failed with an unknown exception");
    }
}

}  // namespace edgelink
