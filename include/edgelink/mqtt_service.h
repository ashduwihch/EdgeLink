#pragma once

#include "edgelink/message_bus.h"

#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>

struct mosquitto;

namespace edgelink
{

class StorageService;

// MQTT连接和发布所需的基础配置
struct MqttConfig
{
    std::string host = "127.0.0.1";
    std::uint16_t port = 1883;
    std::string clientId = "edgelink";
    std::uint16_t keepAliveSeconds = 60;
    int connectTimeoutMs = 3000;
    unsigned int reconnectInitialDelaySeconds = 1;
    unsigned int reconnectMaximumDelaySeconds = 30;
    int publishAckTimeoutMs = 3000;
    std::string topicPrefix = "edgelink/devices";
};

// MqttService：将EdgeLink内部Tag消息发布到MQTT Broker
class MqttService
{
public:
    /**
     * @brief 使用指定配置创建MQTT服务，但不会立即连接Broker
     * @param config MQTT连接、超时和Topic前缀配置
     */
    explicit MqttService(MqttConfig config = {});

    /**
     * @brief 释放MessageBus订阅并关闭MQTT连接
     */
    ~MqttService();

    MqttService(const MqttService&) = delete;
    MqttService& operator=(const MqttService&) = delete;

    /**
     * @brief 启动libmosquitto网络线程并等待首次连接结果
     * @return 超时前收到成功CONNACK时返回true，否则返回false；后台仍会继续重连
     */
    bool connect();

    /**
     * @brief 向Broker发送DISCONNECT报文并关闭TCP连接
     */
    void disconnect();

    /**
     * @brief 查询当前是否已完成MQTT连接握手
     * @return 已连接返回true，否则返回false
     */
    bool isConnected() const;

    /**
     * @brief 以QoS 0方式发布一条MQTT消息
     * @param topic MQTT目标主题
     * @param payload 消息正文
     * @param retain 是否要求Broker保留最后一条消息
     * @return 报文完整写入socket时返回true，否则返回false
     */
    bool publish(
        const std::string& topic,
        const std::string& payload,
        bool retain = false);

    /**
     * @brief 订阅MessageBus中的Tag更新，并自动转发到MQTT
     * @param messageBus 需要订阅的进程内消息总线
     * @param sourceTopic MessageBus中的源主题
     * @return 订阅成功返回true，否则返回false
     */
    bool attachMessageBus(
        MessageBus& messageBus,
        const std::string& sourceTopic = "device.tag.updated");

    /**
     * @brief 取消当前MessageBus订阅，停止自动转发
     */
    void detachMessageBus();

    /**
     * @brief 设置MQTT离线消息使用的SQLite存储服务
     * @param storageService 已打开且生命周期长于MqttService的存储服务
     */
    void setOfflineStorage(StorageService* storageService);

private:
    /**
     * @brief 创建libmosquitto客户端并设置连接、断线和退避参数
     * @return 客户端初始化成功返回true，否则返回false
     */
    bool initializeClientLocked();

    /**
     * @brief 接收libmosquitto的CONNACK回调并更新连接状态
     * @param client 触发回调的libmosquitto客户端
     * @param userData mosquitto_new保存的MqttService指针
     * @param resultCode Broker返回的连接结果码，0表示成功
     */
    static void handleConnect(
        struct mosquitto* client,
        void* userData,
        int resultCode);

    /**
     * @brief 接收libmosquitto断线回调并标记连接不可用
     * @param client 触发回调的libmosquitto客户端
     * @param userData mosquitto_new保存的MqttService指针
     * @param resultCode 0表示主动断开，非0表示意外掉线
     */
    static void handleDisconnect(
        struct mosquitto* client,
        void* userData,
        int resultCode);

    /**
     * @brief 接收QoS 1 PUBACK对应的发布完成回调
     * @param client 触发回调的libmosquitto客户端
     * @param userData mosquitto_new保存的MqttService指针
     * @param messageId libmosquitto分配的MQTT消息ID
     */
    static void handlePublish(
        struct mosquitto* client,
        void* userData,
        int messageId);

    /**
     * @brief 后台按SQLite自增ID顺序补传离线消息
     */
    void replayLoop();

    /**
     * @brief 使用QoS 1发布一条缓存消息并等待Broker确认
     * @param topic MQTT目标主题
     * @param payload MQTT消息正文
     * @param retain 是否设置MQTT RETAIN标志
     * @return 超时前收到对应PUBACK返回true，否则返回false
     */
    bool publishWithAcknowledgement(
        const std::string& topic,
        const std::string& payload,
        bool retain);

    /**
     * @brief 将一条统一Tag消息转换为MQTT Topic并发布
     * @param message MessageBus收到的Tag更新消息
     */
    void forwardMessage(const Message& message);

    MqttConfig config_;
    mutable std::mutex mutex_;
    std::condition_variable connectionCondition_;
    std::condition_variable replayCondition_;
    std::condition_variable publishAckCondition_;
    struct mosquitto* client_ = nullptr;
    bool loopRunning_ = false;
    bool connected_ = false;
    bool initialConnectionCompleted_ = false;
    bool stopping_ = false;
    bool replayRequested_ = false;
    bool replaying_ = false;
    int replayMessageId_ = 0;
    bool replayPublishAcknowledged_ = false;
    std::thread replayThread_;

    MessageBus* messageBus_ = nullptr;
    MessageBus::SubscriptionId subscriptionId_ =
        MessageBus::InvalidSubscriptionId;
    StorageService* offlineStorage_ = nullptr;
};

} // namespace edgelink
