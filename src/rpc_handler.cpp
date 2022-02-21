#include <rpc_handler.h>

#define LOG(logger) ::logger.Log() << "[RPC] "

TRPCHandler::TRPCHandler(PMQTTSerialDriver serialDriver, WBMQTT::PMqttRpcServer rpcServer)
    : serialDriver(serialDriver),
      rpcServer(rpcServer)
{
    rpcServer->RegisterMethod("port", "Load", std::bind(&TRPCHandler::PortLoad, this, std::placeholders::_1));
    rpcServer->RegisterMethod("metrics", "Load", std::bind(&TRPCHandler::LoadMetrics, this, std::placeholders::_1));
}

bool TRPCPortConfig::CheckParamSet(const Json::Value& request)
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
        set = serialConf ? RPCPortConfigSet::RPC_SERIAL_SET : RPCPortConfigSet::RPC_TCP_SET;
    }

    return correct;
}

bool TRPCPortConfig::LoadValues(const Json::Value& request)
{
    bool res = true;

    try {

        if (set == RPCPortConfigSet::RPC_SERIAL_SET) {
            if (!WBMQTT::JSON::Get(request, "path", path)) {
                throw std::exception();
            }
        } else {
            if (!WBMQTT::JSON::Get(request, "ip", ip) || !WBMQTT::JSON::Get(request, "port", port) ||
                (port < 0) | (port > 65536)) {
                throw std::exception();
            }
        }

        if (!WBMQTT::JSON::Get(request, "response_size", responseSize) || (responseSize < 0)) {
            throw std::exception();
        }

        if (!WBMQTT::JSON::Get(request, "response_timeout", responseTimeout)) {
            responseTimeout = DefaultResponseTimeout;
        }

        if (!WBMQTT::JSON::Get(request, "frame_timeout", frameTimeout)) {
            frameTimeout = DefaultFrameTimeout;
        }

        if (!WBMQTT::JSON::Get(request, "format", format)) {
            format = "";
        }

        std::string msgStr;
        if (!WBMQTT::JSON::Get(request, "msg", msgStr) || (msgStr == "")) {
            throw std::exception();
        }

        if (format == "HEX") {
            if (msgStr.size() % 2 != 0) {
                throw std::exception();
            }

            for (unsigned int i = 0; i < msgStr.size(); i += 2) {
                auto byte = strtol(msgStr.substr(i, 2).c_str(), NULL, 16);
                msg.push_back(byte);
            }
        } else {
            msg.assign(msgStr.begin(), msgStr.end());
        }

    } catch (std::exception e) {
        res = false;
    }

    return res;
}

namespace
{
    std::string PortLoadResponseFormat(const std::vector<uint8_t>& response,
                                       size_t actualResponseSize,
                                       std::string format)
    {
        std::string responseStr;

        if (format == "HEX") {
            std::stringstream ss;
            ss << std::hex << std::setfill('0');

            for (size_t i = 0; i < actualResponseSize; i++) {
                ss << std::hex << std::setw(2) << static_cast<int>(response[i]);
            }

            responseStr = ss.str();
        } else {
            responseStr.assign(response.begin(), response.end());
        }

        return responseStr;
    }
}

Json::Value TRPCHandler::PortLoad(const Json::Value& request)
{
    Json::Value replyJSON;
    std::string errorMsg;
    RPCPortHandlerResult resultCode;

    size_t actualResponseSize;
    std::vector<uint8_t> response;
    std::string responseStr;

    try {

        if (!config.CheckParamSet(request)) {
            throw TRPCException("Wrong mandatory parameters set", RPCPortHandlerResult::RPC_WRONG_PARAM_SET);
        }

        if (!config.LoadValues(request)) {
            throw TRPCException("Wrong parameters types or values", RPCPortHandlerResult::RPC_WRONG_PARAM_VALUE);
        }

        PSerialPortDriver portDriver;
        bool find = serialDriver->RPCGetPortDriverByName(config.path, config.ip, config.port, portDriver);
        if (!find) {
            throw TRPCException("Requested port doesn't exist", RPCPortHandlerResult::RPC_WRONG_PORT);
        }

        bool error = false;
        portDriver->RPCWrite(config.msg, config.responseSize, config.responseTimeout, config.frameTimeout);
        while (!portDriver->RPCRead(response, actualResponseSize, error)) {
        };

        if (error) {
            throw TRPCException("Port IO error", RPCPortHandlerResult::RPC_WRONG_IO);
        }

        if (actualResponseSize < config.responseSize) {
            throw TRPCException("Actual response length shorter than requested",
                                RPCPortHandlerResult::RPC_WRONG_RESP_LNGTH);
        }

        responseStr = PortLoadResponseFormat(response, actualResponseSize, config.format);
        errorMsg = "Success";
        resultCode = RPCPortHandlerResult::RPC_OK;
    } catch (TRPCException& e) {
        LOG(Error) << e.GetResultMessage();
        resultCode = e.GetResultCode();
        errorMsg = e.GetResultMessage();
        responseStr = "";
    }

    replyJSON["result_code"] = std::to_string(static_cast<int>(resultCode));
    replyJSON["error_msg"] = errorMsg;
    replyJSON["response"] = responseStr;

    return replyJSON;
}

Json::Value TRPCHandler::LoadMetrics(const Json::Value& request)
{
    Json::Value metrics;
    serialDriver->RPCGetMetrics(metrics);
    return metrics;
}