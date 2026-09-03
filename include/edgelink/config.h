#pragma once

#include <string>
#include <yaml-cpp/yaml.h>

namespace edgelink
{

class Config
{
public:
    // 从 YAML 文件加载配置
    bool load(const std::string& filePath);

    // 判断指定配置项是否存在
    bool has(const std::string& key) const;

    // 读取字符串配置项
    std::string getString(const std::string& key, const std::string& defaultValue = "") const;

    // 读取整数配置项
    int getInt(const std::string& key, int defaultValue = 0) const;

    // 读取布尔配置项
    bool getBool(const std::string& key, bool defaultValue = false) const;

private:
    // 根据点分路径查找 YAML 节点
    YAML::Node getNode(const std::string& key) const;

    YAML::Node root_;
};

}  // namespace edgelink