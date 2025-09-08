#ifdef __EMSCRIPTEN__

#include "log.h"
#include "rpc/rpc_device_load_config_task.h"
#include "wasm_port.h"

#include <emscripten/bind.h>

#define LOG(logger) logger.Log() << "[wasm] "

using namespace std::chrono_literals;
using namespace std::chrono;

namespace
{
    const auto CONFED_COMMON_JSON_SCHEMA_FILE = "wb-mqtt-serial-confed-common.schema.json";
    const auto TEMPLATES_JSON_SCHEMA_FILE = "wb-mqtt-serial-device-template.schema.json";
    const auto TEMPLATES_DIR = "wasm/templates";

    Json::Value parseRequest(const std::string& string)
    {
        std::istringstream stream(string);

        Json::Value root;
        Json::CharReaderBuilder builder;
        Json::String errs;

        if (!Json::parseFromStream(builder, stream, &root, &errs)) {
            throw std::runtime_error("Failed to parse JSON:" + errs);
        }

        return root;
    }
}

void OnResult(const Json::Value& result)
{
    LOG(Info) << "on result: " << result;
}

void OnError(const WBMQTT::TMqttRpcErrorCode& errorCode, const std::string& errorString)
{
    LOG(Warn) << "on error: " << static_cast<int>(errorCode) << " - " << errorString;
}

void LoadConfigRequest(const std::string& requestString)
{
    Json::Value request = parseRequest(requestString);
    TRPCDeviceParametersCache parametersCache;
    TSerialDeviceFactory deviceFactory;

    LOG(Info) << "Hello from LoadConfigRequest!";
    RegisterProtocols(deviceFactory);

    try {
        auto commonDeviceSchema = std::make_shared<Json::Value>(WBMQTT::JSON::Parse(CONFED_COMMON_JSON_SCHEMA_FILE));
        auto templates =
            std::make_shared<TTemplateMap>(LoadConfigTemplatesSchema(TEMPLATES_JSON_SCHEMA_FILE, *commonDeviceSchema));
        templates->AddTemplatesDir(TEMPLATES_DIR);

        auto deviceTemplate = templates->GetTemplate(request["device_type"].asString());
        auto protocolParams = deviceFactory.GetProtocolParams("modbus");
        auto config = std::make_shared<TDeviceConfig>("WASM Device", request["slave_id"].asString(), "modbus");
        config->MaxRegHole = Modbus::MAX_HOLE_CONTINUOUS_16_BIT_REGISTERS;
        config->MaxBitHole = Modbus::MAX_HOLE_CONTINUOUS_1_BIT_REGISTERS;
        config->MaxReadRegisters = Modbus::MAX_READ_REGISTERS;

        auto device =
            protocolParams.factory->CreateDevice(deviceTemplate->GetTemplate(), config, protocolParams.protocol);

        auto rpcRequest = ParseRPCDeviceLoadConfigRequest(request,
                                                          protocolParams,
                                                          device,
                                                          deviceTemplate,
                                                          false,
                                                          parametersCache,
                                                          OnResult,
                                                          OnError);

        TRPCDeviceLoadConfigSerialClientTask task(rpcRequest);
        std::list<PSerialDevice> polledDevices;

        TSerialClientRegisterAndEventsReader serialClient({device}, 50ms, []() { return steady_clock::now(); });
        TSerialClientDeviceAccessHandler lastAccessedDevice(serialClient.GetEventsReader());

        auto port = std::make_shared<TWASMPort>();
        task.Run(port, lastAccessedDevice, polledDevices);
    } catch (const std::runtime_error& e) {
        LOG(Error) << "Exception: " << e.what();
    }
}

EMSCRIPTEN_BINDINGS(module)
{
    emscripten::function("loadConfigRequest", &LoadConfigRequest);
}

#endif