#include "edgelink/message_bus.h"
#include "edgelink/mqtt_service.h"
#include "edgelink/storage_service.h"

#include <arpa/inet.h>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <functional>
#include <iostream>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <utility>
#include <vector>

namespace
{

struct MqttPacket
{
    std::uint8_t header = 0;
    std::vector<std::uint8_t> body;
};

struct ReplayBrokerResult
{
    std::vector<std::string> topics;
    std::vector<std::string> payloads;
    bool receivedDisconnect = false;
    std::string error;
};

/**
 * @brief 检查测试条件，失败时输出对应错误
 * @param condition 需要验证的条件
 * @param message 条件失败时的说明
 * @return 条件成立返回true，否则返回false
 */
bool check(bool condition, const char* message)
{
    if (!condition)
    {
        std::cerr << "[FAIL] " << message << std::endl;
        return false;
    }

    return true;
}

/**
 * @brief 从socket完整读取指定字节数
 * @param socketFd 已连接的socket描述符
 * @param data 接收缓冲区
 * @param size 期望读取的字节数
 * @return 完整读取返回true，否则返回false
 */
bool receiveAll(int socketFd, std::uint8_t* data, std::size_t size)
{
    std::size_t receivedSize = 0;

    while (receivedSize < size)
    {
        ssize_t result = ::recv(
            socketFd,
            data + receivedSize,
            size - receivedSize,
            0);

        if (result > 0)
        {
            receivedSize += static_cast<std::size_t>(result);
            continue;
        }

        if (result < 0 && errno == EINTR)
        {
            continue;
        }

        return false;
    }

    return true;
}

/**
 * @brief 读取一个完整MQTT报文
 * @param socketFd 已连接的socket描述符
 * @param packet 输出解析后的固定头和报文体
 * @return MQTT剩余长度和报文体读取成功返回true
 */
bool receiveMqttPacket(int socketFd, MqttPacket& packet)
{
    if (!receiveAll(socketFd, &packet.header, 1))
    {
        return false;
    }

    std::size_t remainingLength = 0;
    std::size_t multiplier = 1;

    for (int byteIndex = 0; byteIndex < 4; ++byteIndex)
    {
        std::uint8_t encodedByte = 0;

        if (!receiveAll(socketFd, &encodedByte, 1))
        {
            return false;
        }

        remainingLength += (encodedByte & 0x7F) * multiplier;

        if ((encodedByte & 0x80) == 0)
        {
            packet.body.resize(remainingLength);
            return remainingLength == 0 ||
                   receiveAll(socketFd, packet.body.data(), remainingLength);
        }

        multiplier *= 128;
    }

    return false;
}

/**
 * @brief 解析QoS 1 PUBLISH中的Topic、Payload和消息ID
 * @param packet MQTT PUBLISH报文
 * @param topic 输出Topic
 * @param payload 输出Payload
 * @param messageId 输出等待PUBACK的消息ID
 * @return 报文结构正确且QoS为1时返回true
 */
bool parseQosOnePublish(
    const MqttPacket& packet,
    std::string& topic,
    std::string& payload,
    std::uint16_t& messageId)
{
    int qos = (packet.header >> 1) & 0x03;

    if ((packet.header & 0xF0) != 0x30 || qos != 1 ||
        packet.body.size() < 4)
    {
        return false;
    }

    std::size_t topicLength =
        (static_cast<std::size_t>(packet.body[0]) << 8) |
        packet.body[1];
    std::size_t messageIdOffset = 2 + topicLength;

    if (packet.body.size() < messageIdOffset + 2)
    {
        return false;
    }

    topic.assign(
        packet.body.begin() + 2,
        packet.body.begin() + messageIdOffset);
    messageId = static_cast<std::uint16_t>(
        (static_cast<std::uint16_t>(packet.body[messageIdOffset]) << 8) |
        packet.body[messageIdOffset + 1]);
    payload.assign(
        packet.body.begin() + messageIdOffset + 2,
        packet.body.end());
    return messageId != 0;
}

/**
 * @brief 创建只监听本机随机端口的测试Broker
 * @param port 输出内核分配的TCP端口
 * @return 成功返回监听socket，否则返回-1
 */
int createListener(std::uint16_t& port)
{
    int listenerFd = ::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);

    if (listenerFd < 0)
    {
        return -1;
    }

    int reuseAddress = 1;
    ::setsockopt(
        listenerFd,
        SOL_SOCKET,
        SO_REUSEADDR,
        &reuseAddress,
        sizeof(reuseAddress));

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = 0;

    if (::bind(
            listenerFd,
            reinterpret_cast<sockaddr*>(&address),
            sizeof(address)) != 0 ||
        ::listen(listenerFd, 1) != 0)
    {
        ::close(listenerFd);
        return -1;
    }

