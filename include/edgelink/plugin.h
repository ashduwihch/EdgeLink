#pragma once

namespace edgelink
{

// Plugin：所有插件必须实现的统一接口，这个类用于后面子插件继承
class Plugin
{
    public:
        //一个类只要准备被当作多态基类使用，析构函数一般要写成 virtual
        // 使用虚析构，保证通过Plugin指针能够正确销毁具体插件
        virtual ~Plugin() = default;  //编译器生成默认版本

        // 获取插件名称
        virtual const char* name() const = 0;

        // 初始化插件
        virtual bool initialize() = 0;

        // 启动插件
        virtual bool start() = 0;

        // 停止插件
        virtual void stop() = 0;
    };

    // 创建插件函数类型
    //（*）是一个函数指针，（）表示函数没有参数
    //CreatePluginFunc 表示一种函数指针，这个函数没有参数，并返回一个 Plugin*
    using CreatePluginFunc = Plugin* (*)(); //函数指针类型

    // 销毁插件函数类型
    //DestroyPluginFunc 表示一种函数指针，这个函数接收一个 Plugin*，不返回东西
    using DestroyPluginFunc = void (*)(Plugin*);

}  // namespace edgelink