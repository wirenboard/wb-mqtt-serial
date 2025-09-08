#ifdef __EMSCRIPTEN__

#include "rpc/rpc_device_load_config_task.h"
#include "wasm_port.h"

#include <emscripten/bind.h>

using namespace std::chrono_literals;
using namespace std::chrono;

int LoadConfigRequest(const Json::Value& request)
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
                                                      nullptr,  // onResult,
                                                      nullptr); // onError);

    TRPCDeviceLoadConfigSerialClientTask task(rpcRequest);
    std::list<PSerialDevice> polledDevices;

    TSerialClientRegisterAndEventsReader serialClient({device}, 50ms, []() { return steady_clock::now(); });
    TSerialClientDeviceAccessHandler lastAccessedDevice(serialClient.GetEventsReader());

    task.Run(port, lastAccessedDevice, polledDevices);

    printf("hello, world!\n");
    return 0;
}

EMSCRIPTEN_BINDINGS(module)
{
    emscripten::function("loadConfigRequest", &LoadConfigRequest);
}

#endif