    socklen_t addressLength = sizeof(address);

    if (::getsockname(
            listenerFd,
            reinterpret_cast<sockaddr*>(&address),
            &addressLength) != 0)
    {
        ::close(listenerFd);
        return -1;
    }

    port = ntohs(address.sin_port);
    return listenerFd;
}

/**
 * @brief 在超时时间内等待指定条件成立
 * @param predicate 需要轮询的条件函数
 * @param timeout 最长等待时间
 * @return 条件在超时前成立返回true
 */
bool waitUntil(
    const std::function<bool()>& predicate,
    std::chrono::milliseconds timeout)
{
    auto deadline = std::chrono::steady_clock::now() + timeout;

    while (std::chrono::steady_clock::now() < deadline)
    {
        if (predicate())
        {
            return true;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    return predicate();
}

/**
 * @brief 创建当前测试独占的SQLite数据库路径
 * @return 成功返回唯一文件路径，否则返回空字符串
 */
std::string createTemporaryDatabasePath()
{
    char pathTemplate[] = "/tmp/edgelink-replay-XXXXXX";
    int fileDescriptor = ::mkstemp(pathTemplate);

    if (fileDescriptor < 0)
    {
        return {};
    }

    ::close(fileDescriptor);
    ::unlink(pathTemplate);
    return pathTemplate;
}

/**
 * @brief 删除测试数据库及SQLite辅助文件
 * @param databasePath 测试数据库路径
 */
void removeTemporaryDatabase(const std::string& databasePath)
{
    ::unlink(databasePath.c_str());
    ::unlink((databasePath + "-wal").c_str());
    ::unlink((databasePath + "-shm").c_str());
}

/**
 * @brief 创建带固定序号的Tag更新消息
 * @param sequence 用作Tag名称、数值和时间戳的序号
 * @return 可发布到MessageBus的统一消息
 */
edgelink::Message makeMessage(int sequence)
{
    edgelink::Message message;
    message.topic = "device.tag.updated";
    message.deviceId = "replay-device";
    message.tagName = "tag-" + std::to_string(sequence);
    message.value = static_cast<std::int32_t>(sequence);
    message.quality = edgelink::Quality::Good;
    message.timestamp = std::chrono::system_clock::time_point(
        std::chrono::milliseconds(3000 + sequence));
    return message;
}

/**
 * @brief 运行支持QoS 1确认的最小Broker并记录四条补传消息
 * @param listenerFd 已创建的监听socket
 * @param firstMessageReceived 第一条补传已到达的同步标志
 * @param allowFirstAck 允许Broker回复第一条PUBACK的同步标志
 * @param result 输出收到的Topic、Payload和断开状态
 */
void runReplayBroker(
    int listenerFd,
    std::atomic<bool>& firstMessageReceived,
    std::atomic<bool>& allowFirstAck,
    ReplayBrokerResult& result)
{
    int clientFd = ::accept4(listenerFd, nullptr, nullptr, SOCK_CLOEXEC);
    ::close(listenerFd);

    if (clientFd < 0)
    {
        result.error = "accept failed";
        return;
    }

    timeval timeout{};
    timeout.tv_sec = 4;
    ::setsockopt(
        clientFd,
        SOL_SOCKET,
        SO_RCVTIMEO,
        &timeout,
        sizeof(timeout));

    MqttPacket connectPacket;

    if (!receiveMqttPacket(clientFd, connectPacket) ||
        connectPacket.header != 0x10)
    {
        result.error = "invalid CONNECT packet";
        ::close(clientFd);
        return;
    }

    const std::uint8_t connack[] = {0x20, 0x02, 0x00, 0x00};

    if (::send(clientFd, connack, sizeof(connack), MSG_NOSIGNAL) !=
        static_cast<ssize_t>(sizeof(connack)))
    {
        result.error = "failed to send CONNACK";
        ::close(clientFd);
        return;
    }

    for (int messageIndex = 0; messageIndex < 4; ++messageIndex)
    {
        MqttPacket publishPacket;
        std::string topic;
        std::string payload;
        std::uint16_t messageId = 0;

        if (!receiveMqttPacket(clientFd, publishPacket) ||
            !parseQosOnePublish(
                publishPacket,
                topic,
                payload,
                messageId))
        {
            result.error = "invalid QoS 1 PUBLISH packet";
            ::close(clientFd);
            return;
        }

        result.topics.push_back(std::move(topic));
        result.payloads.push_back(std::move(payload));

        if (messageIndex == 0)
        {
            firstMessageReceived.store(true);

            if (!waitUntil(
                    [&allowFirstAck]()
                    {
                        return allowFirstAck.load();
                    },
                    std::chrono::milliseconds(2000)))
            {
                result.error = "timed out waiting to release first PUBACK";
                ::close(clientFd);
                return;
            }
        }

        const std::uint8_t puback[] = {
            0x40,
            0x02,
            static_cast<std::uint8_t>((messageId >> 8) & 0xFF),
            static_cast<std::uint8_t>(messageId & 0xFF)};

        if (::send(clientFd, puback, sizeof(puback), MSG_NOSIGNAL) !=
            static_cast<ssize_t>(sizeof(puback)))
        {
            result.error = "failed to send PUBACK";
            ::close(clientFd);
            return;
        }
    }

    MqttPacket disconnectPacket;

    if (receiveMqttPacket(clientFd, disconnectPacket) &&
        disconnectPacket.header == 0xE0)
    {
        result.receivedDisconnect = true;
    }
    else
    {
        result.error = "invalid DISCONNECT packet";
    }

    ::close(clientFd);
}

} // namespace

/**
 * @brief 验证SQLite缓存按照原始顺序补传且收到PUBACK后才删除
 * @return 全部检查通过返回0，否则返回1
 */
int main()
{
    std::string databasePath = createTemporaryDatabasePath();
    std::uint16_t brokerPort = 0;
    int listenerFd = createListener(brokerPort);

    if (!check(!databasePath.empty(), "Failed to create database path") ||
        !check(listenerFd >= 0, "Failed to create replay Broker"))
    {
        if (listenerFd >= 0)
        {
            ::close(listenerFd);
        }

        removeTemporaryDatabase(databasePath);
        return 1;
    }

    bool success = true;
    std::atomic<bool> firstMessageReceived{false};
    std::atomic<bool> allowFirstAck{false};
    ReplayBrokerResult brokerResult;
    std::thread brokerThread(
        runReplayBroker,
        listenerFd,
        std::ref(firstMessageReceived),
        std::ref(allowFirstAck),
        std::ref(brokerResult));

    edgelink::StorageService storage(databasePath);
    edgelink::MessageBus messageBus;
    edgelink::MqttConfig config;
    config.host = "127.0.0.1";
    config.port = brokerPort;
    config.clientId = "edgelink-replay-test";
    config.connectTimeoutMs = 1000;
    config.publishAckTimeoutMs = 2500;
    config.topicPrefix = "factory/replay";
    edgelink::MqttService mqttService(config);

    success = check(storage.open(), "Failed to open replay storage") &&
              success;
    mqttService.setOfflineStorage(&storage);
    success = check(
                  mqttService.attachMessageBus(messageBus),
                  "Failed to attach replay MessageBus") &&
              success;

    // 连接前产生三条离线数据
    messageBus.publish(makeMessage(1));
    messageBus.publish(makeMessage(2));
    messageBus.publish(makeMessage(3));
    success = check(
                  storage.pendingMqttMessageCount() == 3,
                  "Three messages should be buffered before connect") &&
              success;

    success = check(mqttService.connect(), "Failed to connect replay MQTT") &&
              success;
    success = check(
                  waitUntil(
                      [&firstMessageReceived]()
                      {
                          return firstMessageReceived.load();
                      },
                      std::chrono::milliseconds(1500)),
                  "Broker did not receive first replay message") &&
              success;

    // 第一条仍在等待PUBACK，此时新数据也必须排到旧数据后面
    messageBus.publish(makeMessage(4));
    allowFirstAck.store(true);

    success = check(
                  waitUntil(
                      [&storage]()
                      {
                          return storage.pendingMqttMessageCount() == 0;
                      },
                      std::chrono::milliseconds(3000)),
                  "MQTT outbox was not fully replayed") &&
              success;

    mqttService.detachMessageBus();
    mqttService.disconnect();
    brokerThread.join();

    success = check(
                  brokerResult.error.empty(),
                  brokerResult.error.c_str()) &&
              success;
    success = check(
                  brokerResult.receivedDisconnect,
                  "Broker did not receive DISCONNECT") &&
              success;
    success = check(
                  brokerResult.topics.size() == 4,
                  "Broker should receive four replay messages") &&
              success;

    if (brokerResult.topics.size() == 4)
    {
        for (int sequence = 1; sequence <= 4; ++sequence)
        {
            std::string expectedSuffix =
                "/replay-device/tags/tag-" + std::to_string(sequence);
            success = check(
                          brokerResult.topics[sequence - 1] ==
                              "factory/replay" + expectedSuffix,
                          "Replay order is incorrect") &&
                      success;
        }
    }

    removeTemporaryDatabase(databasePath);

    if (!success)
    {
        return 1;
    }

    std::cout << "[PASS] MQTT outbox replayed in order after PUBACK"
              << std::endl;
    return 0;
}
