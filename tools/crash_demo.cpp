#include <csignal>
#include <cstdlib>
#include <iostream>
#include <string>

namespace
{

/**
 * @brief 故意通过空指针写操作触发SIGSEGV
 * @note 仅供独立调试实验使用，绝不能接入生产程序
 */
[[noreturn]] void writeThroughNullPointer()
{
    volatile int* invalidAddress = nullptr;
    *invalidAddress = 42;
    std::abort();
}

/**
 * @brief 构造调用栈第二层，便于GDB backtrace观察函数调用关系
 */
[[noreturn]] void crashWorker()
{
    writeThroughNullPointer();
}

/**
 * @brief 构造调用栈入口层并进入实际故障函数
 */
[[noreturn]] void runSegmentationFaultDemo()
{
    crashWorker();
}

/**
 * @brief 主动发送SIGABRT，便于strace观察信号终止过程
 */
[[noreturn]] void runAbortDemo()
{
    std::raise(SIGABRT);
    std::abort();
}

} // namespace

/**
 * @brief 根据命令行参数运行受控故障演示
 * @param argumentCount 参数数量
 * @param arguments 参数数组，支持--segfault和--abort
 * @return 未指定故障模式时返回0，故障模式由信号终止进程
 */
int main(int argumentCount, char* arguments[])
{
    if (argumentCount == 2)
    {
        std::string mode = arguments[1];

        if (mode == "--segfault")
        {
            runSegmentationFaultDemo();
        }

        if (mode == "--abort")
        {
            runAbortDemo();
        }
    }

    std::cout << "Usage: edgelink_crash_demo "
                 "--segfault|--abort"
              << std::endl;
    return 0;
}
