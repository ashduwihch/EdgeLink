#include "edgelink/mqtt_service.h"

#include "edgelink/logger.h"
#include "edgelink/storage_service.h"

#include <chrono>
#include <iomanip>
#include <limits>
#include <mosquitto.h>
#include <sstream>
#include <type_traits>
#include <utility>

namespace edgelink
{
namespace
{

// 进程内只初始化一次libmosquitto全局资源
class MosquittoLibrary
{
public:
    /**
     * @brief 初始化libmosquitto进程级资源
     */
    MosquittoLibrary()
        : ready_(mosquitto_lib_init() == MOSQ_ERR_SUCCESS)
    {
    }

    /**
     * @brief 在进程退出阶段释放libmosquitto全局资源
     */
    ~MosquittoLibrary()
    {
        if (ready_)
        {
            mosquitto_lib_cleanup();
        }
    }

    MosquittoLibrary(const MosquittoLibrary&) = delete;
    MosquittoLibrary& operator=(const MosquittoLibrary&) = delete;

    /**
     * @brief 查询libmosquitto全局初始化是否成功
     * @return 初始化成功返回true，否则返回false
     */
    bool ready() const
    {
        return ready_;
    }

private:
    bool ready_ = false;
};

/**
 * @brief 获取进程内唯一的libmosquitto全局资源管理器
 * @return 全局资源管理器引用
 */
MosquittoLibrary& mosquittoLibrary()
{
    static MosquittoLibrary library;
    return library;
}

/**
 * @brief 对JSON字符串中的特殊字符进行转义
 * @param value 原始字符串
 * @return 可以安全放入JSON双引号中的字符串
 */
std::string escapeJson(const std::string& value)
{
    std::ostringstream escaped;

    for (unsigned char character : value)
    {
        switch (character)
        {
            case '\"':
                escaped << "\\\"";
                break;

            case '\\':
                escaped << "\\\\";
                break;

            case '\b':
                escaped << "\\b";
                break;

            case '\f':
                escaped << "\\f";
                break;

            case '\n':
                escaped << "\\n";
                break;

            case '\r':
                escaped << "\\r";
                break;

            case '\t':
                escaped << "\\t";
                break;

            default:
                if (character < 0x20)
                {
                    escaped << "\\u"
                            << std::hex << std::setw(4) << std::setfill('0')
                            << static_cast<int>(character)
                            << std::dec;
                }
                else
                {
                    escaped << static_cast<char>(character);
                }
        }
    }

    return escaped.str();
}

/**
 * @brief 将统一TagValue转换成JSON值文本
 * @param value EdgeLink统一数据值
 * @return 对应的JSON布尔值、数值、字符串或null
 */
std::string tagValueToJson(const TagValue& value)
{
    return std::visit(
        [](const auto& currentValue) -> std::string
        {
            using ValueType = std::decay_t<decltype(currentValue)>;

            if constexpr (std::is_same_v<ValueType, std::monostate>)
            {
                return "null";
            }
            else if constexpr (std::is_same_v<ValueType, bool>)
            {
                return currentValue ? "true" : "false";
            }
            else if constexpr (std::is_same_v<ValueType, std::string>)
            {
                return "\"" + escapeJson(currentValue) + "\"";
            }
            else if constexpr (
                std::is_same_v<ValueType, std::int8_t> ||
                std::is_same_v<ValueType, std::uint8_t>)
            {
                return std::to_string(static_cast<int>(currentValue));
            }
            else
            {
                std::ostringstream output;
                output << currentValue;
                return output.str();
            }
        },
        value);
}

/**
 * @brief 把系统时钟时间点转换为Unix毫秒时间戳
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
 * @brief 将Message序列化为MQTT载荷使用的JSON
 * @param message EdgeLink内部统一消息
 * @return 包含设备、Tag、值、质量和时间戳的JSON字符串
 */
std::string messageToJson(const Message& message)
{
    std::ostringstream payload;
    payload << "{\"deviceId\":\"" << escapeJson(message.deviceId)
            << "\",\"tagName\":\"" << escapeJson(message.tagName)
            << "\",\"value\":" << tagValueToJson(message.value)
            << ",\"quality\":\"" << qualityToString(message.quality)
            << "\",\"timestampMs\":"
            << timestampToMilliseconds(message.timestamp)
            << "}";
    return payload.str();
}

} // namespace

/**
 * @brief 保存MQTT配置，等待调用connect启动客户端
 * @param config MQTT连接、重连和Topic前缀配置
 */
MqttService::MqttService(MqttConfig config)
    : config_(std::move(config))
{
}

/**
 * @brief 先取消MessageBus订阅，再停止libmosquitto网络线程
 */
MqttService::~MqttService()
{
    detachMessageBus();
    disconnect();
}

/**
 * @brief 启动异步MQTT客户端并等待首次CONNACK
 * @return 首次连接在超时前成功返回true，否则返回false，后台仍继续重连
 */
bool MqttService::connect()
{
    std::unique_lock<std::mutex> lock(mutex_);

    if (connected_)
    {
        return true;
    }

    if (loopRunning_)
    {
        return false;
    }

    if (config_.host.empty() ||
        config_.clientId.empty() ||
        config_.port == 0 ||
        config_.connectTimeoutMs <= 0 ||
        config_.publishAckTimeoutMs <= 0 ||
        config_.reconnectInitialDelaySeconds == 0 ||
        config_.reconnectMaximumDelaySeconds <
            config_.reconnectInitialDelaySeconds)
    {
        Logger::error("MQTT configuration is invalid");
        return false;
    }

    if (!initializeClientLocked())
    {
        return false;
    }

    initialConnectionCompleted_ = false;
    stopping_ = false;

    int connectResult = mosquitto_connect_async(
        client_,
        config_.host.c_str(),
        config_.port,
        config_.keepAliveSeconds);

    if (connectResult != MOSQ_ERR_SUCCESS)
    {
        Logger::error(
            "Failed to start MQTT connection: " +
            std::string(mosquitto_strerror(connectResult)));
        mosquitto_destroy(client_);
        client_ = nullptr;
        return false;
    }

    int loopResult = mosquitto_loop_start(client_);

    if (loopResult != MOSQ_ERR_SUCCESS)
    {
        Logger::error(
            "Failed to start MQTT network loop: " +
            std::string(mosquitto_strerror(loopResult)));
        mosquitto_destroy(client_);
        client_ = nullptr;
        return false;
    }

    loopRunning_ = true;
    replayRequested_ = false;
    replaying_ = false;
    replayThread_ = std::thread(&MqttService::replayLoop, this);

    bool receivedConnack = connectionCondition_.wait_for(
        lock,
        std::chrono::milliseconds(config_.connectTimeoutMs),
        [this]()
        {
            return initialConnectionCompleted_;
        });

    if (!receivedConnack)
    {
        Logger::warn(
            "Initial MQTT connection timed out; background reconnect remains active");
        return false;
    }

    return connected_;
}

/**
 * @brief 主动断开Broker并停止libmosquitto后台网络线程
 */
void MqttService::disconnect()
{
    struct mosquitto* client = nullptr;
    bool loopWasRunning = false;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        stopping_ = true;
        connected_ = false;
        initialConnectionCompleted_ = false;
        client = client_;
        client_ = nullptr;
        loopWasRunning = loopRunning_;
        loopRunning_ = false;
        replayRequested_ = false;
    }

