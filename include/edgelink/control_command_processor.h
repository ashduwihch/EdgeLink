#pragma once

#include <string>

namespace edgelink
{

/**
 * @brief 解析本地控制请求并生成统一格式的文本响应。
 *
 * 当前支持 PING、STATUS 和 HELP 三条命令。这个类只负责业务规则，
 * 不负责 Socket 收发，因此可以脱离网络单独测试。
 */
class ControlCommandProcessor
{
public:
    /**
     * @brief 解析并执行一条控制命令。
     * @param request Unix Domain Socket 收到的原始请求字符串。
     * @return 以 OK 或 ERROR 开头的响应字符串。
     */
    std::string process(const std::string& request) const;
};

}  // namespace edgelink
