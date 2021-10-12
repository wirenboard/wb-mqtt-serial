#include "serial_driver.h"
#include "log.h"

#include <wblib/driver.h>

#include <thread>
#include <iostream>

using namespace std;
using namespace WBMQTT;


#define LOG(logger) ::logger.Log() << "[serial] "

namespace
{
    Json::Value MakeLoadItem(const Metrics::TPollItem& pollItem, const Metrics::TMetrics::TResult& value)
    {
        Json::Value item;
        auto& names = item["names"];
        if (pollItem.Controls.empty()) {
            names.append(pollItem.Device);
        } else {
            for (const auto& c: pollItem.Controls) {
                names.append(pollItem.Device + "/" + c);
            }
        }
        item["bl"] = StringFormat("%.2f", value.BusLoad.Minute * 100.0);
        item["bl15"] = StringFormat("%.2f", value.BusLoad.FifteenMinutes * 100.0);
        item["i50"] = value.Histogram.P50.count();
        item["i95"] = value.Histogram.P95.count();
        return item;
    }
}

TMQTTSerialDriver::TMQTTSerialDriver(PDeviceDriver mqttDriver, PHandlerConfig config, WBMQTT::PMqttRpcServer rpc)
    : Active(false)
{
    try {
        for (const auto& portConfig : config->PortConfigs) {
            
            if (portConfig->Devices.empty()) {
                LOG(Warn) << "no devices defined for port " << portConfig->Port->GetDescription() << ". Skipping.";
                continue;
            }

            auto& m = Metrics[portConfig->Port->GetDescription()];
            PortDrivers.push_back(make_shared<TSerialPortDriver>(mqttDriver, portConfig, config->PublishParameters, m));
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
    if (rpc) {
        rpc->RegisterMethod("metrics", "Load", std::bind(&TMQTTSerialDriver::LoadMetrics, this, std::placeholders::_1));
    }
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

Json::Value TMQTTSerialDriver::LoadMetrics(const Json::Value& request)
{
    auto time = std::chrono::steady_clock::now();
    Json::Value res(Json::arrayValue);
    for (auto& port: Metrics) {
        Json::Value item;
        item["port"] = port.first;
        Json::Value channels(Json::arrayValue);
        for (const auto& channel: port.second.GetBusLoad(time)) {
            channels.append(MakeLoadItem(channel.first, channel.second));
        }
        if (!channels.empty()) {
            item["channels"].swap(channels);
        }
        res.append(std::move(item));
    }
    return res;
}
