#include "edgelink/channel.h"

#include <sys/epoll.h>
#include <utility>

/*
1.创建一个channel，在创建时就需要填写他管理的fd
2.这个channel要监听什么
3.设置好对于的各种处理函数
4.等待epoll返回事件
5.根据实际事件调用对于Callback
*/

namespace edgelink
{

    // 构造函数，创建并绑定指定 fd，例Channel channel(5)-》将5传给fd,把fd的值用于初始化成员变量fd_
    Channel::Channel(int fd) : fd_(fd)
    {
    }

    // 返回 Channel 管理的 fd
    int Channel::fd() const
    {
        return fd_;
    }

    // 设置需要监听的事件
    void Channel::setEvents(std::uint32_t events)
    {
        events_ = events;
    }

    // 返回需要监听的事件
    std::uint32_t Channel::events() const
    {
        return events_;
    }

    // 保存 epoll 实际返回的事件
    void Channel::setRevents(std::uint32_t revents)
    {
        revents_ = revents;
    }

    // 根据实际发生的事件调用对应回调
    // 根据实际发生的事件调用对应回调
    void Channel::handleEvent()
    {
        std::uint32_t revents = revents_;
        EventCallback readCallback = readCallback_;
        EventCallback writeCallback = writeCallback_;
        EventCallback errorCallback = errorCallback_;

        if (revents & (EPOLLERR | EPOLLHUP))
        {
            if (errorCallback)
            {
                errorCallback();
            }

            return;
        }

        if (revents & (EPOLLIN | EPOLLRDHUP))
        {
            if (readCallback)
            {
                readCallback();
            }

            return;
        }

        if (revents & EPOLLOUT)
        {
            if (writeCallback)
            {
                writeCallback();
            }
        }
    }

    // 设置可读事件回调
    //例如外部channel.setReadCallback(handleRead);此时callback里面装着handleRead，
    void Channel::setReadCallback(EventCallback callback)
    {
        readCallback_ = std::move(callback);//把这个函数对象转移进成员变量，减少不必要复制
    }

    // 设置可写事件回调
    void Channel::setWriteCallback(EventCallback callback)
    {
        writeCallback_ = std::move(callback);
    }

    // 设置错误事件回调
    void Channel::setErrorCallback(EventCallback callback)
    {
        errorCallback_ = std::move(callback);
    }

}  // namespace edgelink