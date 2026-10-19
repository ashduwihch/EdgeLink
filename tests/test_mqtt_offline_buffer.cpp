#include "edgelink/message_bus.h"
#include "edgelink/mqtt_service.h"
#include "edgelink/storage_service.h"

#include <chrono>
#include <cstdint>
#include <iostream>
#include <string>
#include <unistd.h>
#include <utility>
#include <vector>

namespace
{

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
 * @brief 创建当前测试独占的SQLite临时文件路径
 * @return 成功返回唯一文件路径，失败返回空字符串
 */
std::string createTemporaryDatabasePath()
{
    char pathTemplate[] = "/tmp/edgelink-outbox-XXXXXX";
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
 * @brief 创建用于离线缓存测试的Tag更新消息
 * @param tagName Tag名称
 * @param value Tag数值
 * @param timestampMs Unix毫秒时间戳
 * @return 可发布到MessageBus的统一消息
 */
edgelink::Message makeMessage(
    std::string tagName,
    double value,
    std::int64_t timestampMs)
{
    edgelink::Message message;
    message.topic = "device.tag.updated";
    message.deviceId = "offline-device";
    message.tagName = std::move(tagName);
    message.value = value;
    message.quality = edgelink::Quality::Good;
    message.timestamp = std::chrono::system_clock::time_point(
        std::chrono::milliseconds(timestampMs));
    return message;
}

} // namespace

/**
 * @brief 验证MQTT离线消息按顺序落盘并在存储重开后继续存在
 * @return 全部检查通过返回0，否则返回1
 */
int main()
{
    std::string databasePath = createTemporaryDatabasePath();

    if (!check(!databasePath.empty(), "Failed to create temporary path"))
    {
        return 1;
    }

    bool success = true;

    {
        edgelink::MessageBus messageBus;
        edgelink::StorageService storage(databasePath);
        success = check(storage.open(), "Failed to open offline storage") &&
                  success;

        {
            edgelink::MqttConfig config;
            config.topicPrefix = "factory/devices";
            edgelink::MqttService mqttService(config);
            mqttService.setOfflineStorage(&storage);

            success = check(
                          mqttService.attachMessageBus(messageBus),
                          "Failed to attach MqttService to MessageBus") &&
                      success;

            // 不调用connect，模拟Broker完全不可用
            success = check(
                          messageBus.publish(
                              makeMessage("temperature", 21.5, 2001)) == 1,
                          "First offline message was not handled") &&
                      success;
            success = check(
                          messageBus.publish(
                              makeMessage("pressure", 101.3, 2002)) == 1,
                          "Second offline message was not handled") &&
                      success;
            success = check(
                          messageBus.publish(
                              makeMessage("flow", 7.25, 2003)) == 1,
                          "Third offline message was not handled") &&
                      success;
        }

        success = check(
                      storage.pendingMqttMessageCount() == 3,
                      "Offline queue should contain three messages") &&
                  success;

        std::vector<edgelink::PendingMqttMessage> limited =
            storage.loadPendingMqttMessages(2);
        success = check(
                      limited.size() == 2,
                      "Limited outbox query should return two messages") &&
                  success;
    }

    // 重新打开数据库，验证待发送队列不是只存在于内存
    {
        edgelink::StorageService restoredStorage(databasePath);
        success = check(
                      restoredStorage.open(),
                      "Failed to reopen offline storage") &&
                  success;

        std::vector<edgelink::PendingMqttMessage> pending =
            restoredStorage.loadPendingMqttMessages();
        success = check(
                      pending.size() == 3,
                      "Offline queue did not survive storage restart") &&
                  success;

        if (pending.size() == 3)
        {
            success = check(
                          pending[0].id < pending[1].id &&
                              pending[1].id < pending[2].id,
                          "Offline messages are not in insertion order") &&
                      success;
            success = check(
                          pending[0].topic ==
                              "factory/devices/offline-device/tags/temperature",
                          "First offline MQTT topic is incorrect") &&
                      success;
            success = check(
                          pending[0].payload.find("\"value\":21.5") !=
                              std::string::npos,
                          "First offline MQTT payload is incorrect") &&
                      success;
            success = check(
                          pending[1].topic.find("/pressure") !=
                                  std::string::npos &&
                              pending[2].topic.find("/flow") !=
                                  std::string::npos,
                          "Offline MQTT ordering is incorrect") &&
                      success;
            success = check(
                          restoredStorage.removePendingMqttMessage(
                              pending[0].id) &&
                              restoredStorage.pendingMqttMessageCount() == 2,
                          "Failed to remove completed outbox message") &&
                      success;
        }
    }

    removeTemporaryDatabase(databasePath);

    if (!success)
    {
        return 1;
    }

    std::cout << "[PASS] MQTT offline messages persisted in SQLite outbox"
              << std::endl;
    return 0;
}
