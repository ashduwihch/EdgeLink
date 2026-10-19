#include "edgelink/storage_service.h"

#include "edgelink/logger.h"

#include <chrono>
#include <exception>
#include <filesystem>
#include <iomanip>
#include <limits>
#include <sstream>
#include <sqlite3.h>
#include <type_traits>
#include <utility>

namespace edgelink
{
namespace
{

// SQLite预编译语句的自动释放包装器
class SqliteStatement
{
public:
    /**
     * @brief 接管一条SQLite预编译语句
     * @param statement sqlite3_prepare_v2创建的语句指针
     */
    explicit SqliteStatement(sqlite3_stmt* statement)
        : statement_(statement)
    {
    }

    /**
     * @brief 自动释放SQLite预编译语句
     */
    ~SqliteStatement()
    {
        if (statement_ != nullptr)
        {
            sqlite3_finalize(statement_);
        }
    }

    SqliteStatement(const SqliteStatement&) = delete;
    SqliteStatement& operator=(const SqliteStatement&) = delete;

    /**
     * @brief 获取底层SQLite语句指针
     * @return sqlite3_stmt指针
     */
    sqlite3_stmt* get() const
    {
        return statement_;
    }

private:
    sqlite3_stmt* statement_ = nullptr;
};

struct SerializedValue
{
    int type = 0;
    std::string text;
};

/**
 * @brief 将TagValue保存为类型编号和可逆文本
 * @param value 需要持久化的统一Tag值
 * @return 包含variant下标和数据文本的结构
 */
SerializedValue serializeTagValue(const TagValue& value)
{
    SerializedValue serialized;
    serialized.type = static_cast<int>(value.index());
    serialized.text = std::visit(
        [](const auto& currentValue) -> std::string
        {
            using ValueType = std::decay_t<decltype(currentValue)>;

            if constexpr (std::is_same_v<ValueType, std::monostate>)
            {
                return {};
            }
            else if constexpr (std::is_same_v<ValueType, bool>)
            {
                return currentValue ? "1" : "0";
            }
            else if constexpr (std::is_same_v<ValueType, std::string>)
            {
                return currentValue;
            }
            else if constexpr (
                std::is_same_v<ValueType, std::int8_t> ||
                std::is_same_v<ValueType, std::uint8_t>)
            {
                return std::to_string(static_cast<int>(currentValue));
            }
            else if constexpr (std::is_floating_point_v<ValueType>)
            {
                std::ostringstream output;
                output << std::setprecision(
                              std::numeric_limits<ValueType>::max_digits10)
                       << currentValue;
                return output.str();
            }
            else
            {
                return std::to_string(currentValue);
            }
        },
        value);
    return serialized;
}

/**
 * @brief 根据持久化类型编号还原TagValue
 * @param type TagValue在variant中的类型下标
 * @param text 数据库保存的值文本
 * @param value 输出还原后的TagValue
 * @return 类型和文本有效时返回true，否则返回false
 */
bool deserializeTagValue(
    int type,
    const std::string& text,
    TagValue& value)
{
    try
    {
        switch (type)
        {
            case 0:
                value = std::monostate{};
                return true;

            case 1:
                value = text == "1";
                return text == "0" || text == "1";

            case 2:
                value = static_cast<std::int8_t>(std::stoi(text));
                return true;

            case 3:
                value = static_cast<std::int16_t>(std::stoi(text));
                return true;

            case 4:
                value = static_cast<std::int32_t>(std::stol(text));
                return true;

            case 5:
                value = static_cast<std::int64_t>(std::stoll(text));
                return true;

            case 6:
                value = static_cast<std::uint8_t>(std::stoul(text));
                return true;

            case 7:
                value = static_cast<std::uint16_t>(std::stoul(text));
                return true;

            case 8:
                value = static_cast<std::uint32_t>(std::stoul(text));
                return true;

            case 9:
                value = static_cast<std::uint64_t>(std::stoull(text));
                return true;

            case 10:
                value = std::stof(text);
                return true;

            case 11:
                value = std::stod(text);
                return true;

            case 12:
                value = text;
                return true;

            default:
                return false;
        }
    }
    catch (const std::exception&)
    {
        return false;
    }
}

/**
 * @brief 将系统时钟时间点转换为Unix毫秒时间戳
 * @param timestamp 需要转换的时间点
 * @return 1970年以来的毫秒数
 */
std::int64_t timestampToMilliseconds(
    std::chrono::system_clock::time_point timestamp)
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               timestamp.time_since_epoch())
        .count();
}

