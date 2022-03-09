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

TMQTTSerialDriver::TMQTTSerialDriver(PDeviceDriver mqttDriver, PHandlerConfig config): Active(false)
{
    try {
        for (const auto& portConfig: config->PortConfigs) {

            if (portConfig->Devices.empty()) {
                LOG(Warn) << "no devices defined for port " << portConfig->Port->GetDescription() << ". Skipping.";
                continue;
            }

            auto& m = Metrics[portConfig->Port->GetDescription()];
            PortDrivers.push_back(make_shared<TSerialPortDriver>(mqttDriver, portConfig, config->PublishParameters, m));
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

void TMQTTSerialDriver::RPCGetMetrics(Json::Value& metrics)
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
    metrics = res;
    return;
}

bool TMQTTSerialDriver::RPCGetPortDriverByPath(const std::string& path,
                                               const std::string& ip,
                                               int port,
                                               PSerialPortDriver& portDriver)
{
    try {
        auto findPort =
            std::find_if(PortDrivers.begin(), PortDrivers.end(), [&path, &ip, &port](PSerialPortDriver item) {
                Json::Value name = item->GetPortPath();

                std::string pathItem, ipItem;
                int portItem;
                bool pathEntry = WBMQTT::JSON::Get(name, "path", pathItem);
                bool ipEntry = WBMQTT::JSON::Get(name, "ip", ipItem);
                bool portEntry = WBMQTT::JSON::Get(name, "port", portItem);

                if (!pathEntry && !(ipEntry && portEntry)) {
                    throw runtime_error("RPC meets unknown port type when try find need one");
                } else if (pathEntry && (path == pathItem)) {
                    return true;
                } else if (ipEntry && portEntry && (ip == ipItem) && (port == portItem)) {
                    return true;
                } else {
                    return false;
                }
            });

        if (findPort != PortDrivers.end()) {
            portDriver = *findPort;
            return true;
        }

    } catch (exception& e) {
        LOG(Error) << e.what();
    }

    return false;
}