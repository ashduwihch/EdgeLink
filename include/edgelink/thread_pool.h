#pragma once

#include <condition_variable>
#include <functional>
#include <cstddef>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

namespace edgelink
{
    class ThreadPool
    {
        public:
            //创建指定数量的工作线程,构造函数名字必须和类名相同(c++)
            explicit ThreadPool(std::size_t threadCount);
            //析构时安全停止线程池，前面～是析构函数标志
            ~ThreadPool();
            //启动线程池
            void start();
            //提交一个任务
            void submit(std::function<void()> task);
            //停止线程池并等待所有线程退出
            void stop();
        private:
            // 工作线程循环获取并执行任务
            void workerLoop();

            std::size_t threadCount_;
            //
            std::vector<std::thread> workers_;
            std::queue<std::function<void()>> tasks_;

            std::mutex mutex_;
            std::condition_variable condition_;

            bool running_ = false;
            bool stopping_ = false;
    };
};// namespace edgelink