/**
 * @brief 把Unix毫秒时间戳还原为系统时钟时间点
 * @param milliseconds 1970年以来的毫秒数
 * @return 对应的系统时钟时间点
 */
std::chrono::system_clock::time_point millisecondsToTimestamp(
    std::int64_t milliseconds)
{
    return std::chrono::system_clock::time_point(
        std::chrono::milliseconds(milliseconds));
}

/**
 * @brief 安全读取SQLite文本列
 * @param statement 当前查询语句
 * @param columnIndex 列序号
 * @return UTF-8文本，SQL NULL返回空字符串
 */
std::string readTextColumn(sqlite3_stmt* statement, int columnIndex)
{
    const unsigned char* text = sqlite3_column_text(statement, columnIndex);
    return text != nullptr
               ? reinterpret_cast<const char*>(text)
               : std::string{};
}

/**
 * @brief 检查数据库中的整数是否是有效Quality枚举
 * @param qualityValue SQLite读取的整数值
 * @return 属于Quality有效范围返回true
 */
bool isValidQuality(int qualityValue)
{
    return qualityValue >= static_cast<int>(Quality::Good) &&
           qualityValue <= static_cast<int>(Quality::Disconnected);
}

} // namespace

/**
 * @brief 保存SQLite文件路径，等待调用open初始化数据库
 * @param databasePath SQLite数据库文件路径
 */
StorageService::StorageService(std::string databasePath)
    : databasePath_(std::move(databasePath))
{
}

/**
 * @brief 先取消MessageBus订阅，再关闭数据库连接
 */
StorageService::~StorageService()
{
    detachMessageBus();
    close();
}

/**
 * @brief 打开SQLite数据库并初始化WAL、表和索引
 * @return 初始化成功返回true，否则返回false
 */
bool StorageService::open()
{
    std::lock_guard<std::mutex> lock(mutex_);

    if (database_ != nullptr)
    {
        return true;
    }

    if (databasePath_.empty())
    {
        Logger::error("SQLite database path is empty");
        return false;
    }

    // 普通运行使用./data，systemd运行使用/var/lib/edgelink；首次启动时自动建目录。
    std::filesystem::path parentDirectory =
        std::filesystem::path(databasePath_).parent_path();

    if (!parentDirectory.empty())
    {
        std::error_code directoryError;
        bool directoryExists = std::filesystem::is_directory(
            parentDirectory,
            directoryError);

        if (!directoryError && !directoryExists)
        {
            std::filesystem::create_directories(
                parentDirectory,
                directoryError);
        }

        if (directoryError ||
            !std::filesystem::is_directory(parentDirectory))
        {
            Logger::error(
                "SQLite parent path is not an accessible directory: " +
                parentDirectory.string() +
                (directoryError
                     ? " (" + directoryError.message() + ")"
                     : ""));
            return false;
        }
    }

    int openResult = sqlite3_open_v2(
        databasePath_.c_str(),
        &database_,
        SQLITE_OPEN_READWRITE |
            SQLITE_OPEN_CREATE |
            SQLITE_OPEN_FULLMUTEX,
        nullptr);

    if (openResult != SQLITE_OK)
    {
        Logger::error(
            "Failed to open SQLite database: " +
            std::string(database_ != nullptr
                            ? sqlite3_errmsg(database_)
                            : "unknown error"));

        if (database_ != nullptr)
        {
            sqlite3_close_v2(database_);
            database_ = nullptr;
        }

        return false;
    }

    sqlite3_busy_timeout(database_, 3000);

    if (!initializeDatabaseLocked())
    {
        sqlite3_close_v2(database_);
        database_ = nullptr;
        return false;
    }

    Logger::info("SQLite storage opened: " + databasePath_);
    return true;
}

