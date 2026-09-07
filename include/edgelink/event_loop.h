#pragma once

#include <cstdint>  //提供固定长度整数类型,例如std::uint32_t
#include <functional>//提供：std::function<void()>，专门用来保存“一个函数”的变量
#include <unordered_map>//哈希表，用key快速找到对于value，保存 fd 和处理函数的对应关系

namespace edgelink{
    class EventLoop{
        public:
        using EventCallback = std::function<void(std::uint32_t)>;//起别名
        // 创建 epoll 实例
        EventLoop();

        // 释放 epoll 资源
        ~EventLoop();

        // 注册需要监听的 fd
        bool addFd(int fd, std::uint32_t events, EventCallback callback);

        // 移除已经注册的 fd
        bool removeFd(int fd);

        // 启动事件循环
        void run();

        // 停止事件循环
        void stop();

        private:
            int epollFd_ = -1;
            bool running_ = false;
            //建一个表，key 是 int 类型的 fd，value 是这个 fd 对应的处理函数
            std::unordered_map<int, EventCallback> callbacks_;
    };
}