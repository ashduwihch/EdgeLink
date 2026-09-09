#pragma once

#include <atomic>
#include <unordered_map>

namespace edgelink
{
    //称为前向声明，先告诉编译器，有一个类叫 Channel
    class Channel;

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

    private:
        int epollFd_ = -1;
        //为什么要用原子布尔变量？
        //普通 bool：只有同一个线程读写它。原子：这个变量允许多个线程安全地读和写
        std::atomic<bool> running_{false};   

        std::unordered_map<int, Channel*> channels_;
    };

}  // namespace edgelink