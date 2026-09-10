#pragma once

#include <atomic>
#include <memory>
#include <unordered_map>

namespace edgelink
{

class Channel;
class Wakeup;

class EventLoop
{
public:
    // 创建 epoll 实例
    EventLoop();

    // 释放 epoll 资源
    ~EventLoop();

    // 将 Channel 注册到 epoll
    bool addChannel(Channel* channel);

    // 从 epoll 中移除 Channel
    bool removeChannel(Channel* channel);

    // 启动事件循环
    void run();

    // 停止事件循环
    void stop();

    // 主动唤醒事件循环
    void wakeup();

private:
    int epollFd_ = -1;

    std::atomic<bool> running_{false};

    std::unordered_map<int, Channel*> channels_;

    std::unique_ptr<Wakeup> wakeup_;
};

}  // namespace edgelink