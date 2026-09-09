#pragma once

#include <cstdint>
#include <functional>

/*
Channel
├── fd_              我负责谁
├── events_          我想监听什么
├── revents_         实际发生什么
├── readCallback_    可读了怎么办
├── writeCallback_   可写了怎么办
└── errorCallback_   出错了怎么办
*/
namespace edgelink
{

    class Channel
    {
        public:
            using EventCallback = std::function<void()>;  //给这个很长的类型起个短名字

            // 构造函数，创建一个绑定指定 fd 的 Channel
            //explicit 是为了防止这种奇怪的隐式转换：Channel channel = 5;而必须写：Channel channel(5);
            explicit Channel(int fd);

            // 获取 Channel 管理的 fd
            int fd() const;   //获取这个 Channel 管理的 fd，const:这个函数不会修改 Channel 对象里的成员变量

            // 设置需要监听的事件
            void setEvents(std::uint32_t events);

            // 获取需要监听的事件
            std::uint32_t events() const; //为什么要std::uint32？把当前保存的 events_ 返回给调用者

            // 设置实际发生的事件
            void setRevents(std::uint32_t revents);

            // 根据发生的事件执行对应回调
            void handleEvent();

            // 设置可读事件回调
            void setReadCallback(EventCallback callback);

            // 设置可写事件回调
            void setWriteCallback(EventCallback callback);

            // 设置错误事件回调
            void setErrorCallback(EventCallback callback);

        private:
            int fd_;    //这个 Channel 负责哪个 fd。，fd_ 这种写法是一个很常见的 C++ 成员变量命名习惯，有_通常表示这是类的成员变量，和局部变量区分
            std::uint32_t events_ = 0;  //要监听哪些事件
            std::uint32_t revents_ = 0;  //epoll实际返回了什么事件

            EventCallback readCallback_;
            EventCallback writeCallback_;
            EventCallback errorCallback_;
    };

}  // namespace edgelink