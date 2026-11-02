#include "edgelink/system_monitor.h"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <unistd.h>

namespace
{

/**
 * @brief 检查测试条件并输出失败原因
 * @param condition 需要验证的条件
 * @param message 条件失败时的说明
 * @return 条件成立返回true
 */
bool check(bool condition, const char* message)
{
    if (!condition)
    {
        std::cerr << "[FAIL] " << message << std::endl;
        return false;
    }

    return true;
}

/**
 * @brief 创建当前测试独占的临时目录
 * @return 成功返回目录路径，失败返回空路径
 */
std::filesystem::path createTemporaryDirectory()
{
    char pathTemplate[] = "/tmp/edgelink-system-monitor-XXXXXX";
    char* directory = ::mkdtemp(pathTemplate);
    return directory != nullptr
               ? std::filesystem::path(directory)
               : std::filesystem::path{};
}

/**
 * @brief 覆盖写入一份测试虚拟文件内容
 * @param path 目标文件路径
 * @param content 文件内容
 * @return 写入成功返回true
 */
bool writeTextFile(
    const std::filesystem::path& path,
    const std::string& content)
{
    std::ofstream output(path, std::ios::trunc);
    output << content;
    return output.good();
}

} // namespace

/**
 * @brief 验证CPU差值、内存换算和thermal zone温度采集
 * @return 全部系统指标符合预期返回0
 */
int main()
{
    std::filesystem::path temporaryRoot = createTemporaryDirectory();

    if (!check(!temporaryRoot.empty(), "Failed to create temporary directory"))
    {
        return 1;
    }

    std::filesystem::path procDirectory = temporaryRoot / "proc";
    std::filesystem::path thermalDirectory = temporaryRoot / "thermal";
    std::filesystem::path thermalZone = thermalDirectory / "thermal_zone0";
    std::filesystem::create_directories(procDirectory);
    std::filesystem::create_directories(thermalZone);

    bool prepared =
        writeTextFile(
            procDirectory / "stat",
            "cpu 100 0 50 850 0 0 0 0 0 0\n") &&
        writeTextFile(
            procDirectory / "meminfo",
            "MemTotal: 1024 kB\n"
            "MemFree: 128 kB\n"
            "MemAvailable: 256 kB\n"
            "Buffers: 32 kB\n"
            "Cached: 64 kB\n") &&
        writeTextFile(thermalZone / "type", "x86_pkg_temp\n") &&
        writeTextFile(thermalZone / "temp", "42500\n");

    if (!check(prepared, "Failed to prepare virtual system files"))
    {
        std::filesystem::remove_all(temporaryRoot);
        return 1;
    }

    edgelink::SystemMonitor monitor(
        (procDirectory / "stat").string(),
        (procDirectory / "meminfo").string(),
        thermalDirectory.string());
    edgelink::SystemMetrics firstMetrics;
    bool success = true;

    success = check(
                  monitor.sample(firstMetrics),
                  "First system sample failed") &&
              success;
    success = check(
                  !firstMetrics.cpuUsageAvailable,
                  "First CPU sample should only establish a baseline") &&
              success;
    success = check(
                  firstMetrics.memoryTotalBytes == 1024ULL * 1024ULL &&
                      firstMetrics.memoryAvailableBytes == 256ULL * 1024ULL &&
                      firstMetrics.memoryUsedBytes == 768ULL * 1024ULL,
                  "Memory byte conversion is incorrect") &&
              success;
    success = check(
                  std::abs(firstMetrics.memoryUsagePercent - 75.0) < 0.001,
                  "Memory usage percentage is incorrect") &&
              success;
    success = check(
                  firstMetrics.temperatures.size() == 1 &&
                      firstMetrics.temperatures[0].zone == "thermal_zone0" &&
                      firstMetrics.temperatures[0].type == "x86_pkg_temp" &&
                      std::abs(
                          firstMetrics.temperatures[0].celsius - 42.5) < 0.001,
                  "Thermal zone parsing is incorrect") &&
              success;

    success = check(
                  writeTextFile(
                      procDirectory / "stat",
                      "cpu 160 0 70 920 0 0 0 0 0 0\n"),
                  "Failed to update CPU counters") &&
              success;

    edgelink::SystemMetrics secondMetrics;
    success = check(
                  monitor.sample(secondMetrics),
                  "Second system sample failed") &&
              success;
    success = check(
                  secondMetrics.cpuUsageAvailable &&
                      std::abs(
                          secondMetrics.cpuUsagePercent - 53.333333) < 0.001,
                  "CPU delta usage calculation is incorrect") &&
              success;

    std::filesystem::remove_all(temporaryRoot);

    if (!success)
    {
        return 1;
    }

    std::cout << "[PASS] CPU usage was calculated from /proc/stat deltas"
              << std::endl;
    std::cout << "[PASS] RAM and thermal metrics were collected"
              << std::endl;
    return 0;
}
