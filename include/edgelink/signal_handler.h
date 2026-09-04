#pragma once

namespace edgelink{
    //注册程序退出信号,通知Linux：SIGINT / SIGTERM 交与该函数处理
    void registerSignalHandlers();
    //判断是否收到退出请求
    bool shutdownRequested();
}