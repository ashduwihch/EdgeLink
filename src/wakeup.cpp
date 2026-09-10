#include "edgelink/wakeup.h"
#include "edgelink/event_loop.h"
#include "edgelink/logger.h"

#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <unistd.h>

#include <cstdint>
#include <stdexcept>

namespace edgelink
{

    // 创建 eventfd 并接入 EventLoop
    Wakeup::Wakeup(EventLoop* loop)
        : loop_(loop),
        eventFd_(eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC)),//通过eventfd就能得到一个fd返回值
        channel_(eventFd_)  //让.h里面的channel_管理他
    {
        if (eventFd_ < 0)   //如果还是初始值-1就报错说明创建失败
        {
            throw std::runtime_error("Failed to create eventfd");
        }

        channel_.setEvents(EPOLLIN);//为何是可读事件？因为其他线程往 eventfd 写数据以后eventFd_→ 变成可读→ 产生 EPOLLIN

        channel_.setReadCallback([this]()  //给这个通道设置可读事件回调
        {
            handleRead();  //回调函数
        });

        if (!loop_->addChannel(&channel_))  //如果这个通道未成功进EventLoop了
        {
            close(eventFd_);    //关闭
            eventFd_ = -1;
            throw std::runtime_error("Failed to add wakeup channel");
        }
    }

    // 释放 eventfd 资源，Wakeup 对象销毁时自动执行
    Wakeup::~Wakeup()
    {
        if (eventFd_ >= 0)
        {
            loop_->removeChannel(&channel_);//从EventLoop中移除
            close(eventFd_);
        }
    }

    // 主动唤醒 EventLoop
    void Wakeup::wakeup()
    {
        std::uint64_t value = 1;  //一个64位整数，因为eventfd的接口规定读写是八字节整数

        ssize_t bytesWritten = write(eventFd_, &value, sizeof(value));//往 eventfd 中写入数字 1

        if (bytesWritten != sizeof(value))
        {
            Logger::error("Failed to write eventfd");
        }
    }

    // 读取并清除唤醒事件
    void Wakeup::handleRead()
    {
        std::uint64_t value = 0;//准备一个 64 位整数，用来接收 eventfd 里面的计数。

        ssize_t bytesRead = read(eventFd_, &value, sizeof(value));//从 eventfd 读取计数值，默认模式下，读取成功后 eventfd 内部计数器会重新变成0

        if (bytesRead != sizeof(value))
        {
            Logger::error("Failed to read eventfd");
        }
    }

}  // namespace edgelink