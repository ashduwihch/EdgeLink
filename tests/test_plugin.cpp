#include "edgelink/plugin.h"

#include <iostream>


// 声明插件入口函数

extern "C"
edgelink::Plugin* createPlugin();


extern "C"
void destroyPlugin(edgelink::Plugin* plugin);



int main()
{
    // 创建插件对象
    edgelink::Plugin* plugin = createPlugin();


    if (plugin == nullptr)
    {
        std::cout << "Failed to create plugin" << std::endl;

        return 1;
    }


    std::cout << "Plugin name: "
              << plugin->name()
              << std::endl;


    // 测试生命周期

    if (!plugin->initialize())
    {
        std::cout << "Initialize failed"
                  << std::endl;

        destroyPlugin(plugin);

        return 1;
    }


    if (!plugin->start())
    {
        std::cout << "Start failed"
                  << std::endl;

        destroyPlugin(plugin);

        return 1;
    }


    plugin->stop();


    // 销毁插件
    destroyPlugin(plugin);


    std::cout << "Plugin test finished"
              << std::endl;


    return 0;
}