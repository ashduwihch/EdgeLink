#pragma once

#include "edgelink/message_bus.h"

#include <cstdint>
#include <vector>

namespace edgelink
{

/**
 * @brief 在 EdgeLink Message 与本机 IPC 二进制格式之间进行转换。
 */
class IpcMessageCodec
{
public:
    /**
     * @brief 将包含 string 和 variant 的 Message 编码为可跨进程复制的字节。
     * @param message 需要编码的进程内消息。
     * @param output 接收编码结果的字节数组。
     * @return 字段长度和类型均有效时返回 true，否则返回 false。
     */
    static bool encode(const Message& message, std::vector<std::uint8_t>& output);

    /**
     * @brief 校验二进制格式并重新构造当前进程可用的 Message。
     * @param input 从共享内存读取的字节数组。
     * @param message 接收解码结果的消息对象。
     * @return 格式完整且所有字段合法时返回 true，否则返回 false。
     */
    static bool decode(const std::vector<std::uint8_t>& input, Message& message);
};

}  // namespace edgelink
