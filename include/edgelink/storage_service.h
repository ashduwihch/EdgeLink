#pragma once

#include "edgelink/message_bus.h"

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

struct sqlite3;

namespace edgelink
{

// StoredMessage：数据库行ID和还原后的统一消息
struct StoredMessage
{
    std::int64_t id = 0;
    Message message;
};

// PendingMqttMessage：MQTT离线期间等待补传的一条消息
struct PendingMqttMessage
{
    std::int64_t id = 0;
    std::string topic;
    std::string payload;
    bool retain = false;
    std::int64_t createdAtMs = 0;
};

// StorageService：将MessageBus消息持久化到SQLite WAL数据库
class StorageService
{
public:
    /**
     * @brief 创建SQLite存储服务，但不会立即打开数据库
     * @param databasePath SQLite数据库文件路径
     */
    explicit StorageService(std::string databasePath);

    /**
     * @brief 取消MessageBus订阅并关闭SQLite连接
     */
    ~StorageService();

    StorageService(const StorageService&) = delete;
    StorageService& operator=(const StorageService&) = delete;

    /**
     * @brief 打开数据库、启用WAL并创建消息表
     * @return 初始化全部成功返回true，否则返回false
     */
    bool open();

    /**
     * @brief 关闭当前SQLite数据库连接
     */
    void close();

    /**
     * @brief 查询数据库当前是否打开
     * @return 数据库连接可用返回true，否则返回false
     */
    bool isOpen() const;

    /**
     * @brief 把一条统一消息写入SQLite事务日志和数据表
     * @param message 需要持久化的MessageBus消息
     * @return 插入成功返回true，否则返回false
     */
    bool storeMessage(const Message& message);

    /**
     * @brief 按写入顺序读取持久化消息
     * @param limit 最大返回数量，0表示读取全部
     * @return 成功还原的消息列表，查询失败时返回空列表
     */
    std::vector<StoredMessage> loadMessages(std::size_t limit = 0) const;

    /**
     * @brief 查询数据库中的消息总数
     * @return 当前消息行数，查询失败时返回0
     */
    std::size_t messageCount() const;

    /**
     * @brief 将MQTT暂时无法发送的消息加入SQLite待发送队列
     * @param topic MQTT目标主题
     * @param payload MQTT消息正文
     * @param retain 是否设置MQTT RETAIN标志
     * @return 成功写入待发送队列返回true，否则返回false
     */
    bool enqueueMqttMessage(
        const std::string& topic,
        const std::string& payload,
        bool retain = false);

    /**
     * @brief 按入队顺序读取尚未发送的MQTT消息
     * @param limit 最大返回数量，0表示读取全部
     * @return 按自增ID升序排列的待发送消息
     */
    std::vector<PendingMqttMessage> loadPendingMqttMessages(
        std::size_t limit = 0) const;

    /**
     * @brief 从待发送队列删除一条已完成消息
     * @param messageId 待发送消息的数据库ID
     * @return 成功删除一行返回true，否则返回false
     */
    bool removePendingMqttMessage(std::int64_t messageId);

    /**
     * @brief 查询MQTT待发送队列长度
     * @return 队列中的消息数量，查询失败时返回0
     */
    std::size_t pendingMqttMessageCount() const;

    /**
     * @brief 查询SQLite当前使用的日志模式
     * @return 正常情况下返回wal，数据库未打开或查询失败返回空字符串
     */
    std::string journalMode() const;

    /**
     * @brief 订阅MessageBus主题并自动持久化收到的消息
     * @param messageBus 需要订阅的进程内消息总线
     * @param sourceTopic 需要持久化的MessageBus主题
     * @return 订阅成功返回true，否则返回false
     */
    bool attachMessageBus(
        MessageBus& messageBus,
        const std::string& sourceTopic = "device.tag.updated");

    /**
     * @brief 取消当前MessageBus持久化订阅
     */
    void detachMessageBus();

private:
    /**
     * @brief 在已持有mutex_时执行一条无结果集SQL
     * @param sql 需要执行的SQL语句
     * @return SQLite执行成功返回true，否则返回false
     */
    bool executeSqlLocked(const char* sql);

    /**
     * @brief 在已持有mutex_时启用WAL并创建数据库表和索引
     * @return 数据库结构初始化成功返回true，否则返回false
     */
    bool initializeDatabaseLocked();

    std::string databasePath_;
    mutable std::mutex mutex_;
    struct sqlite3* database_ = nullptr;

    MessageBus* messageBus_ = nullptr;
    MessageBus::SubscriptionId subscriptionId_ =
        MessageBus::InvalidSubscriptionId;
};

} // namespace edgelink
