#include "socketcan_plugin.h"

#include "edgelink/logger.h"

#include <utility>

namespace edgelink
{

/**
 * @brief 使用指定SocketCAN配置创建插件。
 */
SocketCanPlugin::SocketCanPlugin(SocketCanConfig config)
    : transport_(std::move(config))
{
}

/**
 * @brief 在插件销毁前确保CAN套接字已关闭。
 */
SocketCanPlugin::~SocketCanPlugin()
{
    stop();
}

/**
 * @brief 返回PluginManager使用的插件名称。
 */
const char* SocketCanPlugin::name() const
{
    return "SocketCanPlugin";
}

/**
 * @brief 标记SocketCAN插件已完成初始化。
 */
bool SocketCanPlugin::initialize()
{
    initialized_ = true;
    Logger::info("SocketCanPlugin initialized");
    return true;
}

/**
 * @brief 如有需要先初始化插件，然后连接CAN接口。
 */
bool SocketCanPlugin::start()
{
    if (!initialized_ && !initialize())
    {
        return false;
    }

    return transport_.connect();
}

/**
 * @brief 停止插件并断开CAN接口。
 */
void SocketCanPlugin::stop()
{
    transport_.disconnect();
}

/**
 * @brief 返回用于发送和接收CAN帧的传输对象。
 */
SocketCanTransport& SocketCanPlugin::transport()
{
    return transport_;
}

} // namespace edgelink

/**
 * @brief 动态库导出工厂函数，创建默认使用vcan0的SocketCAN插件。
 * @return 新创建的插件对象。
 */
extern "C" edgelink::Plugin* createPlugin()
{
    return new edgelink::SocketCanPlugin();
}

/**
 * @brief 动态库导出销毁函数。
 * @param plugin 待释放的SocketCAN插件。
 */
extern "C" void destroyPlugin(edgelink::Plugin* plugin)
{
    delete plugin;
}
