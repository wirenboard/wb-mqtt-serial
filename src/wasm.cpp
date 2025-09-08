#ifdef __EMSCRIPTEN__

#include "log.h"
#include "rpc/rpc_device_load_config_task.h"
#include "wasm_port.h"

#include <emscripten/bind.h>

#define LOG(logger) logger.Log() << "[wasm] "

using namespace std::chrono_literals;
using namespace std::chrono;

void OnResult(const Json::Value& result)
{
    LOG(Info) << "on result: " << result;
}

void OnError(const WBMQTT::TMqttRpcErrorCode& errorCode, const std::string& errorString)
{
    LOG(Warn) << "on error: " << static_cast<int>(errorCode) << " - " << errorString;
}

void LoadConfigRequest(const Json::Value& request)
{
    Json::Value deviceTemplate; // TODO: use real template

    TRPCDeviceParametersCache parametersCache;
    TSerialDeviceFactory deviceFactory;

    // hell is here
    auto port = std::make_shared<TWASMPort>();

    // do we need it?
    RegisterProtocols(deviceFactory);

    // common from device rpc
    auto protocolParams = deviceFactory.GetProtocolParams("modbus");
    auto config = std::make_shared<TDeviceConfig>("WASM Device", request["slave_id"].asString(), "modbus");
    auto device = protocolParams.factory->CreateDevice(deviceTemplate, config, protocolParams.protocol);
    config->MaxRegHole = Modbus::MAX_HOLE_CONTINUOUS_16_BIT_REGISTERS;
    config->MaxBitHole = Modbus::MAX_HOLE_CONTINUOUS_1_BIT_REGISTERS;
    config->MaxReadRegisters = Modbus::MAX_READ_REGISTERS;
    //

    auto rpcRequest = ParseRPCDeviceLoadConfigRequest(request,
                                                      protocolParams,
                                                      device,
                                                      nullptr, // helper.DeviceTemplate,
                                                      false,
                                                      parametersCache,
                                                      OnResult,
                                                      OnError);

    TRPCDeviceLoadConfigSerialClientTask task(rpcRequest);
    std::list<PSerialDevice> polledDevices;

    TSerialClientRegisterAndEventsReader serialClient({device}, 50ms, []() { return steady_clock::now(); });
    TSerialClientDeviceAccessHandler lastAccessedDevice(serialClient.GetEventsReader());

    LOG(Info) << "Hello from LoadConfigRequest";
    task.Run(port, lastAccessedDevice, polledDevices);
}

EMSCRIPTEN_BINDINGS(module)
{
    emscripten::function("loadConfigRequest", &LoadConfigRequest);
}

#endif