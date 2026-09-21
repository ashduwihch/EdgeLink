#pragma once

#include "edgelink/plugin.h"

namespace edgelink
{

// MockPlugin：用于测试插件框架流程的模拟插件
class MockPlugin : public Plugin  //这里继承了Plugin,MockPlugin 是 Plugin 的一个子类
{
public:  //这里四个函数因为父类里面要求必须实现

    // 获取插件名称
    const char* name() const override; //override 表示这个函数是在重写父类虚函数

    // 初始化插件
    bool initialize() override;

    // 启动插件,具体这个插件启动的时候要干什么，之前plugin只是说要有这个东西，其他同理
    bool start() override;

    // 停止插件
    void stop() override;
};

} // namespace edgelink