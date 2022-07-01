#include "rpc_request.h"
#include "serial_device.h"

bool TRPCRequest::CheckParamSet(const Json::Value& request)
{
    bool pathEnt = request.isMember("path");
    bool ipEnt = request.isMember("ip");
    bool portEnt = request.isMember("port");
    bool msgEnt = request.isMember("msg");
    bool respSizeEnt = request.isMember("response_size");

    bool serialConf = (pathEnt && !ipEnt && !portEnt);
    bool tcpConf = (!pathEnt && ipEnt && portEnt);
    bool paramConf = msgEnt && respSizeEnt;

    bool correct = ((tcpConf || serialConf) && paramConf);
    if (correct) {
        Mode = serialConf ? TRPCModbusMode::RPC_RTU : TRPCModbusMode::RPC_TCP;
    }

    return correct;
}

bool TRPCRequest::LoadValues(const Json::Value& request)
{
    bool res = true;

    try {

        if (Mode == TRPCModbusMode::RPC_RTU) {
            if (!WBMQTT::JSON::Get(request, "path", Path)) {
                throw std::exception();
            }
        } else {
            if (!WBMQTT::JSON::Get(request, "ip", Ip) || !WBMQTT::JSON::Get(request, "port", PortNumber) ||
                (PortNumber < 0) || (PortNumber > 65536))
            {
                throw std::exception();
            }
        }

        if (!WBMQTT::JSON::Get(request, "response_size", ResponseSize) || (ResponseSize < 0)) {
            throw std::exception();
        }

        if (!WBMQTT::JSON::Get(request, "response_timeout", ResponseTimeout)) {
            ResponseTimeout = DefaultResponseTimeout;
        }

        if (!WBMQTT::JSON::Get(request, "frame_timeout", FrameTimeout)) {
            FrameTimeout = DefaultFrameTimeout;
        }

        TotalTimeout = std::chrono::seconds(10);

        if (!WBMQTT::JSON::Get(request, "format", Format)) {
            Format = "";
        }

        std::string msgStr;
        if (!WBMQTT::JSON::Get(request, "msg", msgStr) || (msgStr == "")) {
            throw std::exception();
        }

        Msg.clear();
        if (Format == "HEX") {
            if (msgStr.size() % 2 != 0) {
                throw std::exception();
            }

            for (unsigned int i = 0; i < msgStr.size(); i += 2) {
                auto byte = strtol(msgStr.substr(i, 2).c_str(), NULL, 16);
                Msg.push_back(byte);
            }
        } else {
            Msg.assign(msgStr.begin(), msgStr.end());
        }

    } catch (std::exception e) {
        res = false;
    }

    return res;
}
