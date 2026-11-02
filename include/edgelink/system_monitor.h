#pragma once

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

namespace edgelink
{

// ThermalReading：一个Linux thermal zone的温度读数
struct ThermalReading
{
    std::string zone;
    std::string type;
    double celsius = 0.0;
};

// SystemMetrics：一次系统资源采样得到的统一快照
struct SystemMetrics
{
    std::chrono::system_clock::time_point timestamp{};
    bool cpuUsageAvailable = false;
    double cpuUsagePercent = 0.0;
    std::uint64_t memoryTotalBytes = 0;
    std::uint64_t memoryAvailableBytes = 0;
    std::uint64_t memoryUsedBytes = 0;
    double memoryUsagePercent = 0.0;
    std::vector<ThermalReading> temperatures;
};

// SystemMonitor：从Linux /proc和/sys虚拟文件系统采集资源指标
class SystemMonitor
{
public:
    /**
     * @brief 创建Linux系统资源监控器
     * @param procStatPath /proc/stat或测试替代文件
     * @param procMeminfoPath /proc/meminfo或测试替代文件
     * @param thermalRootPath /sys/class/thermal或测试替代目录
     */
    explicit SystemMonitor(
        std::string procStatPath = "/proc/stat",
        std::string procMeminfoPath = "/proc/meminfo",
        std::string thermalRootPath = "/sys/class/thermal");

    /**
     * @brief 采集一次CPU、内存和温度快照
     * @param metrics 输出本次采集结果
     * @return CPU和内存数据均读取成功返回true
     * @note CPU使用率依赖两次累计计数，第一次采样只建立基线
     */
    bool sample(SystemMetrics& metrics);

private:
    struct CpuTimes
    {
        std::uint64_t total = 0;
        std::uint64_t idle = 0;
    };

    /**
     * @brief 读取CPU累计运行时间并计算相邻两次采样的使用率
     * @param usageAvailable 输出本次是否能计算使用率
     * @param usagePercent 输出CPU使用百分比
     * @return /proc/stat首行有效时返回true
     */
    bool sampleCpu(bool& usageAvailable, double& usagePercent);

    /**
     * @brief 读取Linux内存总量和可用量
     * @param metrics 输出内存相关字段
     * @return /proc/meminfo包含有效MemTotal时返回true
     */
    bool sampleMemory(SystemMetrics& metrics) const;

    /**
     * @brief 枚举thermal_zone目录并读取可用温度
     * @return 按zone名称排序的温度读数
     */
    std::vector<ThermalReading> sampleTemperatures() const;

    std::string procStatPath_;
    std::string procMeminfoPath_;
    std::string thermalRootPath_;
    bool hasPreviousCpuTimes_ = false;
    CpuTimes previousCpuTimes_;
};

} // namespace edgelink
