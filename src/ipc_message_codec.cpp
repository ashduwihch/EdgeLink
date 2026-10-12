#include "edgelink/ipc_message_codec.h"

#include <chrono>
#include <cstring>
#include <limits>
#include <string>
#include <type_traits>
#include <variant>

namespace edgelink
{

namespace
{

constexpr std::uint32_t MessageMagic = 0x454C4D53;
constexpr std::uint16_t MessageVersion = 1;

/**
 * @brief 把一个可直接复制的数值追加到字节数组尾部。
 * @param output 接收二进制数据的字节数组。
 * @param value 需要追加的整数或浮点数。
 */
template <typename T>
void appendPod(std::vector<std::uint8_t>& output, const T& value)
{
    static_assert(std::is_trivially_copyable<T>::value, "T must be trivially copyable");
    const auto* bytes = reinterpret_cast<const std::uint8_t*>(&value);
    output.insert(output.end(), bytes, bytes + sizeof(T));
}

/**
 * @brief 从指定偏移读取一个可直接复制的数值并推进偏移。
 * @param input 完整IPC字节数组。
 * @param offset 当前读取位置，成功后增加sizeof(T)。
 * @param value 接收解码后的数值。
 * @return 剩余字节足够时返回true，否则返回false。
 */
template <typename T>
bool readPod(
    const std::vector<std::uint8_t>& input,
    std::size_t& offset,
    T& value)
{
    static_assert(std::is_trivially_copyable<T>::value, "T must be trivially copyable");

    if (offset > input.size() || input.size() - offset < sizeof(T))
    {
        return false;
    }

    std::memcpy(&value, input.data() + offset, sizeof(T));
    offset += sizeof(T);
    return true;
}

/**
 * @brief 从字节数组读取指定长度的字符串并推进偏移。
 * @param input 完整IPC字节数组。
 * @param offset 当前读取位置。
 * @param length 需要读取的字符串字节数。
 * @param value 接收重新构造的字符串。
 * @return 长度未越界时返回true，否则返回false。
 */
bool readString(
    const std::vector<std::uint8_t>& input,
    std::size_t& offset,
    std::uint32_t length,
    std::string& value)
{
    if (offset > input.size() || input.size() - offset < length)
    {
        return false;
    }

    value.assign(
        reinterpret_cast<const char*>(input.data() + offset),
        static_cast<std::size_t>(length));
    offset += length;
    return true;
}

/**
 * @brief 将TagValue当前保存的具体类型编码为独立字节区。
 * @param value 需要编码的统一Tag值。
 * @param output 接收值数据的字节数组。
 * @return variant类型受支持时返回true。
 */
bool encodeValue(const TagValue& value, std::vector<std::uint8_t>& output)
{
    output.clear();

    switch (value.index())
    {
        case 0:
            return true;
        case 1:
            appendPod(output, static_cast<std::uint8_t>(std::get<bool>(value) ? 1 : 0));
            return true;
        case 2:
            appendPod(output, std::get<std::int8_t>(value));
            return true;
        case 3:
            appendPod(output, std::get<std::int16_t>(value));
            return true;
        case 4:
            appendPod(output, std::get<std::int32_t>(value));
            return true;
        case 5:
            appendPod(output, std::get<std::int64_t>(value));
            return true;
        case 6:
            appendPod(output, std::get<std::uint8_t>(value));
            return true;
        case 7:
            appendPod(output, std::get<std::uint16_t>(value));
            return true;
        case 8:
            appendPod(output, std::get<std::uint32_t>(value));
            return true;
        case 9:
            appendPod(output, std::get<std::uint64_t>(value));
            return true;
        case 10:
            appendPod(output, std::get<float>(value));
            return true;
        case 11:
            appendPod(output, std::get<double>(value));
            return true;
        case 12:
        {
            const std::string& text = std::get<std::string>(value);
            output.assign(text.begin(), text.end());
            return true;
        }
        default:
            return false;
    }
}

/**
 * @brief 从独立值字节区读取一个指定类型的数值。
 * @param input 完整值字节区。
 * @param value 接收解码后的具体数值。
 * @return 字节长度恰好匹配类型大小时返回true。
 */
template <typename T>
bool decodeNumericValue(const std::vector<std::uint8_t>& input, TagValue& value)
{
    if (input.size() != sizeof(T))
    {
        return false;
    }

    T decoded{};
    std::memcpy(&decoded, input.data(), sizeof(T));
    value = decoded;
    return true;
}

/**
 * @brief 根据variant类型编号还原TagValue。
 * @param typeIndex 编码端TagValue::index()得到的类型编号。
 * @param input 只包含值本身的二进制数据。
 * @param value 接收解码结果的统一Tag值。
 * @return 类型编号和字节长度均合法时返回true。
 */
bool decodeValue(
    std::uint16_t typeIndex,
    const std::vector<std::uint8_t>& input,
    TagValue& value)
{
    switch (typeIndex)
    {
        case 0:
            if (!input.empty())
            {
                return false;
            }

            value = std::monostate{};
            return true;
        case 1:
            if (input.size() != 1 || input[0] > 1)
            {
                return false;
            }

            value = input[0] != 0;
            return true;
        case 2: return decodeNumericValue<std::int8_t>(input, value);
        case 3: return decodeNumericValue<std::int16_t>(input, value);
        case 4: return decodeNumericValue<std::int32_t>(input, value);
        case 5: return decodeNumericValue<std::int64_t>(input, value);
        case 6: return decodeNumericValue<std::uint8_t>(input, value);
        case 7: return decodeNumericValue<std::uint16_t>(input, value);
        case 8: return decodeNumericValue<std::uint32_t>(input, value);
        case 9: return decodeNumericValue<std::uint64_t>(input, value);
        case 10: return decodeNumericValue<float>(input, value);
        case 11: return decodeNumericValue<double>(input, value);
        case 12:
            value = std::string(input.begin(), input.end());
            return true;
        default:
            return false;
    }
}

}  // namespace

/**
 * @brief 把Message头部、字符串和TagValue依次编码为无进程指针的字节格式。
 * @param message 需要编码的进程内消息。
 * @param output 接收编码结果的字节数组。
 * @return 字段长度和类型均有效时返回true。
 */
bool IpcMessageCodec::encode(
    const Message& message,
    std::vector<std::uint8_t>& output)
{
    const auto maxLength = static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max());

