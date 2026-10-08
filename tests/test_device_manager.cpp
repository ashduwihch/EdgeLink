#include "edgelink/device_manager.h"

#include <algorithm>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

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
    edgelink::DeviceManager manager;

    edgelink::Device motor;
    motor.id = "motor-01";
    motor.name = "Main Motor";
    motor.protocol = "modbus-tcp";

    if (!check(manager.addDevice(std::move(motor)), "Failed to add device") ||
        !check(manager.hasDevice("motor-01"), "Added device was not found") ||
        !check(manager.deviceCount() == 1, "Device count should be one"))
    {
        return 1;
    }

    edgelink::Device duplicate;
    duplicate.id = "motor-01";

    edgelink::Device emptyId;

    if (!check(!manager.addDevice(std::move(duplicate)), "Duplicate device ID was accepted") ||
        !check(!manager.addDevice(std::move(emptyId)), "Empty device ID was accepted") ||
        !check(manager.updateConnectionState("motor-01", edgelink::ConnectionState::Connected), "Failed to update device state") ||
        !check(!manager.updateConnectionState("missing", edgelink::ConnectionState::Connected), "Missing device state update succeeded"))
    {
        return 1;
    }

    edgelink::Tag speed;
    speed.name = "speed";
    speed.value = 1450.0;
    speed.quality = edgelink::Quality::Good;

    edgelink::Tag emptyTag;

    if (!check(manager.updateTag("motor-01", std::move(speed)), "Failed to update tag") ||
        !check(!manager.updateTag("motor-01", std::move(emptyTag)), "Empty tag name was accepted"))
    {
        return 1;
    }

    const edgelink::DeviceManager& constManager = manager;
    const edgelink::Device* storedMotor = constManager.findDevice("motor-01");
    const edgelink::Tag* storedSpeed = storedMotor != nullptr ? storedMotor->findTag("speed") : nullptr;
    const std::vector<std::string> ids = manager.deviceIds();

    if (!check(storedMotor != nullptr, "Stored device was not found") ||
        !check(storedMotor->isOnline(), "Stored device should be online") ||
        !check(storedSpeed != nullptr, "Stored tag was not found") ||
        !check(storedSpeed->isValid(), "Stored tag should be valid") ||
        !check(std::find(ids.begin(), ids.end(), "motor-01") != ids.end(), "Device ID list is incorrect") ||
        !check(manager.removeDevice("motor-01"), "Failed to remove device") ||
        !check(!manager.removeDevice("motor-01"), "Removing a missing device succeeded") ||
        !check(manager.deviceCount() == 0, "Device count should be zero"))
    {
        return 1;
    }

    edgelink::Device sensor;
    sensor.id = "sensor-01";

    if (!check(manager.addDevice(std::move(sensor)), "Failed to add second device"))
    {
        return 1;
    }

    manager.clear();

    if (!check(manager.deviceCount() == 0, "Clear did not remove all devices"))
    {
        return 1;
    }

    std::cout << "[PASS] DeviceManager test finished" << std::endl;
    return 0;
}
