#include "edgelink/signal_handler.h"

#include <csignal>   //标准库里的信号处理头文件

namespace edgelink
{
    //std::sig_atomic_t是 <csignal> 里提供的一种整数类型，专门适合在信号处理函数里做这种简单标志
    //这个变量可能在程序正常流程之外被突然修改，所以每次使用时都要重新读取它，因为受到SIGINT后他就变1了
    //static表示这个变量只给 signal_handler.cpp 内部使用，不对其他 .cpp 暴露
    //g 表示 global，全局变量的命名习惯
    static volatile std::sig_atomic_t gShutdownRequested = 0;

    // 收到退出信号后设置退出标志
    static void handleSignal(int signal)
    {
        if (signal == SIGINT || signal == SIGTERM)
        {
            gShutdownRequested = 1;
        }
    }

    // 注册 SIGINT 和 SIGTERM 信号
    void registerSignalHandlers()
    {
        std::signal(SIGINT, handleSignal);  //收到什么信号后就调用handleSignal
        std::signal(SIGTERM, handleSignal);
    }

    // 判断程序是否需要退出
    bool shutdownRequested()
    {
        return gShutdownRequested != 0;
    }

}  // namespace edgelink