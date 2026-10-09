#include "edgelink/control_command_processor.h"

#include <iostream>
#include <string>

namespace
{

/**
 * @brief 检查实际响应是否符合预期，并在失败时输出命令信息。
 * @param processor 被测试的控制命令处理器。
 * @param request 输入的原始控制请求。
 * @param expected 预期得到的响应。
 * @return 响应相同返回 true，否则返回 false。
 */
bool checkResponse(
    const edgelink::ControlCommandProcessor& processor,
    const std::string& request,
    const std::string& expected)
{
    const std::string actual = processor.process(request);

    if (actual == expected)
    {
        return true;
    }

    std::cerr << "[FAIL] request='" << request
              << "', expected='" << expected
              << "', actual='" << actual << "'" << std::endl;
    return false;
}

}  // namespace

/**
 * @brief 验证控制命令的正常响应、格式兼容和错误处理。
 * @return 全部检查通过返回 0，否则返回 1。
 */
int main()
{
    edgelink::ControlCommandProcessor processor;

    bool passed =
        checkResponse(processor, "PING", "OK PONG") &&
        checkResponse(processor, " status\n", "OK RUNNING") &&
        checkResponse(processor, "Help", "OK COMMANDS PING STATUS HELP") &&
        checkResponse(processor, "   ", "ERROR EMPTY_COMMAND") &&
        checkResponse(processor, "RESTART", "ERROR UNKNOWN_COMMAND");

    if (!passed)
    {
        return 1;
    }

    std::cout << "[PASS] Control command processor test finished" << std::endl;
    return 0;
}
