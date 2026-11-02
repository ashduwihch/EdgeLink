#include "edgelink/system_monitor.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>
#include <utility>

namespace edgelink
{
namespace
{

/**
 * @brief 去掉文本末尾的回车和换行
 * @param text 需要清理的文本
 * @return 清理后的单行字符串
 */
std::string trimLineEnding(std::string text)
{
    while (!text.empty() &&
           (text.back() == '\n' || text.back() == '\r'))
    {
        text.pop_back();
    }

    return text;
}

/**
 * @brief 将/proc/meminfo中的KiB安全换算成字节
 * @param kibibytes 以KiB为单位的数值
 * @param bytes 输出字节数
 * @return 数值未溢出时返回true
 */
bool kibibytesToBytes(
    std::uint64_t kibibytes,
    std::uint64_t& bytes)
{
    constexpr std::uint64_t BytesPerKibibyte = 1024;

    if (kibibytes >
        std::numeric_limits<std::uint64_t>::max() / BytesPerKibibyte)
    {
        return false;
    }

    bytes = kibibytes * BytesPerKibibyte;
    return true;
}

} // namespace

/**
 * @brief 保存Linux虚拟文件路径，便于生产环境和测试环境复用解析逻辑
 * @param procStatPath CPU累计时间文件路径
 * @param procMeminfoPath 内存信息文件路径
 * @param thermalRootPath thermal zone根目录
 */
SystemMonitor::SystemMonitor(
    std::string procStatPath,
    std::string procMeminfoPath,
    std::string thermalRootPath)
    : procStatPath_(std::move(procStatPath)),
      procMeminfoPath_(std::move(procMeminfoPath)),
      thermalRootPath_(std::move(thermalRootPath))
{
}

/**
 * @brief 组合CPU、内存、温度和采样时间形成统一快照
 * @param metrics 输出本次系统资源指标
 * @return CPU与内存核心指标读取成功返回true
 */
bool SystemMonitor::sample(SystemMetrics& metrics)
{
    SystemMetrics currentMetrics;

    if (!sampleCpu(
            currentMetrics.cpuUsageAvailable,
            currentMetrics.cpuUsagePercent) ||
        !sampleMemory(currentMetrics))
    {
        return false;
    }

    currentMetrics.temperatures = sampleTemperatures();
    currentMetrics.timestamp = std::chrono::system_clock::now();
    metrics = std::move(currentMetrics);
    return true;
}

/**
 * @brief 根据/proc/stat两次累计计数的差值计算CPU使用率
 * @param usageAvailable 输出是否已经拥有有效的前一次基线
 * @param usagePercent 输出0到100之间的CPU使用率
 * @return cpu汇总行能够成功解析时返回true
 */
bool SystemMonitor::sampleCpu(
    bool& usageAvailable,
    double& usagePercent)
{
    std::ifstream input(procStatPath_);
    std::string line;

    if (!input || !std::getline(input, line))
    {
        return false;
    }

    std::istringstream parser(line);
    std::string label;
    parser >> label;

    if (label != "cpu")
    {
        return false;
    }

    std::vector<std::uint64_t> counters;
    std::uint64_t counter = 0;

    while (parser >> counter)
    {
        counters.push_back(counter);
    }

    if (counters.size() < 4)
    {
        return false;
    }

    // guest和guest_nice已经包含在user/nice中，只累计前8项避免重复计算。
    std::size_t counterCount = std::min<std::size_t>(8, counters.size());
    CpuTimes currentTimes;

    for (std::size_t index = 0; index < counterCount; ++index)
    {
        if (currentTimes.total >
            std::numeric_limits<std::uint64_t>::max() - counters[index])
        {
            return false;
        }

        currentTimes.total += counters[index];
    }

    currentTimes.idle = counters[3];

    if (counters.size() > 4)
    {
        if (currentTimes.idle >
            std::numeric_limits<std::uint64_t>::max() - counters[4])
        {
            return false;
        }

        currentTimes.idle += counters[4];
    }

    usageAvailable = false;
    usagePercent = 0.0;

    if (hasPreviousCpuTimes_ &&
        currentTimes.total >= previousCpuTimes_.total &&
        currentTimes.idle >= previousCpuTimes_.idle)
    {
        std::uint64_t totalDelta =
            currentTimes.total - previousCpuTimes_.total;
        std::uint64_t idleDelta =
            currentTimes.idle - previousCpuTimes_.idle;

        if (totalDelta > 0 && idleDelta <= totalDelta)
        {
            usagePercent =
                static_cast<double>(totalDelta - idleDelta) * 100.0 /
                static_cast<double>(totalDelta);
            usageAvailable = true;
        }
    }

    previousCpuTimes_ = currentTimes;
    hasPreviousCpuTimes_ = true;
    return true;
}

/**
 * @brief 解析/proc/meminfo并计算内存占用
 * @param metrics 输出内存字节数和使用率
 * @return MemTotal和可用内存字段有效时返回true
 */
bool SystemMonitor::sampleMemory(SystemMetrics& metrics) const
{
    std::ifstream input(procMeminfoPath_);

    if (!input)
    {
        return false;
    }

    std::uint64_t totalKib = 0;
    std::uint64_t availableKib = 0;
    std::uint64_t freeKib = 0;
    std::uint64_t buffersKib = 0;
    std::uint64_t cachedKib = 0;
    bool hasAvailable = false;
    std::string line;

    while (std::getline(input, line))
    {
        std::istringstream parser(line);
        std::string key;
        std::uint64_t value = 0;
        std::string unit;

        if (!(parser >> key >> value >> unit))
        {
            continue;
        }

        if (unit != "kB")
        {
            continue;
        }

        if (key == "MemTotal:")
        {
            totalKib = value;
        }
        else if (key == "MemAvailable:")
        {
            availableKib = value;
            hasAvailable = true;
        }
        else if (key == "MemFree:")
        {
            freeKib = value;
        }
        else if (key == "Buffers:")
        {
            buffersKib = value;
        }
        else if (key == "Cached:")
        {
            cachedKib = value;
        }
    }

    if (totalKib == 0)
    {
        return false;
    }

    if (!hasAvailable)
    {
        if (freeKib > std::numeric_limits<std::uint64_t>::max() - buffersKib ||
            freeKib + buffersKib >
                std::numeric_limits<std::uint64_t>::max() - cachedKib)
        {
            return false;
        }

        availableKib = freeKib + buffersKib + cachedKib;
    }

    availableKib = std::min(availableKib, totalKib);

    if (!kibibytesToBytes(totalKib, metrics.memoryTotalBytes) ||
        !kibibytesToBytes(
            availableKib,
            metrics.memoryAvailableBytes))
    {
        return false;
    }

    metrics.memoryUsedBytes =
        metrics.memoryTotalBytes - metrics.memoryAvailableBytes;
    metrics.memoryUsagePercent =
        static_cast<double>(metrics.memoryUsedBytes) * 100.0 /
        static_cast<double>(metrics.memoryTotalBytes);
    return true;
}

/**
 * @brief 从/sys/class/thermal读取所有有效thermal zone温度
 * @return zone名称排序后的温度列表，无温度设备时返回空列表
 */
std::vector<ThermalReading> SystemMonitor::sampleTemperatures() const
{
    std::vector<ThermalReading> temperatures;
    std::error_code iteratorError;
    std::filesystem::directory_iterator iterator(
        thermalRootPath_,
        iteratorError);

    if (iteratorError)
    {
        return temperatures;
    }

    for (const std::filesystem::directory_entry& entry : iterator)
    {
        std::string zone = entry.path().filename().string();

        if (zone.rfind("thermal_zone", 0) != 0)
        {
            continue;
        }

        std::ifstream temperatureInput(entry.path() / "temp");
        long long millidegrees = 0;

        if (!(temperatureInput >> millidegrees))
        {
            continue;
        }

        std::ifstream typeInput(entry.path() / "type");
        std::string type;
        std::getline(typeInput, type);
        type = trimLineEnding(type);

        temperatures.push_back(
            {zone,
             type.empty() ? zone : type,
             static_cast<double>(millidegrees) / 1000.0});
    }

    std::sort(
        temperatures.begin(),
        temperatures.end(),
        [](const ThermalReading& left, const ThermalReading& right)
        {
            return left.zone < right.zone;
        });
    return temperatures;
}

} // namespace edgelink
