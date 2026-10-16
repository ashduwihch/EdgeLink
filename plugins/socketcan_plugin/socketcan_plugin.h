#pragma once

#include "edgelink/plugin.h"
#include "socketcan_transport.h"

namespace edgelink
{

/**
 * @brief 将SocketCAN传输接入EdgeLink动态插件生命周期。
 */
class SocketCanPlugin : public Plugin
{
public:
    /**
     * @brief 创建SocketCAN插件。
     * @param config CAN网络接口配置。
     */
    explicit SocketCanPlugin(SocketCanConfig config = {});

    /**
     * @brief 停止插件并关闭CAN套接字。
     */
    ~SocketCanPlugin() override;

    /**
     * @brief 获取插件名称。
     * @return 固定返回SocketCanPlugin。
     */
    const char* name() const override;

    /**
     * @brief 完成插件非硬件资源初始化。
     * @return 初始化成功返回true。
     */
    bool initialize() override;

    /**
     * @brief 打开并绑定SocketCAN接口。
     * @return 接口连接成功返回true。
     */
    bool start() override;

    /**
     * @brief 关闭SocketCAN接口。
     */
    void stop() override;

    /**
     * @brief 获取插件内部的CAN传输对象。
     * @return SocketCAN传输对象引用。
     */
    SocketCanTransport& transport();

private:
    SocketCanTransport transport_;
    bool initialized_ = false;
};

} // namespace edgelink
