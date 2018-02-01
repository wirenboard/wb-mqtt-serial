#include "serial_driver.h"
#include "log.h"

#include <thread>
#include <iostream>

using namespace std;
using namespace WBMQTT;


#define LOG(logger) ::logger.Log() << "[serial] "

TMQTTSerialDriver::TMQTTSerialDriver(PDeviceDriver mqttDriver, PHandlerConfig config, PAbstractSerialPort portOverride)
    : Active(false)
{
    try {
        for (const auto& portConfig : config->PortConfigs) {
            if (config->Debug) {
                portConfig->Debug = config->Debug;
            }

            if (portConfig->DeviceConfigs.empty()) {
                LOG(Warn) << "no devices defined for port " << portConfig->ConnSettings << ". Skipping.";
                continue;
            }

            PortDrivers.push_back(make_shared<TSerialPortDriver>(mqttDriver, portConfig, portOverride));
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
    if (Active.load()) {
        throw runtime_error("Start on already active TMQTTSerialDriver");
    }

    Active.store(true);
    for (const auto& portDriver: PortDrivers) {
        PortLoops.emplace_back([&]{
            while (Active.load()) {
                portDriver->Cycle();
            }
        });
    }
}

void TMQTTSerialDriver::Stop()
{
    if (!Active.load()) {
        throw runtime_error("StopLoop on non active TMQTTSerialDriver");
    }

    Active.store(false);
    for (auto & loopThread : PortLoops) {
        if (loopThread.joinable()) {
            loopThread.join();
        }
    }

    ClearDevices();
}

bool TMQTTSerialDriver::WriteInitValues()
{
    bool did_write = false;
    for (const auto & portDriver: PortDrivers) {
        if (portDriver->WriteInitValues())
            did_write = true;
    }

    return did_write;
}
