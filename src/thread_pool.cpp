#include "edgelink/thread_pool.h"

namespace edgelink
{

    // 初始化线程池线程数量
    ThreadPool::ThreadPool(std::size_t threadCount) : threadCount_(threadCount)
    {
    }

    // 析构时停止线程池
    ThreadPool::~ThreadPool()
    {
        stop();
    }

    // 启动工作线程
    void ThreadPool::start()
    {
        if (running_)
        {
            return;
        }

        running_ = true;
        stopping_ = false;

        // 创建指定数量的工作线程
        for (std::size_t i = 0; i < threadCount_; ++i)
        {
            workers_.emplace_back(&ThreadPool::workerLoop, this);
        }
    }

    // 工作线程循环获取并执行任务
    void ThreadPool::workerLoop()
    {
        while (true)
        {
            std::function<void()> task;

            {
                // 加锁保护任务队列
                std::unique_lock<std::mutex> lock(mutex_);

                // 没有任务且线程池未停止时，等待新任务
                while (!stopping_ && tasks_.empty())
                {
                    condition_.wait(lock);
                }

                // 线程池停止且任务已处理完，退出工作线程
                if (stopping_ && tasks_.empty())
                {
                    return;//结束当前函数，并返回到调用这个函数的地方
                }

                // 取出队头任务
                task = std::move(tasks_.front());
                tasks_.pop();
            }

            // 执行任务
            task();
        }
    }
    // 提交一个新任务
    void ThreadPool::submit(std::function<void()> task)
    {
        {
            // 加锁保护任务队列
            std::lock_guard<std::mutex> lock(mutex_);

            // 线程池未运行或正在停止时不再接收任务
            if (!running_ || stopping_)
            {
                return;
            }

            // 将任务加入队列,队尾
            tasks_.push(std::move(task));
        }

        // 唤醒一个等待中的工作线程
        condition_.notify_one();
    }
    // 停止线程池并等待所有工作线程退出
    void ThreadPool::stop()
    {
        {
            // 修改线程池停止状态
            std::lock_guard<std::mutex> lock(mutex_);

            if (!running_)
            {
                return;
            }

            stopping_ = true;
        }

        // 唤醒所有正在等待的工作线程
        condition_.notify_all();

        // 等待所有工作线程结束
        for (std::thread& worker : workers_)
        {
            if (worker.joinable())
            {
                worker.join();
            }
        }

        // 清空已经结束的线程对象
        workers_.clear();

        // 恢复线程池状态
        {
            std::lock_guard<std::mutex> lock(mutex_);
            running_ = false;
            stopping_ = false;
        }
    }

}  // namespace edgelink