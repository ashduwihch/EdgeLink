#include "edgelink/message_bus.h"
#include "edgelink/storage_service.h"

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <unistd.h>
#include <utility>
#include <variant>
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
    char pathTemplate[] = "/tmp/edgelink-storage-XXXXXX";
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
 * @brief 删除测试数据库及SQLite可能生成的WAL和共享内存文件
 * @param databasePath 测试数据库文件路径
 */
void removeTemporaryDatabase(const std::string& databasePath)
{
    ::unlink(databasePath.c_str());
    ::unlink((databasePath + "-wal").c_str());
    ::unlink((databasePath + "-shm").c_str());
}

/**
 * @brief 创建包含固定时间戳的测试消息
 * @param deviceId 设备ID
 * @param tagName Tag名称
 * @param value 统一Tag值
 * @param timestampMs Unix毫秒时间戳
 * @return 可发布到MessageBus的消息
 */
edgelink::Message makeMessage(
    std::string deviceId,
    std::string tagName,
    edgelink::TagValue value,
    std::int64_t timestampMs)
{
    edgelink::Message message;
    message.topic = "device.tag.updated";
    message.deviceId = std::move(deviceId);
    message.tagName = std::move(tagName);
    message.value = std::move(value);
    message.quality = edgelink::Quality::Good;
    message.timestamp = std::chrono::system_clock::time_point(
        std::chrono::milliseconds(timestampMs));
    return message;
}

} // namespace

/**
 * @brief 验证WAL模式、MessageBus落盘、类型保持和数据库重开恢复
 * @return 全部检查通过返回0，否则返回1
 */
int main()
{
    std::string databasePath = createTemporaryDatabasePath();

    if (!check(!databasePath.empty(), "Failed to create temporary path"))
    {
        return 1;
    }

    edgelink::MessageBus messageBus;
    bool success = true;

    {
        edgelink::StorageService storage(databasePath);

        success = check(storage.open(), "Failed to open SQLite storage") &&
                  success;
        success = check(
                      storage.journalMode() == "wal",
                      "SQLite journal mode should be WAL") &&
                  success;
        success = check(
                      storage.attachMessageBus(messageBus),
                      "Failed to attach StorageService to MessageBus") &&
                  success;

        edgelink::Message temperature = makeMessage(
            "sensor-01", "temperature", 26.375, 1001);
        edgelink::Message status = makeMessage(
            "pump-02", "status", std::string("running\nstable"), 1002);
        edgelink::Message counter = makeMessage(
            "meter-03",
            "counter",
            static_cast<std::uint64_t>(9000000000001ULL),
            1003);

        success = check(
                      messageBus.publish(temperature) == 1,
                      "Temperature message was not persisted") &&
                  success;
        success = check(
                      messageBus.publish(status) == 1,
                      "Status message was not persisted") &&
                  success;
        success = check(
                      messageBus.publish(counter) == 1,
                      "Counter message was not persisted") &&
                  success;
        success = check(
                      storage.messageCount() == 3,
                      "SQLite should contain three messages") &&
                  success;

        std::vector<edgelink::StoredMessage> limited =
            storage.loadMessages(2);
        success = check(
                      limited.size() == 2,
                      "Limited query should return two messages") &&
                  success;
    }

    // 创建全新的StorageService，模拟程序重启后重新打开同一数据库
    {
        edgelink::StorageService restoredStorage(databasePath);
        success = check(
                      restoredStorage.open(),
                      "Failed to reopen SQLite storage") &&
                  success;
        success = check(
                      restoredStorage.journalMode() == "wal",
                      "Reopened database should remain in WAL mode") &&
                  success;

        std::vector<edgelink::StoredMessage> restored =
            restoredStorage.loadMessages();

        success = check(
                      restored.size() == 3,
                      "Persisted messages were not restored") &&
                  success;

        if (restored.size() == 3)
        {
            success = check(
                          restored[0].id < restored[1].id &&
                              restored[1].id < restored[2].id,
                          "Messages should keep insertion order") &&
                      success;
            success = check(
                          restored[0].message.deviceId == "sensor-01" &&
                              restored[0].message.tagName == "temperature" &&
                              std::holds_alternative<double>(
                                  restored[0].message.value) &&
                              std::get<double>(restored[0].message.value) ==
                                  26.375,
                          "Double TagValue was not restored") &&
                      success;
            success = check(
                          std::holds_alternative<std::string>(
                              restored[1].message.value) &&
                              std::get<std::string>(
                                  restored[1].message.value) ==
                                  "running\nstable",
                          "String TagValue was not restored") &&
                      success;
            success = check(
                          std::holds_alternative<std::uint64_t>(
                              restored[2].message.value) &&
                              std::get<std::uint64_t>(
                                  restored[2].message.value) ==
                                  9000000000001ULL,
                          "uint64 TagValue was not restored") &&
                      success;
            success = check(
                          restored[2].message.quality ==
                                  edgelink::Quality::Good &&
                              std::chrono::duration_cast<
                                  std::chrono::milliseconds>(
                                  restored[2]
                                      .message.timestamp
                                      .time_since_epoch())
                                      .count() == 1003,
                          "Quality or timestamp was not restored") &&
                      success;
        }
    }

    removeTemporaryDatabase(databasePath);

    if (!success)
    {
        return 1;
    }

    std::cout << "[PASS] SQLite WAL data survived storage restart"
              << std::endl;
    return 0;
}
