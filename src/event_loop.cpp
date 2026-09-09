#include "edgelink/event_loop.h"
#include "edgelink/channel.h"
#include "edgelink/logger.h"

#include <cerrno>
#include <stdexcept>
#include <sys/epoll.h>
#include <unistd.h>

namespace edgelink
{

// 创建 epoll 实例
EventLoop::EventLoop()
{
    epollFd_ = epoll_create1(0);

    if (epollFd_ < 0)
    {
        throw std::runtime_error("Failed to create epoll");
    }
}

// 释放 epoll 实例
EventLoop::~EventLoop()
{
    stop();

    if (epollFd_ >= 0)
    {
        close(epollFd_);
    }
}

// 将 Channel 注册到 epoll
bool EventLoop::addChannel(Channel* channel)
{
    int fd = channel->fd();

    epoll_event event{};
    event.events = channel->events();
    event.data.fd = fd;

    if (epoll_ctl(epollFd_, EPOLL_CTL_ADD, fd, &event) < 0)
    {
        Logger::error("Failed to add channel to epoll: " + std::to_string(fd));
        return false;
    }

    channels_[fd] = channel;
    return true;
}

// 从 epoll 中移除 Channel
bool EventLoop::removeChannel(Channel* channel)
{
    int fd = channel->fd();

    if (epoll_ctl(epollFd_, EPOLL_CTL_DEL, fd, nullptr) < 0)
    {
        Logger::error("Failed to remove channel from epoll: " + std::to_string(fd));
        return false;
    }

    channels_.erase(fd);
    return true;
}

// 启动事件循环并分发事件
void EventLoop::run()
{
    constexpr int maxEvents = 32;
    epoll_event events[maxEvents]{};

    running_ = true;

    while (running_)
    {
        int eventCount = epoll_wait(epollFd_, events, maxEvents, 200);

        if (eventCount < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }

            Logger::error("epoll_wait failed");
            break;
        }

        for (int i = 0; i < eventCount; ++i)
        {
            int fd = events[i].data.fd;

            auto channel = channels_.find(fd);

            if (channel != channels_.end())
            {
                channel->second->setRevents(events[i].events);
                channel->second->handleEvent();
            }
        }
    }

    running_ = false;
}

// 停止事件循环
void EventLoop::stop()
{
    running_ = false;
}

}  // namespace edgelink