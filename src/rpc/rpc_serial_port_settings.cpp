#include "rpc_serial_port_settings.h"

TSerialPortConnectionSettings ParseRPCSerialPortSettings(const Json::Value& request)
{
    TSerialPortConnectionSettings res;
    WBMQTT::JSON::Get(request, "baud_rate", res.BaudRate);
    if (request.isMember("parity")) {
        res.Parity = request["parity"].asCString()[0];
    }
    WBMQTT::JSON::Get(request, "data_bits", res.DataBits);
    WBMQTT::JSON::Get(request, "stop_bits", res.StopBits);
    return res;
}
