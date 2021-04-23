#include "serial_driver.h"
#include "log.h"

#include <wblib/driver.h>

#include <thread>
#include <iostream>

using namespace std;
using namespace WBMQTT;


#define LOG(logger) ::logger.Log() << "[serial] "

TMQTTSerialDriver::TMQTTSerialDriver(PDeviceDriver mqttDriver, PHandlerConfig config)
    : Active(false)
{
    try {
        for (const auto& portConfig : config->PortConfigs) {
            
            if (portConfig->Devices.empty()) {
                LOG(Warn) << "no devices defined for port " << portConfig->Port->GetDescription() << ". Skipping.";
                continue;
            }

            PortDrivers.push_back(make_shared<TSerialPortDriver>(mqttDriver, portConfig));
            PortDrivers.back()->SetUpDevices();
        }
    } catch (const exception & e) {
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
    for (const auto & portDriver: PortDrivers)
        portDriver->Cycle();
}

void TMQTTSerialDriver::ClearDevices()
{
    for (const auto & portDriver : PortDrivers) {
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
        PortLoops.emplace_back([&]{
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

    for (auto & loopThread : PortLoops) {
        if (loopThread.joinable()) {
            loopThread.join();
        }
    }

    ClearDevices();
}
