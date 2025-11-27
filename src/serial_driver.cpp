#include "serial_driver.h"
#include "log.h"

#include <wblib/driver.h>

#include <iostream>
#include <thread>

using namespace std;
using namespace WBMQTT;

#define LOG(logger) ::logger.Log() << "[serial] "

namespace
{
    size_t GetChannelsCount(PPortConfig portConfig)
    {
        size_t res = 0;
        for (const auto& device: portConfig->Devices) {
            res += device->Channels.size();
        }
        return res;
    }

    size_t GetChannelsCount(PHandlerConfig config)
    {
        size_t res = 0;
        for (const auto& portConfig: config->PortConfigs) {
            res += GetChannelsCount(portConfig);
        }
        return res;
    }
}

TMQTTSerialDriver::TMQTTSerialDriver(PDeviceDriver mqttDriver, PHandlerConfig config): Active(false)
{
    try {
        size_t totalChannels = GetChannelsCount(config);
        for (const auto& portConfig: config->PortConfigs) {
            auto rateLimit = config->LowPriorityRegistersRateLimit;
            if (totalChannels != 0) {
                rateLimit *= GetChannelsCount(portConfig);
                rateLimit /= totalChannels;
            }
            if (rateLimit < 1) {
                rateLimit = 1;
            }
            PortDrivers.push_back(
                make_shared<TSerialPortDriver>(mqttDriver, portConfig, config->PublishParameters, rateLimit));
            PortDrivers.back()->SetUpDevices();
        }
    } catch (const exception& e) {
        LOG(Error) << "unable to create port driver: '" << e.what() << "'. Cleaning.";
        ClearDevices();
        throw;
    } catch (...) {
        LOG(Error) << "unable to create port driver. Cleaning.";
        ClearDevices();
        throw;
    }

    mqttDriver->On<TControlOnValueEvent>(&TSerialPortDriver::HandleControlOnValueEvent);
}

void TMQTTSerialDriver::LoopOnce()
{
    for (const auto& portDriver: PortDrivers)
        portDriver->Cycle();
}

void TMQTTSerialDriver::ClearDevices()
{
    for (const auto& portDriver: PortDrivers) {
        portDriver->ClearDevices();
    }
}

void TMQTTSerialDriver::Start()
{
    {
        std::lock_guard<std::mutex> lg(ActiveMutex);
        if (Active) {
            LOG(Error) << "Attempt to start already active TMQTTSerialDriver";
            return;
        }
        Active = true;
    }

    for (const auto& portDriver: PortDrivers) {
        PortLoops.emplace_back([&] {
            WBMQTT::SetThreadName(portDriver->GetShortDescription());
            while (Active) {
                portDriver->Cycle();
            }
        });
    }
}

void TMQTTSerialDriver::Stop()
{
    {
        std::lock_guard<std::mutex> lg(ActiveMutex);
        if (!Active) {
            LOG(Error) << "Attempt to stop non active TMQTTSerialDriver";
            return;
        }
        Active = false;
    }

    for (auto& loopThread: PortLoops) {
        if (loopThread.joinable()) {
            loopThread.join();
        }
    }

    ClearDevices();
}

std::vector<PSerialPortDriver> TMQTTSerialDriver::GetPortDrivers()
{
    return PortDrivers;
}
