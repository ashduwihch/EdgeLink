#include "edgelink/control_command_processor.h"

#include <algorithm>
#include <cctype>

namespace edgelink
{

namespace
{

/**
 * @brief 删除字符串首尾的空白字符，保留命令中间的内容。
 * @param text 需要清理的原始字符串。
 * @return 去掉首尾空白后的新字符串。
 */
std::string trim(const std::string& text)
{
    auto first = std::find_if_not(text.begin(), text.end(), [](unsigned char character)
    {
        return std::isspace(character) != 0;
    });

    auto last = std::find_if_not(text.rbegin(), text.rend(), [](unsigned char character)
    {
        return std::isspace(character) != 0;
    }).base();

    if (first >= last)
    {
        return {};
    }

    return std::string(first, last);
}

/**
 * @brief 将命令转换为大写，使 ping、Ping 和 PING 都能被识别。
 * @param text 需要转换的命令字符串。
 * @return 全部英文字母转换为大写后的字符串。
 */
std::string toUpper(std::string text)
{
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char character)
    {
        return static_cast<char>(std::toupper(character));
    });

    return text;
}

}  // namespace

/**
 * @brief 标准化请求并根据命令名称生成对应响应。
 * @param request Unix Domain Socket 收到的原始请求字符串。
 * @return 以 OK 或 ERROR 开头的响应字符串。
 */
std::string ControlCommandProcessor::process(const std::string& request) const
{
    const std::string command = toUpper(trim(request));

    if (command.empty())
    {
        return "ERROR EMPTY_COMMAND";
    }

    if (command == "PING")
    {
        return "OK PONG";
    }

    if (command == "STATUS")
    {
        return "OK RUNNING";
    }

    if (command == "HELP")
    {
        return "OK COMMANDS PING STATUS HELP";
    }

    return "ERROR UNKNOWN_COMMAND";
}

}  // namespace edgelink
