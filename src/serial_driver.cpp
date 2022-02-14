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

TMQTTSerialDriver::TMQTTSerialDriver(PDeviceDriver mqttDriver, PHandlerConfig config, WBMQTT::PMqttRpcServer rpc)
    : Active(false)
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
    if (rpc) {
        rpc->RegisterMethod("metrics", "Load", std::bind(&TMQTTSerialDriver::LoadMetrics, this, std::placeholders::_1));
        rpc->RegisterMethod("port", "Load", std::bind(&TMQTTSerialDriver::LoadPortData, this, std::placeholders::_1));
    }
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

Json::Value TMQTTSerialDriver::LoadPortData(const Json::Value& request)
{
    Json::Value resultJSON;
    bool error = false;
    std::string errorMsg = "Success";
    int resultCode = 0;

    size_t respSize, actualRespSize;
    std::string resp;
    std::vector<uint8_t> respVect;

    int port;
    std::chrono::milliseconds respTimeout, frameTimeout;
    std::string path, ip, msg, format;
    std::vector<uint8_t> msgVect;

    bool pathEnt = request.isMember("path");
    bool ipEnt = request.isMember("ip");
    bool portEnt = request.isMember("port");
    bool msgEnt = request.isMember("msg");
    bool respTimeoutEnt = request.isMember("response_timeout");
    bool frameTimeoutEnt = request.isMember("frame_timeout");
    bool formatEnt = request.isMember("format");
    bool respSizeEnt = request.isMember("response_size");

    bool serialConf = (pathEnt && !ipEnt && !portEnt);
    bool tcpConf = (!pathEnt && ipEnt && portEnt);
    bool paramConf = msgEnt && respSizeEnt;

    if (!((tcpConf | serialConf) & paramConf)) {
        error = true;
        errorMsg = "Wrong parameters set";
        resultCode = -1;
    }

    if (!error) {
        bool correct = true;

        for (const auto key: request.getMemberNames()) {
            correct &= request[key].isString();
        }

        if (!correct) {
            error = true;
            errorMsg = "Wrong parameters type. All parameters must have a string type";
            resultCode = -2;
        }
    }

    if (!error) {
        try {

            if (respTimeoutEnt) {
                WBMQTT::JSON::Get(request, "respTimeout", respTimeout);
            } else {
                respTimeout = DefaultResponseTimeout;
            }

            if (frameTimeoutEnt) {
                WBMQTT::JSON::Get(request, "frameTimeout", frameTimeout);
            } else {
                frameTimeout = DefaultFrameTimeout;
            }

            respSize = stoi(request["response_size"].asString());
            if (respSize <= 0) {
                throw std::exception();
            }

            msg = request["msg"].asString();
            if (msg == "") {
                throw std::exception();
            }

            format = request["format"].asString();
            if (formatEnt && (format == "HEX")) {
                if (msg.size() % 2 != 0) {
                    throw std::exception();
                }

                uint8_t byte;
                for (unsigned int i = 0; i < msg.size(); i += 2) {
                    byte = strtol(msg.substr(i, 2).c_str(), NULL, 16);
                    msgVect.push_back(byte);
                }
            } else {
                msgVect.assign(msg.begin(), msg.end());
            }

            if (tcpConf) {
                port = stoi(request["port"].asString());
                if ((port < 0) | (port > 65536)) {
                    throw std::exception();
                }
            }

        } catch (const std::exception& e) {
            error = true;
            errorMsg = "Wrong parameters values. Check \"format\", \"msg\", \"port\", "
                       "\"response_size\" and timeouts";
            resultCode = -3;
        }
    }

    if (!error) {
        bool cmp_result = false;
        uint i = 0;

        while (!cmp_result & (i < PortDrivers.size())) {
            auto portDriver = PortDrivers[i];
            std::string desc = portDriver->GetShortDescription();
            std::replace(desc.begin(), desc.end(), '<', ' ');
            std::replace(desc.begin(), desc.end(), '>', ' ');

            if (serialConf) {
                std::istringstream in(desc);
                std::string path;
                in >> path;

                cmp_result = (path.compare(request["path"].asString()) == 0);
            } else {
                std::replace(desc.begin(), desc.end(), ':', ' ');
                std::istringstream in(desc);
                std::string ip, port;
                in >> ip >> port;

                bool ip_cmp = (ip.compare(request["ip"].asString()) == 0);
                bool port_cmp = (port.compare(request["port"].asString()) == 0);
                cmp_result = ip_cmp && port_cmp;
            }

            if (cmp_result) { //если нашли нужный порт
                bool errorread = false;
                portDriver->RPCWrite(msgVect, respSize, respTimeout, frameTimeout);
                while (!portDriver->RPCRead(respVect, actualRespSize, errorread)) {
                };

                if (errorread) {
                    errorMsg = "Port IO error";
                    resultCode = -5;
                } else if (actualRespSize < respSize) {
                    errorMsg = "Actual answer length shorter then requested";
                    resultCode = -6;
                } else if (format == "HEX") {
                    std::stringstream ss;
                    ss << std::hex << std::setfill('0');

                    for (size_t i = 0; i < actualRespSize; i++) {
                        ss << std::hex << std::setw(2) << static_cast<int>(respVect[i]);
                    }

                    resp = ss.str();
                }
            }

            i++;
        }

        if (!cmp_result) {
            errorMsg = "Requested port doesn't exist";
            resultCode = -4;
        }
    }

    resultJSON["result_code"] = to_string(resultCode);
    resultJSON["error_msg"] = errorMsg;
    resultJSON["response"] = resp;

    return resultJSON;
}