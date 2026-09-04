#include "edgelink/config.h"
#include "edgelink/logger.h"
#include "edgelink/signal_handler.h"
#include "edgelink/thread_pool.h"

#include <chrono>
#include <string>
#include <thread>

int main()
{
    edgelink::Config config;

    // 加载配置文件
    if (!config.load("../config/edgelink.yaml"))
    {
        return 1;
    }

    // 设置日志等级
    std::string logLevel = config.getString("logging.level", "INFO");

    if (logLevel == "WARN")
    {
        edgelink::Logger::setLevel(edgelink::LogLevel::Warn);
    }
    else if (logLevel == "ERROR")
    {
        edgelink::Logger::setLevel(edgelink::LogLevel::Error);
    }
    else
    {
        edgelink::Logger::setLevel(edgelink::LogLevel::Info);
    }

    // 读取程序配置
    std::string appName = config.getString("app.name", "EdgeLink");
    int workerThreads = config.getInt("app.worker_threads", 4);
    int shutdownTimeout = config.getInt("runtime.shutdown_timeout_ms", 3000);

    // 输出启动信息
    edgelink::Logger::info("App name: " + appName);
    edgelink::Logger::info("Worker threads: " + std::to_string(workerThreads));
    edgelink::Logger::info("Shutdown timeout: " + std::to_string(shutdownTimeout) + " ms");

    // 注册退出信号
    edgelink::registerSignalHandlers();

    // 启动线程池
    edgelink::ThreadPool pool(workerThreads);
    pool.start();

    edgelink::Logger::info("EdgeLink started");

    // 主循环持续运行，直到收到退出请求
    while (!edgelink::shutdownRequested())
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    // 收到退出信号后安全停止
    edgelink::Logger::info("Shutdown requested");

    pool.stop();

    edgelink::Logger::info("EdgeLink stopped");

    return 0;
}