/**
 * @brief 关闭SQLite数据库句柄
 */
void StorageService::close()
{
    std::lock_guard<std::mutex> lock(mutex_);

    if (database_ != nullptr)
    {
        sqlite3_close_v2(database_);
        database_ = nullptr;
    }
}

/**
 * @brief 在线程安全条件下查询数据库连接状态
 * @return SQLite句柄存在返回true，否则返回false
 */
bool StorageService::isOpen() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return database_ != nullptr;
}

/**
 * @brief 使用预编译语句保存Message及其TagValue具体类型
 * @param message 需要持久化的统一消息
 * @return SQLite插入成功返回true，否则返回false
 */
bool StorageService::storeMessage(const Message& message)
{
    std::lock_guard<std::mutex> lock(mutex_);

    if (database_ == nullptr)
    {
        return false;
    }

    constexpr const char* InsertSql =
        "INSERT INTO tag_messages("
        "topic, device_id, tag_name, value_type, value_text, quality, timestamp_ms"
        ") VALUES(?, ?, ?, ?, ?, ?, ?);";

    sqlite3_stmt* rawStatement = nullptr;

    if (sqlite3_prepare_v2(
            database_,
            InsertSql,
            -1,
            &rawStatement,
            nullptr) != SQLITE_OK)
    {
        Logger::error("Failed to prepare SQLite insert statement");
        return false;
    }

    SqliteStatement statement(rawStatement);
    SerializedValue serializedValue = serializeTagValue(message.value);

    sqlite3_bind_text(
        statement.get(), 1, message.topic.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(
        statement.get(), 2, message.deviceId.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(
        statement.get(), 3, message.tagName.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(statement.get(), 4, serializedValue.type);
    sqlite3_bind_text(
        statement.get(),
        5,
        serializedValue.text.c_str(),
        -1,
        SQLITE_TRANSIENT);
    sqlite3_bind_int(
        statement.get(), 6, static_cast<int>(message.quality));
    sqlite3_bind_int64(
        statement.get(),
        7,
        timestampToMilliseconds(message.timestamp));

    if (sqlite3_step(statement.get()) != SQLITE_DONE)
    {
        Logger::error(
            "Failed to store SQLite message: " +
            std::string(sqlite3_errmsg(database_)));
        return false;
    }

    return true;
}

/**
 * @brief 查询数据库并还原Message和TagValue具体类型
 * @param limit 最大返回数量，0表示读取全部
 * @return 按数据库ID升序排列的消息列表
 */
std::vector<StoredMessage> StorageService::loadMessages(
    std::size_t limit) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<StoredMessage> messages;

    if (database_ == nullptr)
    {
        return messages;
    }

    const char* querySql = limit == 0
                               ? "SELECT id, topic, device_id, tag_name, "
                                 "value_type, value_text, quality, timestamp_ms "
                                 "FROM tag_messages ORDER BY id ASC;"
                               : "SELECT id, topic, device_id, tag_name, "
                                 "value_type, value_text, quality, timestamp_ms "
                                 "FROM tag_messages ORDER BY id ASC LIMIT ?;";

    sqlite3_stmt* rawStatement = nullptr;

    if (sqlite3_prepare_v2(
            database_,
            querySql,
            -1,
            &rawStatement,
            nullptr) != SQLITE_OK)
    {
        Logger::error("Failed to prepare SQLite message query");
        return messages;
    }

    SqliteStatement statement(rawStatement);

    if (limit > 0)
    {
        sqlite3_bind_int64(
            statement.get(),
            1,
            static_cast<sqlite3_int64>(limit));
    }

    int stepResult = SQLITE_ROW;

    while ((stepResult = sqlite3_step(statement.get())) == SQLITE_ROW)
    {
        int qualityValue = sqlite3_column_int(statement.get(), 6);
        TagValue value;

        if (!isValidQuality(qualityValue) ||
            !deserializeTagValue(
                sqlite3_column_int(statement.get(), 4),
                readTextColumn(statement.get(), 5),
                value))
        {
            Logger::warn("Skipped invalid message row in SQLite database");
            continue;
        }

        StoredMessage storedMessage;
        storedMessage.id = sqlite3_column_int64(statement.get(), 0);
        storedMessage.message.topic = readTextColumn(statement.get(), 1);
        storedMessage.message.deviceId = readTextColumn(statement.get(), 2);
        storedMessage.message.tagName = readTextColumn(statement.get(), 3);
        storedMessage.message.value = std::move(value);
        storedMessage.message.quality = static_cast<Quality>(qualityValue);
        storedMessage.message.timestamp = millisecondsToTimestamp(
            sqlite3_column_int64(statement.get(), 7));
        messages.push_back(std::move(storedMessage));
    }

    if (stepResult != SQLITE_DONE)
    {
        Logger::error(
            "Failed while reading SQLite messages: " +
            std::string(sqlite3_errmsg(database_)));
        messages.clear();
    }

    return messages;
}

/**
 * @brief 使用COUNT查询数据库消息总数
 * @return 查询成功返回消息数量，否则返回0
 */
std::size_t StorageService::messageCount() const
{
    std::lock_guard<std::mutex> lock(mutex_);

    if (database_ == nullptr)
    {
        return 0;
    }

    sqlite3_stmt* rawStatement = nullptr;

    if (sqlite3_prepare_v2(
            database_,
            "SELECT COUNT(*) FROM tag_messages;",
            -1,
            &rawStatement,
            nullptr) != SQLITE_OK)
    {
        return 0;
    }

    SqliteStatement statement(rawStatement);

    if (sqlite3_step(statement.get()) != SQLITE_ROW)
    {
        return 0;
    }

    return static_cast<std::size_t>(
        sqlite3_column_int64(statement.get(), 0));
}

/**
 * @brief 将发送失败的MQTT报文写入持久化待发送队列
 * @param topic MQTT目标主题
 * @param payload MQTT消息正文
 * @param retain 是否设置RETAIN标志
 * @return 插入成功返回true，否则返回false
 */
bool StorageService::enqueueMqttMessage(
    const std::string& topic,
    const std::string& payload,
    bool retain)
{
    std::lock_guard<std::mutex> lock(mutex_);

    if (database_ == nullptr || topic.empty())
    {
        return false;
    }

    constexpr const char* InsertSql =
        "INSERT INTO mqtt_outbox(topic, payload, retain, created_at_ms) "
        "VALUES(?, ?, ?, ?);";
    sqlite3_stmt* rawStatement = nullptr;

    if (sqlite3_prepare_v2(
            database_,
            InsertSql,
            -1,
            &rawStatement,
            nullptr) != SQLITE_OK)
    {
        Logger::error("Failed to prepare MQTT outbox insert");
        return false;
    }

    SqliteStatement statement(rawStatement);
    sqlite3_bind_text(
        statement.get(), 1, topic.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(
        statement.get(), 2, payload.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(statement.get(), 3, retain ? 1 : 0);
    sqlite3_bind_int64(
        statement.get(),
        4,
        timestampToMilliseconds(std::chrono::system_clock::now()));

    if (sqlite3_step(statement.get()) != SQLITE_DONE)
    {
        Logger::error(
            "Failed to enqueue MQTT message: " +
            std::string(sqlite3_errmsg(database_)));
        return false;
    }

    return true;
}

/**
 * @brief 按自增ID读取MQTT待发送队列
 * @param limit 最大返回数量，0表示读取全部
 * @return 按入队顺序排列的待发送消息
 */
std::vector<PendingMqttMessage> StorageService::loadPendingMqttMessages(
    std::size_t limit) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<PendingMqttMessage> messages;

    if (database_ == nullptr)
    {
        return messages;
    }

    const char* querySql = limit == 0
                               ? "SELECT id, topic, payload, retain, created_at_ms "
                                 "FROM mqtt_outbox ORDER BY id ASC;"
                               : "SELECT id, topic, payload, retain, created_at_ms "
                                 "FROM mqtt_outbox ORDER BY id ASC LIMIT ?;";
    sqlite3_stmt* rawStatement = nullptr;

    if (sqlite3_prepare_v2(
            database_,
            querySql,
            -1,
            &rawStatement,
            nullptr) != SQLITE_OK)
    {
        Logger::error("Failed to prepare MQTT outbox query");
        return messages;
    }

    SqliteStatement statement(rawStatement);

    if (limit > 0)
    {
        sqlite3_bind_int64(
            statement.get(),
            1,
            static_cast<sqlite3_int64>(limit));
    }

    int stepResult = SQLITE_ROW;

    while ((stepResult = sqlite3_step(statement.get())) == SQLITE_ROW)
    {
        PendingMqttMessage message;
        message.id = sqlite3_column_int64(statement.get(), 0);
        message.topic = readTextColumn(statement.get(), 1);
        message.payload = readTextColumn(statement.get(), 2);
        message.retain = sqlite3_column_int(statement.get(), 3) != 0;
        message.createdAtMs = sqlite3_column_int64(statement.get(), 4);
        messages.push_back(std::move(message));
    }

    if (stepResult != SQLITE_DONE)
    {
        Logger::error(
            "Failed while reading MQTT outbox: " +
            std::string(sqlite3_errmsg(database_)));
        messages.clear();
    }

    return messages;
}

/**
 * @brief 根据ID删除一条已经完成发送的MQTT消息
 * @param messageId 待发送消息数据库ID
 * @return 恰好删除一行返回true，否则返回false
 */
bool StorageService::removePendingMqttMessage(std::int64_t messageId)
{
    std::lock_guard<std::mutex> lock(mutex_);

    if (database_ == nullptr || messageId <= 0)
    {
        return false;
    }

    sqlite3_stmt* rawStatement = nullptr;

    if (sqlite3_prepare_v2(
            database_,
            "DELETE FROM mqtt_outbox WHERE id = ?;",
            -1,
            &rawStatement,
            nullptr) != SQLITE_OK)
    {
        Logger::error("Failed to prepare MQTT outbox delete");
        return false;
    }

    SqliteStatement statement(rawStatement);
    sqlite3_bind_int64(statement.get(), 1, messageId);

    return sqlite3_step(statement.get()) == SQLITE_DONE &&
           sqlite3_changes(database_) == 1;
}

/**
 * @brief 查询MQTT待发送消息总数
 * @return 查询成功返回队列长度，否则返回0
 */
std::size_t StorageService::pendingMqttMessageCount() const
{
    std::lock_guard<std::mutex> lock(mutex_);

    if (database_ == nullptr)
    {
        return 0;
    }

    sqlite3_stmt* rawStatement = nullptr;

    if (sqlite3_prepare_v2(
            database_,
            "SELECT COUNT(*) FROM mqtt_outbox;",
            -1,
            &rawStatement,
            nullptr) != SQLITE_OK)
    {
        return 0;
    }

    SqliteStatement statement(rawStatement);

    if (sqlite3_step(statement.get()) != SQLITE_ROW)
    {
        return 0;
    }

    return static_cast<std::size_t>(
        sqlite3_column_int64(statement.get(), 0));
}

/**
 * @brief 读取SQLite journal_mode配置
 * @return 查询成功返回日志模式名称，否则返回空字符串
 */
std::string StorageService::journalMode() const
{
    std::lock_guard<std::mutex> lock(mutex_);

    if (database_ == nullptr)
    {
        return {};
    }

    sqlite3_stmt* rawStatement = nullptr;

    if (sqlite3_prepare_v2(
            database_,
            "PRAGMA journal_mode;",
            -1,
            &rawStatement,
            nullptr) != SQLITE_OK)
    {
        return {};
    }

    SqliteStatement statement(rawStatement);

    if (sqlite3_step(statement.get()) != SQLITE_ROW)
    {
        return {};
    }

    return readTextColumn(statement.get(), 0);
}

/**
 * @brief 订阅MessageBus，并在回调中同步写入SQLite
 * @param messageBus 需要订阅的进程内消息总线
 * @param sourceTopic 需要持久化的源主题
 * @return 订阅成功返回true，否则返回false
 */
bool StorageService::attachMessageBus(
    MessageBus& messageBus,
    const std::string& sourceTopic)
{
    detachMessageBus();

    if (!isOpen())
    {
        Logger::error("Cannot attach MessageBus before SQLite storage is open");
        return false;
    }

    MessageBus::SubscriptionId subscriptionId = messageBus.subscribe(
        sourceTopic,
        [this](const Message& message)
        {
            if (!storeMessage(message))
            {
                Logger::error("Failed to persist MessageBus message");
            }
        });

    if (subscriptionId == MessageBus::InvalidSubscriptionId)
    {
        return false;
    }

    messageBus_ = &messageBus;
    subscriptionId_ = subscriptionId;
    return true;
}

/**
 * @brief 删除当前MessageBus持久化订阅
 */
void StorageService::detachMessageBus()
{
    if (messageBus_ != nullptr &&
        subscriptionId_ != MessageBus::InvalidSubscriptionId)
    {
        messageBus_->unsubscribe(subscriptionId_);
    }

    messageBus_ = nullptr;
    subscriptionId_ = MessageBus::InvalidSubscriptionId;
}

/**
 * @brief 在锁保护下执行无需读取结果集的SQL
 * @param sql 需要执行的SQL语句
 * @return SQLite返回SQLITE_OK时返回true
 */
bool StorageService::executeSqlLocked(const char* sql)
{
    char* errorMessage = nullptr;
    int result = sqlite3_exec(
        database_,
        sql,
        nullptr,
        nullptr,
        &errorMessage);

    if (result != SQLITE_OK)
    {
        Logger::error(
            "SQLite SQL failed: " +
            std::string(errorMessage != nullptr
                            ? errorMessage
                            : sqlite3_errmsg(database_)));
        sqlite3_free(errorMessage);
        return false;
    }

    return true;
}

/**
 * @brief 启用WAL和NORMAL同步策略，并创建消息表及时间索引
 * @return 所有SQL执行成功返回true，否则返回false
 */
bool StorageService::initializeDatabaseLocked()
{
    return executeSqlLocked("PRAGMA journal_mode=WAL;") &&
           executeSqlLocked("PRAGMA synchronous=NORMAL;") &&
           executeSqlLocked(
               "CREATE TABLE IF NOT EXISTS tag_messages("
               "id INTEGER PRIMARY KEY AUTOINCREMENT,"
               "topic TEXT NOT NULL,"
               "device_id TEXT NOT NULL,"
               "tag_name TEXT NOT NULL,"
               "value_type INTEGER NOT NULL,"
               "value_text TEXT NOT NULL,"
               "quality INTEGER NOT NULL,"
               "timestamp_ms INTEGER NOT NULL"
               ");") &&
           executeSqlLocked(
               "CREATE INDEX IF NOT EXISTS idx_tag_messages_timestamp "
               "ON tag_messages(timestamp_ms);") &&
           executeSqlLocked(
               "CREATE TABLE IF NOT EXISTS mqtt_outbox("
               "id INTEGER PRIMARY KEY AUTOINCREMENT,"
               "topic TEXT NOT NULL,"
               "payload TEXT NOT NULL,"
               "retain INTEGER NOT NULL DEFAULT 0 CHECK(retain IN (0, 1)),"
               "created_at_ms INTEGER NOT NULL"
               ");");
}

} // namespace edgelink
