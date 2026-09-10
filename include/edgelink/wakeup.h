#pragma once

#include "edgelink/channel.h"

namespace edgelink
{

class EventLoop;

class Wakeup
{
public:
    // 构造函数声明：创建 eventfd 并接入 EventLoop（创建的时候必须回答）
    explicit Wakeup(EventLoop* loop);

    // 释放 eventfd 资源
    ~Wakeup();

    // 主动唤醒 EventLoop
    void wakeup();

private:
    // 处理 eventfd 可读事件
    void handleRead();

    EventLoop* loop_;
    int eventFd_ = -1;

    Channel channel_;
};

}  // namespace edgelink