    replayCondition_.notify_all();
    publishAckCondition_.notify_all();

    if (replayThread_.joinable())
    {
        replayThread_.join();
    }

    if (client == nullptr)
    {
        return;
    }

    int disconnectResult = mosquitto_disconnect(client);

    if (loopWasRunning)
    {
        // 在线时让网络线程发完DISCONNECT；已经掉线时才强制结束
        bool forceStop = disconnectResult != MOSQ_ERR_SUCCESS;
        mosquitto_loop_stop(client, forceStop);
    }

    mosquitto_destroy(client);
}

/**
 * @brief 在线程安全条件下查询最近一次MQTT连接状态
 * @return 已收到成功CONNACK且尚未掉线时返回true
 */
bool MqttService::isConnected() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return connected_;
}

/**
 * @brief 通过libmosquitto发布QoS 0消息
 * @param topic MQTT目标主题
 * @param payload 消息正文
 * @param retain 是否要求Broker保留最后一条消息
 * @return 消息成功交给libmosquitto发送队列时返回true
 */
bool MqttService::publish(
    const std::string& topic,
    const std::string& payload,
    bool retain)
{
    std::lock_guard<std::mutex> lock(mutex_);

    if (!connected_ || client_ == nullptr || topic.empty() ||
        payload.size() > static_cast<std::size_t>(
                             std::numeric_limits<int>::max()))
    {
        return false;
    }

    int publishResult = mosquitto_publish(
        client_,
        nullptr,
        topic.c_str(),
        static_cast<int>(payload.size()),
        payload.data(),
        0,
        retain);

    if (publishResult != MOSQ_ERR_SUCCESS)
    {
        Logger::error(
            "Failed to publish MQTT message: " +
            std::string(mosquitto_strerror(publishResult)));

        if (publishResult == MOSQ_ERR_NO_CONN ||
            publishResult == MOSQ_ERR_CONN_LOST)
        {
            connected_ = false;
        }

        return false;
    }

    return true;
}

