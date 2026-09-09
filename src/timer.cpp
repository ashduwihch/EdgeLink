#include "edgelink/timer.h"
#include "edgelink/event_loop.h"
#include "edgelink/logger.h"

#include <sys/epoll.h>
#include <sys/timerfd.h>
#include <unistd.h>
#include <stdexcept>
#include <cstdint>
#include <utility>

namespace edgelink
{

    // 创建 timerfd 并接入 EventLoop
    Timer::Timer(EventLoop* loop)
        : loop_(loop),  //外面传进来的 EventLoop 地址保存到成员变量
        timerFd_(timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC)),//让linux创建一个定时器fd,使用单调时钟，非阻塞，
        channel_(timerFd_)//创建这个 Timer 自己的 Channel
    {
        if (timerFd_ < 0)
        {
            throw std::runtime_error("Failed to create timerfd");
        }

        channel_.setEvents(EPOLLIN);

        channel_.setReadCallback([this]()
        {
            handleRead();
        });

        if (!loop_->addChannel(&channel_)) //把监听 timerfd 的 Channel 加进 EventLoop
        {
            close(timerFd_);
            timerFd_ = -1;
            throw std::runtime_error("Failed to add timer channel");
        }
    }

    // 释放定时器资源
    Timer::~Timer()
    {
        stop();

        if (timerFd_ >= 0)
        {
            loop_->removeChannel(&channel_);
            close(timerFd_);
        }
    }

    // 启动一次性定时器
    bool Timer::startOnce(std::uint64_t delayMs, TimerCallback callback)
    {
        callback_ = std::move(callback);

        itimerspec timerSpec{};

        timerSpec.it_value.tv_sec = delayMs / 1000;
        timerSpec.it_value.tv_nsec = (delayMs % 1000) * 1000000;

        if (timerfd_settime(timerFd_, 0, &timerSpec, nullptr) < 0)
        {
            Logger::error("Failed to start one-shot timer");
            return false;
        }

        return true;
    }

    // 启动周期定时器
    bool Timer::startPeriodic(std::uint64_t intervalMs, TimerCallback callback)
    {
        callback_ = std::move(callback);

        itimerspec timerSpec{};

        timerSpec.it_value.tv_sec = intervalMs / 1000;
        timerSpec.it_value.tv_nsec = (intervalMs % 1000) * 1000000;

        timerSpec.it_interval = timerSpec.it_value;

        if (timerfd_settime(timerFd_, 0, &timerSpec, nullptr) < 0)
        {
            Logger::error("Failed to start periodic timer");
            return false;
        }

        return true;
    }

    // 停止定时器
    void Timer::stop()
    {
        if (timerFd_ < 0)  //如果本身就没有有效的timer,就不用干啥
        {
            return;
        }

        itimerspec timerSpec{};//创建一个全部为 0 的定时设置

        timerfd_settime(timerFd_, 0, &timerSpec, nullptr);  //it_value = 0 就代表取消/停止定时器
    }

    // 处理定时器到期事件
    void Timer::handleRead()
    {
        std::uint64_t expirations = 0; //这个定时器累计到期了几次

        ssize_t bytesRead = read(timerFd_, &expirations, sizeof(expirations));//从 timerfd 里面读取

        if (bytesRead != sizeof(expirations))
        {
            Logger::error("Failed to read timerfd");
            return;
        }

        if (callback_)
        {
            callback_();
        }
    }

}  // namespace edgelink