    if (message.topic.empty() ||
        message.topic.size() > maxLength ||
        message.deviceId.size() > maxLength ||
        message.tagName.size() > maxLength ||
        static_cast<std::uint32_t>(message.quality) > static_cast<std::uint32_t>(Quality::Disconnected))
    {
        return false;
    }

    std::vector<std::uint8_t> encodedValue;

    if (!encodeValue(message.value, encodedValue) || encodedValue.size() > maxLength)
    {
        return false;
    }

    const std::uint16_t typeIndex = static_cast<std::uint16_t>(message.value.index());
    const std::uint32_t quality = static_cast<std::uint32_t>(message.quality);
    const std::int64_t timestampNanoseconds =
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            message.timestamp.time_since_epoch()).count();
    const std::uint32_t topicLength = static_cast<std::uint32_t>(message.topic.size());
    const std::uint32_t deviceIdLength = static_cast<std::uint32_t>(message.deviceId.size());
    const std::uint32_t tagNameLength = static_cast<std::uint32_t>(message.tagName.size());
    const std::uint32_t valueLength = static_cast<std::uint32_t>(encodedValue.size());

    output.clear();
    output.reserve(36 + topicLength + deviceIdLength + tagNameLength + valueLength);
    appendPod(output, MessageMagic);
    appendPod(output, MessageVersion);
    appendPod(output, typeIndex);
    appendPod(output, quality);
    appendPod(output, timestampNanoseconds);
    appendPod(output, topicLength);
    appendPod(output, deviceIdLength);
    appendPod(output, tagNameLength);
    appendPod(output, valueLength);
    output.insert(output.end(), message.topic.begin(), message.topic.end());
    output.insert(output.end(), message.deviceId.begin(), message.deviceId.end());
    output.insert(output.end(), message.tagName.begin(), message.tagName.end());
    output.insert(output.end(), encodedValue.begin(), encodedValue.end());
    return true;
}

/**
 * @brief 校验IPC头部和所有长度后，重新构造Message及其TagValue。
 * @param input 从共享内存读取的字节数组。
 * @param message 接收解码结果的消息对象。
 * @return 格式完整且所有字段合法时返回true。
 */
bool IpcMessageCodec::decode(
    const std::vector<std::uint8_t>& input,
    Message& message)
{
    std::size_t offset = 0;
    std::uint32_t magic = 0;
    std::uint16_t version = 0;
    std::uint16_t typeIndex = 0;
    std::uint32_t quality = 0;
    std::int64_t timestampNanoseconds = 0;
    std::uint32_t topicLength = 0;
    std::uint32_t deviceIdLength = 0;
    std::uint32_t tagNameLength = 0;
    std::uint32_t valueLength = 0;

    if (!readPod(input, offset, magic) ||
        !readPod(input, offset, version) ||
        !readPod(input, offset, typeIndex) ||
        !readPod(input, offset, quality) ||
        !readPod(input, offset, timestampNanoseconds) ||
        !readPod(input, offset, topicLength) ||
        !readPod(input, offset, deviceIdLength) ||
        !readPod(input, offset, tagNameLength) ||
        !readPod(input, offset, valueLength) ||
        magic != MessageMagic ||
        version != MessageVersion ||
        quality > static_cast<std::uint32_t>(Quality::Disconnected) ||
        !readString(input, offset, topicLength, message.topic) ||
        !readString(input, offset, deviceIdLength, message.deviceId) ||
        !readString(input, offset, tagNameLength, message.tagName) ||
        message.topic.empty() ||
        offset > input.size() ||
        input.size() - offset != valueLength)
    {
        return false;
    }

    std::vector<std::uint8_t> encodedValue(
        input.begin() + static_cast<std::ptrdiff_t>(offset),
        input.end());

    if (!decodeValue(typeIndex, encodedValue, message.value))
    {
        return false;
    }

    message.quality = static_cast<Quality>(quality);
    message.timestamp = std::chrono::system_clock::time_point(
        std::chrono::nanoseconds(timestampNanoseconds));
    return true;
}

}  // namespace edgelink
