#include "mock_plugin.h"

#include "edgelink/logger.h"


namespace edgelink
{


// 返回插件名称
const char* MockPlugin::name() const
{
    return "MockPlugin";
}


// 初始化插件
bool MockPlugin::initialize()
{
    Logger::info("MockPlugin initialize");

    return true;
}


// 启动插件
bool MockPlugin::start()
{
    Logger::info("MockPlugin start");

    return true;
}


// 停止插件
void MockPlugin::stop()
{
    Logger::info("MockPlugin stop");
}


} // namespace edgelink



// 插件创建入口
// 使用extern "C"避免C++名称修饰，方便dlsym查找
extern "C"
edgelink::Plugin* createPlugin()
{
    return new edgelink::MockPlugin();
}


// 插件销毁入口
// 由主程序调用，用于释放插件对象
extern "C"
void destroyPlugin(edgelink::Plugin* plugin)
{
    delete plugin;
}