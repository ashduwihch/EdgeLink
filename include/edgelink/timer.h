#pragma once

#include "edgelink/channel.h"

#include <cstdint> //这里提供std::uint64_t用于u后面表示毫秒时间
#include <functional>

/*
 * Timer 通过 timerFd_ 表示 Linux 定时器，
 * 使用 channel_ 监听 timerFd_ 的事件，
 * 并通过 loop_ 将该 Channel 接入 EventLoop。
 * 定时器到期后，由 Channel 触发对应的定时回调。
 */

namespace edgelink
{

    class EventLoop;  //前向声明，意思是：后面有一个EventLoop类

    class Timer   //定义Timer类
    {
    public:
        using TimerCallback = std::function<void()>;//后续没有参数没有返回值的回调函数就可以作为TimerCallback

        // 构造函数，创建 Timer，并绑定到指定 EventLoop
        //创建Timer时需要告知是属于哪一个Eventloop
        //为何是EventLoop* loop，因为声明的时候必须说明参数类型，
        explicit Timer(EventLoop* loop);

        // 释放 timerfd 资源
        ~Timer();

        // 启动一次性定时器
        //为何用bool开头？因为可以返回是否启动成功，有个返回值
        bool startOnce(std::uint64_t delayMs, TimerCallback callback);

        // 启动周期定时器
        bool startPeriodic(std::uint64_t intervalMs, TimerCallback callback);

        // 停止定时器,不需要返回值用void即可
        void stop();

    private:
        // 处理 timerfd 可读事件
        void handleRead();

        EventLoop* loop_;  //这个Timer挂在哪个EventLoop上
        int timerFd_ = -1;

        Channel channel_;  //专门负责监听 timerFd_ 这个 fd 的 Channel
        TimerCallback callback_;
    };

}  // namespace edgelink