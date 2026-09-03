#include "edgelink/config.h"
#include "edgelink/logger.h"

namespace edgelink
{

// 从 YAML 文件加载配置
bool Config::load(const std::string& filePath)
{
    try
    {
        root_ = YAML::LoadFile(filePath);
        Logger::info("Config loaded: " + filePath);
        return true;
    }
    catch (const YAML::Exception& e)
    {
        Logger::error("Failed to load config: " + filePath);
        Logger::error(e.what());
        return false;
    }
}

// 判断指定配置项是否存在
bool Config::has(const std::string& key) const
{
    return static_cast<bool>(getNode(key));
}

// 读取字符串配置，找不到时返回默认值
std::string Config::getString(const std::string& key, const std::string& defaultValue) const
{
    YAML::Node node = getNode(key);

    if (!node)
    {
        return defaultValue;
    }

    try
    {
        return node.as<std::string>();
    }
    catch (const YAML::Exception&)
    {
        return defaultValue;
    }
}

// 读取整数配置，找不到时返回默认值
int Config::getInt(const std::string& key, int defaultValue) const
{
    YAML::Node node = getNode(key);

    if (!node)
    {
        return defaultValue;
    }

    try
    {
        return node.as<int>();
    }
    catch (const YAML::Exception&)
    {
        return defaultValue;
    }
}

// 读取布尔配置，找不到时返回默认值
bool Config::getBool(const std::string& key, bool defaultValue) const
{
    YAML::Node node = getNode(key);

    if (!node)
    {
        return defaultValue;
    }

    try
    {
        return node.as<bool>();
    }
    catch (const YAML::Exception&)
    {
        return defaultValue;
    }
}

// 根据 "app.name" 这种路径逐层查找 YAML 节点
YAML::Node Config::getNode(const std::string& key) const
{
    YAML::Node node = root_;
    std::size_t beginPos = 0;

    while (beginPos < key.size())
    {
        std::size_t dotPos = key.find('.', beginPos);
        std::string keyPart = key.substr(beginPos, dotPos - beginPos);

        if (!node || !node.IsMap())
        {
            return {};
        }

        const YAML::Node currentNode = node;
        YAML::Node nextNode = currentNode[keyPart];

        if (!nextNode)
        {
            return {};
        }

        node.reset(nextNode);

        if (dotPos == std::string::npos)
        {
            break;
        }

        beginPos = dotPos + 1;
    }

    return node;
}

}  // namespace edgelink