/**
 * @brief 订阅内部Tag更新主题，并把收到的消息转发到MQTT
 * @param messageBus 需要订阅的进程内消息总线
 * @param sourceTopic MessageBus中的源主题
 * @return 成功获得订阅ID时返回true，否则返回false
 */
bool MqttService::attachMessageBus(
    MessageBus& messageBus,
    const std::string& sourceTopic)
{
    detachMessageBus();

    MessageBus::SubscriptionId subscriptionId = messageBus.subscribe(
        sourceTopic,
        [this](const Message& message)
        {
            forwardMessage(message);
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
 * @brief 从MessageBus删除当前订阅关系
 */
void MqttService::detachMessageBus()
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
 * @brief 设置发布失败时使用的SQLite离线队列
 * @param storageService 已打开且生命周期长于MqttService的存储服务
 */
void MqttService::setOfflineStorage(StorageService* storageService)
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        offlineStorage_ = storageService;

        if (connected_ && storageService != nullptr)
        {
            replayRequested_ = true;
        }
    }

    replayCondition_.notify_all();
}

/**
 * @brief 创建libmosquitto客户端并注册回调与指数退避参数
 * @return 初始化全部成功返回true，否则返回false
 */
bool MqttService::initializeClientLocked()
{
    if (!mosquittoLibrary().ready())
    {
        Logger::error("Failed to initialize libmosquitto");
        return false;
    }

    client_ = mosquitto_new(config_.clientId.c_str(), true, this);

    if (client_ == nullptr)
    {
        Logger::error("Failed to create libmosquitto client");
        return false;
    }

    mosquitto_connect_callback_set(client_, &MqttService::handleConnect);
    mosquitto_disconnect_callback_set(client_, &MqttService::handleDisconnect);
    mosquitto_publish_callback_set(client_, &MqttService::handlePublish);

    int backoffResult = mosquitto_reconnect_delay_set(
        client_,
        config_.reconnectInitialDelaySeconds,
        config_.reconnectMaximumDelaySeconds,
        true);

    if (backoffResult != MOSQ_ERR_SUCCESS)
    {
        Logger::error("Failed to configure MQTT reconnect backoff");
        mosquitto_destroy(client_);
        client_ = nullptr;
        return false;
    }

    return true;
}

/**
 * @brief 处理Broker的CONNACK并唤醒等待首次连接的线程
 * @param client 触发回调的libmosquitto客户端
 * @param userData MqttService对象指针
 * @param resultCode Broker返回的连接结果码
 */
void MqttService::handleConnect(
    struct mosquitto* client,
    void* userData,
    int resultCode)
{
    auto* service = static_cast<MqttService*>(userData);

    if (service == nullptr)
    {
        return;
    }

    bool connectionAccepted = resultCode == 0;

    {
        std::lock_guard<std::mutex> lock(service->mutex_);

        if (service->client_ != client || service->stopping_)
        {
            return;
        }

        service->connected_ = connectionAccepted;
        service->initialConnectionCompleted_ = true;

        if (connectionAccepted && service->offlineStorage_ != nullptr)
        {
            service->replayRequested_ = true;
        }
    }

    service->connectionCondition_.notify_all();
    service->replayCondition_.notify_all();

    if (connectionAccepted)
    {
        Logger::info(
            "Connected to MQTT Broker " + service->config_.host + ":" +
            std::to_string(service->config_.port));
    }
    else
    {
        Logger::warn(
            "MQTT Broker rejected connection, code=" +
            std::to_string(resultCode));
    }
}

/**
 * @brief 处理MQTT断线事件，libmosquitto随后会按指数退避重连
 * @param client 触发回调的libmosquitto客户端
 * @param userData MqttService对象指针
 * @param resultCode 0表示主动断开，非0表示意外掉线
 */
void MqttService::handleDisconnect(
    struct mosquitto* client,
    void* userData,
    int resultCode)
{
    auto* service = static_cast<MqttService*>(userData);

    if (service == nullptr)
    {
        return;
    }

    bool unexpectedDisconnect = false;

    {
        std::lock_guard<std::mutex> lock(service->mutex_);

        if (service->client_ != client)
        {
            return;
        }

        service->connected_ = false;
        unexpectedDisconnect = !service->stopping_ && resultCode != 0;
    }

    service->replayCondition_.notify_all();
    service->publishAckCondition_.notify_all();

    if (unexpectedDisconnect)
    {
        Logger::warn(
            "MQTT connection lost; automatic reconnect will continue: " +
            std::string(mosquitto_strerror(resultCode)));
    }
}

/**
 * @brief 记录QoS 1消息已收到Broker的PUBACK
 * @param client 触发回调的libmosquitto客户端
 * @param userData MqttService对象指针
 * @param messageId libmosquitto分配的MQTT消息ID
 */
void MqttService::handlePublish(
    struct mosquitto* client,
    void* userData,
    int messageId)
{
    auto* service = static_cast<MqttService*>(userData);

    if (service == nullptr)
    {
        return;
    }

    bool matchedReplayMessage = false;

    {
        std::lock_guard<std::mutex> lock(service->mutex_);

        if (service->client_ == client &&
            service->replayMessageId_ == messageId)
        {
            service->replayPublishAcknowledged_ = true;
            matchedReplayMessage = true;
        }
    }

    if (matchedReplayMessage)
    {
        service->publishAckCondition_.notify_all();
    }
}

/**
 * @brief 等待连接恢复后逐条读取、确认并删除SQLite离线消息
 */
void MqttService::replayLoop()
{
    while (true)
    {
        StorageService* storage = nullptr;

        {
            std::unique_lock<std::mutex> lock(mutex_);
            replayCondition_.wait(
                lock,
                [this]()
                {
                    return stopping_ ||
                           (connected_ &&
                            offlineStorage_ != nullptr &&
                            replayRequested_);
                });

            if (stopping_)
            {
                return;
            }

            replayRequested_ = false;
            replaying_ = true;
            storage = offlineStorage_;
        }

        while (isConnected())
        {
            std::vector<PendingMqttMessage> pending =
                storage->loadPendingMqttMessages(1);

            if (pending.empty())
            {
                break;
            }

            const PendingMqttMessage& message = pending.front();

            if (!publishWithAcknowledgement(
                    message.topic,
                    message.payload,
                    message.retain))
            {
                break;
            }

            if (!storage->removePendingMqttMessage(message.id))
            {
                Logger::error(
                    "MQTT replay succeeded but outbox row could not be removed");
                break;
            }
        }

        {
            std::lock_guard<std::mutex> lock(mutex_);
            replaying_ = false;
        }
    }
}

/**
 * @brief 以QoS 1发送缓存消息，并等待同一消息ID的PUBACK
 * @param topic MQTT目标主题
 * @param payload MQTT消息正文
 * @param retain 是否设置RETAIN标志
 * @return Broker在超时前确认返回true，否则返回false
 */
bool MqttService::publishWithAcknowledgement(
    const std::string& topic,
    const std::string& payload,
    bool retain)
{
    std::unique_lock<std::mutex> lock(mutex_);

    if (!connected_ || stopping_ || client_ == nullptr || topic.empty() ||
        payload.size() > static_cast<std::size_t>(
                             std::numeric_limits<int>::max()))
    {
        return false;
    }

    int messageId = 0;
    replayMessageId_ = 0;
    replayPublishAcknowledged_ = false;
    int publishResult = mosquitto_publish(
        client_,
        &messageId,
        topic.c_str(),
        static_cast<int>(payload.size()),
        payload.data(),
        1,
        retain);

    if (publishResult != MOSQ_ERR_SUCCESS)
    {
        Logger::warn(
            "Failed to replay MQTT message: " +
            std::string(mosquitto_strerror(publishResult)));
        return false;
    }

    replayMessageId_ = messageId;
    bool acknowledged = publishAckCondition_.wait_for(
        lock,
        std::chrono::milliseconds(config_.publishAckTimeoutMs),
        [this]()
        {
            return stopping_ || !connected_ || replayPublishAcknowledged_;
        });
    bool succeeded = acknowledged && replayPublishAcknowledged_;
    replayMessageId_ = 0;
    replayPublishAcknowledged_ = false;

    if (!succeeded && connected_ && !stopping_)
    {
        Logger::warn("Timed out waiting for MQTT PUBACK during replay");
    }

    return succeeded;
}

/**
 * @brief 生成设备Tag专属Topic和JSON，并调用publish发送
 * @param message MessageBus收到的Tag更新消息
 */
void MqttService::forwardMessage(const Message& message)
{
    if (message.deviceId.empty() || message.tagName.empty())
    {
        Logger::warn("Ignored MQTT message with empty device or tag name");
        return;
    }

    std::string topic = config_.topicPrefix + "/" + message.deviceId +
                        "/tags/" + message.tagName;
    std::string payload = messageToJson(message);
    StorageService* offlineStorage = nullptr;
    bool shouldBuffer = false;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        offlineStorage = offlineStorage_;
        shouldBuffer = !connected_ || replaying_;
    }

    if (!shouldBuffer && publish(topic, payload))
    {
        return;
    }

    if (offlineStorage == nullptr ||
        !offlineStorage->enqueueMqttMessage(topic, payload))
    {
        Logger::warn(
            "Failed to publish or persist MessageBus message for MQTT");
        return;
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);

        if (connected_)
        {
            replayRequested_ = true;
        }
    }

    replayCondition_.notify_all();
}

} // namespace edgelink
