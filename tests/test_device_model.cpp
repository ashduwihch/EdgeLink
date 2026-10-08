#include "edgelink/device.h"

#include <cstdint>
#include <iostream>
#include <string>
#include <utility>
#include <variant>

namespace
{

bool check(bool condition, const char* message)
{
    if (!condition)
    {
        std::cerr << "[FAIL] " << message << std::endl;
        return false;
    }

    return true;
}

} // namespace

int main()
{
    edgelink::Device device;
    device.id = "motor-01";
    device.name = "Main Motor";
    device.protocol = "modbus-tcp";

    if (!check(!device.isOnline(), "New device should be offline"))
    {
        return 1;
    }

    device.state = edgelink::ConnectionState::Connected;

    edgelink::Tag temperature;
    temperature.name = "temperature";
    temperature.value = 62.5;
    temperature.quality = edgelink::Quality::Good;
    device.updateTag(std::move(temperature));

    edgelink::Tag running;
    running.name = "running";
    running.value = true;
    running.quality = edgelink::Quality::Good;
    device.updateTag(std::move(running));

    edgelink::Tag alarmCode;
    alarmCode.name = "alarm_code";
    alarmCode.value = std::int32_t{7};
    alarmCode.quality = edgelink::Quality::Good;
    device.updateTag(std::move(alarmCode));

    edgelink::Tag mode;
    mode.name = "mode";
    mode.value = std::string{"automatic"};
    mode.quality = edgelink::Quality::Good;
    device.updateTag(std::move(mode));

    const edgelink::Device& constDevice = device;
    const edgelink::Tag* temperatureTag = constDevice.findTag("temperature");
    const edgelink::Tag* runningTag = constDevice.findTag("running");
    const edgelink::Tag* alarmTag = constDevice.findTag("alarm_code");
    const edgelink::Tag* modeTag = constDevice.findTag("mode");

    if (!check(device.isOnline(), "Connected device should be online") ||
        !check(device.tags.size() == 4, "Device should contain four tags") ||
        !check(temperatureTag != nullptr, "Temperature tag was not found") ||
        !check(runningTag != nullptr, "Running tag was not found") ||
        !check(alarmTag != nullptr, "Alarm tag was not found") ||
        !check(modeTag != nullptr, "Mode tag was not found"))
    {
        return 1;
    }

    if (!check(std::get<double>(temperatureTag->value) == 62.5, "Double tag value is incorrect") ||
        !check(temperatureTag->isValid(), "Good tag with a value should be valid") ||
        !check(temperatureTag->timestamp.time_since_epoch().count() != 0, "Tag timestamp was not updated") ||
        !check(std::get<bool>(runningTag->value), "Boolean tag value is incorrect") ||
        !check(std::get<std::int32_t>(alarmTag->value) == 7, "Integer tag value is incorrect") ||
        !check(std::get<std::string>(modeTag->value) == "automatic", "String tag value is incorrect") ||
        !check(constDevice.findTag("missing") == nullptr, "Missing tag should not be found") ||
        !check(std::string(edgelink::qualityToString(edgelink::Quality::Timeout)) == "TIMEOUT", "Quality string is incorrect") ||
        !check(std::string(edgelink::connectionStateToString(device.state)) == "CONNECTED", "Connection state string is incorrect"))
    {
        return 1;
    }

    std::cout << "[PASS] Device model test finished" << std::endl;
    return 0;
}
