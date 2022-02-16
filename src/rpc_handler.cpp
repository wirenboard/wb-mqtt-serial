#include <rpc_handler.h>

enum RPCPortLoadParamSet
{
    RPC_TCP_SET,
    RPC_SERIAL_SET
};

TRPCHandler::TRPCHandler(PMQTTSerialDriver serialDriver, WBMQTT::PMqttRpcServer rpcServer)
    : serialDriver(serialDriver),
      rpcServer(rpcServer)
{
    rpcServer->RegisterMethod("port", "Load", std::bind(&TRPCHandler::PortLoad, this, std::placeholders::_1));
}

namespace
{

    bool PortLoadCheckParamSet(const Json::Value& request, enum RPCPortLoadParamSet& set)
    {
        bool pathEnt = request.isMember("path");
        bool ipEnt = request.isMember("ip");
        bool portEnt = request.isMember("port");
        bool msgEnt = request.isMember("msg");
        // bool respTimeoutEnt = request.isMember("response_timeout");
        // bool frameTimeoutEnt = request.isMember("frame_timeout");
        // bool formatEnt = request.isMember("format");
        bool respSizeEnt = request.isMember("response_size");

        bool serialConf = (pathEnt && !ipEnt && !portEnt);
        bool tcpConf = (!pathEnt && ipEnt && portEnt);
        bool paramConf = msgEnt && respSizeEnt;

        bool correct = ((tcpConf | serialConf) & paramConf);
        if (correct) {
            set = serialConf ? RPC_SERIAL_SET : RPC_TCP_SET;
        }
        return correct;
    }

    bool PortLoadGetValues(const Json::Value& request,
                           const enum RPCPortLoadParamSet& set,
                           std::string& path,
                           std::string& ip,
                           int& port,
                           std::vector<uint8_t>& msg,
                           std::chrono::milliseconds& responseTimeout,
                           std::chrono::milliseconds& frameTimeout,
                           std::string& format,
                           size_t& responseSize)
    {
        bool res = true;

        try {

            if (set == RPC_SERIAL_SET) {
                if (!WBMQTT::JSON::Get(request, "path", path)) {
                    throw std::exception();
                }
            } else {
                if (!WBMQTT::JSON::Get(request, "ip", ip) | !WBMQTT::JSON::Get(request, "port", port) | (port < 0) |
                    (port > 65536)) {
                    throw std::exception();
                }
            }

            if (!WBMQTT::JSON::Get(request, "response_size", responseSize) | (responseSize < 0)) {
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
            if (!WBMQTT::JSON::Get(request, "msg", msgStr) | (msgStr == "")) {
                throw std::exception();
            }

            if (format == "HEX") {
                uint8_t byte;
                for (unsigned int i = 0; i < msgStr.size(); i += 2) { //протестировать нечетное количество
                    byte = strtol(msgStr.substr(i, 2).c_str(), NULL, 16);
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
    RPCPortLoadResult resultCode;

    size_t responseSize, actualResponseSize;
    std::vector<uint8_t> response;
    std::string responseStr;

    enum RPCPortLoadParamSet set;
    std::string path, ip, format;
    int port;
    std::vector<uint8_t> msg;
    std::chrono::milliseconds responseTimeout, frameTimeout;

    try {

        if (!PortLoadCheckParamSet(request, set)) {
            throw TRPCException("Wrong mandatory parameters set", RPC_WRONG_PARAM_SET);
        }

        if (!PortLoadGetValues(request, set, path, ip, port, msg, responseTimeout, frameTimeout, format, responseSize))
        {
            throw TRPCException("Wrong parameters types or values", RPC_WRONG_PARAM_VALUE);
        }

        PSerialPortDriver portDriver;
        bool find = (set == RPC_SERIAL_SET) ? serialDriver->GetPortDriverByName(path, portDriver)
                                            : serialDriver->GetPortDriverByName(ip, port, portDriver);
        if (!find) {
            throw TRPCException("Requested port doesn't exist", RPC_WRONG_PORT);
        }

        bool error = false;
        portDriver->RPCWrite(msg, responseSize, responseTimeout, frameTimeout);
        while (!portDriver->RPCRead(response, actualResponseSize, error)) {
        };

        if (error) {
            throw TRPCException("Port IO error", RPC_WRONG_IO);
        }

        if (actualResponseSize < responseSize) {
            throw TRPCException("Actual response length shorter than requested", RPC_WRONG_RESP_LNGTH);
        }

        responseStr = PortLoadResponseFormat(response, actualResponseSize, format);
        errorMsg = "Success";
        resultCode = RPC_OK;
    } catch (TRPCException& e) {
        resultCode = e.GetResultCode();
        errorMsg = e.GetResultMessage();
        responseStr = "";
    }

    replyJSON["result_code"] = std::to_string((int)resultCode);
    replyJSON["error_msg"] = errorMsg;
    replyJSON["response"] = responseStr;

    return replyJSON;
}