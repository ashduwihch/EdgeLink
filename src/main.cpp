#include "edgelink/control_command_processor.h"
#include "edgelink/config.h"
#include "edgelink/event_loop.h"
#include "edgelink/logger.h"
#include "edgelink/message_bus.h"
#include "edgelink/mqtt_service.h"
#include "edgelink/signal_handler.h"
#include "edgelink/storage_service.h"
#include "edgelink/systemd_notifier.h"
#include "edgelink/tcp_server.h"
#include "edgelink/timer.h"
#include "edgelink/unix_domain_server.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <string>
#include <utility>

/**
 * @brief 启动 EdgeLink TCP 服务、本地控制服务和事件循环。
 * @return 正常退出返回 0，任一服务启动失败返回 1。
 */
int main()
{
    edgelink::registerSignalHandlers();

    // 环境变量方便systemd指定/etc配置；手动运行时读取仓库内默认配置。
    const char* configuredConfigPath = std::getenv("EDGELINK_CONFIG");
    const std::string configPath =
        configuredConfigPath != nullptr && configuredConfigPath[0] != '\0'
            ? configuredConfigPath
            : "config/edgelink.yaml";

    edgelink::Config config;

    if (!config.load(configPath))
    {
        return 1;
    }

    // systemd服务使用/run下的运行时目录；手动启动时保持原来的/tmp路径。
    const char* configuredControlSocket =
        std::getenv("EDGELINK_CONTROL_SOCKET");
    const std::string controlSocketPath =
        configuredControlSocket != nullptr && configuredControlSocket[0] != '\0'
            ? configuredControlSocket
            : "/tmp/edgelink.sock";

    // systemd可用环境变量把数据库固定在StateDirectory中。
    const char* configuredStoragePath =
        std::getenv("EDGELINK_STORAGE_PATH");
    const std::string storagePath =
        configuredStoragePath != nullptr && configuredStoragePath[0] != '\0'
            ? configuredStoragePath
            : config.getString(
                  "storage.database_path",
                  "./data/edgelink.db");

    int mqttPort = config.getInt("mqtt.port", 1883);
    int mqttKeepAlive = config.getInt("mqtt.keep_alive_seconds", 60);
    int mqttReconnectInitial =
        config.getInt("mqtt.reconnect_initial_delay_seconds", 1);
    int mqttReconnectMaximum =
        config.getInt("mqtt.reconnect_maximum_delay_seconds", 30);

    if (mqttPort <= 0 ||
        mqttPort > std::numeric_limits<std::uint16_t>::max() ||
        mqttKeepAlive <= 0 ||
        mqttKeepAlive > std::numeric_limits<std::uint16_t>::max() ||
        mqttReconnectInitial <= 0 ||
        mqttReconnectMaximum < mqttReconnectInitial)
    {
        edgelink::Logger::error("MQTT port or keep-alive is invalid");
        return 1;
    }

    edgelink::MqttConfig mqttConfig;
    mqttConfig.host = config.getString("mqtt.host", "127.0.0.1");
    mqttConfig.port = static_cast<std::uint16_t>(mqttPort);
    mqttConfig.clientId = config.getString("mqtt.client_id", "edgelink");
    mqttConfig.keepAliveSeconds =
        static_cast<std::uint16_t>(mqttKeepAlive);
    mqttConfig.connectTimeoutMs =
        config.getInt("mqtt.connect_timeout_ms", 3000);
    mqttConfig.reconnectInitialDelaySeconds = static_cast<unsigned int>(
        mqttReconnectInitial);
    mqttConfig.reconnectMaximumDelaySeconds = static_cast<unsigned int>(
        mqttReconnectMaximum);
    mqttConfig.publishAckTimeoutMs =
        config.getInt("mqtt.publish_ack_timeout_ms", 3000);
    mqttConfig.topicPrefix = config.getString(
        "mqtt.topic_prefix",
        "edgelink/devices");

    edgelink::EventLoop eventLoop;
    edgelink::MessageBus messageBus;
    edgelink::StorageService storageService(storagePath);
    edgelink::MqttService mqttService(std::move(mqttConfig));
    edgelink::TcpServer server(&eventLoop, 9000);
    edgelink::UnixDomainServer controlServer(&eventLoop, controlSocketPath);
    edgelink::ControlCommandProcessor commandProcessor;
    edgelink::SystemdNotifier systemdNotifier;

    if (!storageService.open() ||
        !storageService.attachMessageBus(messageBus))
    {
        edgelink::Logger::error("Failed to start persistent data pipeline");
        return 1;
    }

    mqttService.setOfflineStorage(&storageService);

    if (!mqttService.attachMessageBus(messageBus))
    {
        edgelink::Logger::error("Failed to attach MQTT to MessageBus");
        return 1;
    }

    // Broker暂时离线不阻止网关启动，后续数据会落入SQLite并自动重连补传。
    if (!mqttService.connect())
    {
        edgelink::Logger::warn(
            "MQTT is offline; EdgeLink continues with persistent buffering");
    }

    if (!server.start())
    {
        edgelink::Logger::error("Failed to start TCP server");
        return 1;
    }

    // Socket层收到请求后，通过回调交给独立的命令处理器。
    controlServer.setMessageHandler([&commandProcessor](const std::string& request)
    {
        return commandProcessor.process(request);
    });

    if (!controlServer.start())
    {
        edgelink::Logger::error("Failed to start Unix Domain control server");
        server.stop();
        return 1;
    }

    // 周期检查退出信号
    edgelink::Timer signalTimer(&eventLoop);

    signalTimer.startPeriodic(200, [&eventLoop]()
    {
        if (edgelink::shutdownRequested())
        {
            eventLoop.stop();
        }
    });

    // systemd配置WatchdogSec后，通过timerfd周期报告主事件循环仍在运行。
    edgelink::Timer watchdogTimer(&eventLoop);
    std::chrono::milliseconds watchdogInterval =
        systemdNotifier.watchdogInterval();

    if (watchdogInterval.count() > 0)
    {
        std::uint64_t heartbeatIntervalMs = static_cast<std::uint64_t>(
            std::max<std::int64_t>(100, watchdogInterval.count() / 2));
        watchdogTimer.startPeriodic(
            heartbeatIntervalMs,
            [&systemdNotifier]()
            {
                systemdNotifier.notifyWatchdog();
            });
    }

    edgelink::Logger::info("EdgeLink TCP and local control servers are running");
    systemdNotifier.notifyReady("EdgeLink services are running");

    eventLoop.run();

    systemdNotifier.notifyStopping("EdgeLink is shutting down");
    controlServer.stop();
    server.stop();
    mqttService.disconnect();
    mqttService.detachMessageBus();
    storageService.detachMessageBus();
    storageService.close();

    edgelink::Logger::info("EdgeLink servers stopped");

    return 0;
}
