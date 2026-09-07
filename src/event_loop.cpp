#include "edgelink/event_loop.h"
#include "edgelink/logger.h"

#include <cerrno> // errno / EINTR，判断系统调用错误
#include <stdexcept>//runtime_error 异常
#include <sys/epoll.h>//epoll 核心 API
#include <unistd.h>//close() 等 Linux 系统调用

namespace edgelink
{

    // 构造函数，创建 epoll 实例，当EventLoop loop;时自动执行
    EventLoop::EventLoop()
    {
        epollFd_ = epoll_create1(0);//现有函数创建一个 epoll 实例，会返回一个值，-1表示失败

        if (epollFd_ < 0)
        {
            throw std::runtime_error("Failed to create epoll");
        }
    }

    // 释放 epoll 实例，析构函数
    EventLoop::~EventLoop()
    {
        stop();

        if (epollFd_ >= 0)
        {
            close(epollFd_);
        }
    }

    // 注册一个 fd 及其事件处理函数
    bool EventLoop::addFd(int fd, std::uint32_t events, EventCallback callback)
    {
        epoll_event event{}; //事件结构体，描述“监听哪个 fd、监听什么事件”的结构体
        event.events = events;
        event.data.fd = fd;

        if (epoll_ctl(epollFd_, EPOLL_CTL_ADD, fd, &event) < 0)
        {
            Logger::error("Failed to add fd to epoll: " + std::to_string(fd));
            return false;
        }

        callbacks_[fd] = std::move(callback);
        return true;
    }

    // 从 epoll 中移除指定 fd
    bool EventLoop::removeFd(int fd)
    {
        if (epoll_ctl(epollFd_, EPOLL_CTL_DEL, fd, nullptr) < 0)
        {
            Logger::error("Failed to remove fd from epoll: " + std::to_string(fd));
            return false;
        }

        callbacks_.erase(fd);
        return true;
    }

    // 启动事件循环并分发 fd 事件
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
                std::uint32_t eventType = events[i].events;

                auto callback = callbacks_.find(fd);

                if (callback != callbacks_.end())//没找到，find() 会返回callbacks_.end()
                {
                    callback->second(eventType);
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