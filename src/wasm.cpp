#ifdef __EMSCRIPTEN__

#include "log.h"
#include "wasm_port.h"

#include "rpc/rpc_device_load_config_task.h"
#include "rpc/rpc_device_set_task.h"
#include "rpc/rpc_helpers.h"

#include <emscripten/bind.h>

#define LOG(logger) logger.Log() << "[wasm] "

using namespace std::chrono_literals;
using namespace std::chrono;

namespace
{
    const auto TEMPLATES_DIR = "wasm/templates";

    const auto COMMON_SCHEMA_FILE = "wb-mqtt-serial-confed-common.schema.json";
    const auto TEMPLATES_SCHEMA_FILE = "wb-mqtt-serial-device-template.schema.json";

    const auto DEVICE_LOAD_CONFIG_SCHEMA_FILE = "wb-mqtt-serial-rpc-device-load-config-request.wasm-schema.json";
    const auto DEVICE_SET_SCHEMA_FILE = "wb-mqtt-serial-rpc-device-set-request.wasm-schema.json";

    PTemplateMap TemplateMap = nullptr;
    auto Port = std::make_shared<TWASMPort>();
    std::list<PSerialDevice> PolledDevices;

    class THelper
    {
        void ParseRequest(const std::string& requestString)
        {
            std::istringstream stream(requestString);
            Json::CharReaderBuilder builder;
            Json::String errors;

            if (!Json::parseFromStream(builder, stream, &Request, &errors)) {
                throw std::runtime_error("Failed to parse request:" + errors);
            }
        }

    public:
        Json::Value Request;
        TDeviceProtocolParams Params;
        PDeviceTemplate Template = nullptr;
        PSerialDevice Device = nullptr;

        THelper(const std::string& requestString, const std::string& schemaFilePath, const std::string& rpcName)
        {
            if (!TemplateMap) {
                auto schema = WBMQTT::JSON::Parse(COMMON_SCHEMA_FILE);
                TemplateMap = std::make_shared<TTemplateMap>(LoadConfigTemplatesSchema(TEMPLATES_SCHEMA_FILE, schema));
                TemplateMap->AddTemplatesDir(TEMPLATES_DIR);
            }

            ParseRequest(requestString);
            ValidateRPCRequest(Request, LoadRPCRequestSchema(schemaFilePath, rpcName));

            TSerialDeviceFactory deviceFactory;
            RegisterProtocols(deviceFactory);
            Params = deviceFactory.GetProtocolParams("modbus");

            auto config = std::make_shared<TDeviceConfig>("WASM Device", Request["slave_id"].asString(), "modbus");
            config->MaxRegHole = Modbus::MAX_HOLE_CONTINUOUS_16_BIT_REGISTERS;
            config->MaxBitHole = Modbus::MAX_HOLE_CONTINUOUS_1_BIT_REGISTERS;
            config->MaxReadRegisters = Modbus::MAX_READ_REGISTERS;
            config->ResponseTimeout = std::chrono::milliseconds(100);

            Template = TemplateMap->GetTemplate(Request["device_type"].asString());
            Device = Params.factory->CreateDevice(Template->GetTemplate(), config, Params.protocol);
        }

        TSerialClientDeviceAccessHandler GetAccessHandler()
        {
            TSerialClientRegisterAndEventsReader client({Device}, 50ms, []() { return steady_clock::now(); });
            return TSerialClientDeviceAccessHandler(client.GetEventsReader());
        }
    };
}

void OnResult(const Json::Value& result)
{
    LOG(Info) << "result: " << result;
}

void OnError(const WBMQTT::TMqttRpcErrorCode& errorCode, const std::string& errorString)
{
    LOG(Warn) << "error: " << static_cast<int>(errorCode) << " - " << errorString;
}

void DeviceLoadConfig(const std::string& requestString)
{
    try {
        THelper helper(requestString, DEVICE_LOAD_CONFIG_SCHEMA_FILE, "device/LoadConfig");
        TRPCDeviceParametersCache parametersCache;
        auto rpcRequest = ParseRPCDeviceLoadConfigRequest(helper.Request,
                                                          helper.Params,
                                                          helper.Device,
                                                          helper.Template,
                                                          false,
                                                          parametersCache,
                                                          OnResult,
                                                          OnError);
        auto accessHandler = helper.GetAccessHandler();
        TRPCDeviceLoadConfigSerialClientTask(rpcRequest).Run(Port, accessHandler, PolledDevices);
    } catch (const std::runtime_error& e) {
        LOG(Error) << "device/LoadConfig RPC exception: " << e.what();
    }
}

void DeviceSet(const std::string& requestString)
{
    try {
        THelper helper(requestString, DEVICE_SET_SCHEMA_FILE, "device/Set");
        auto rpcRequest = ParseRPCDeviceSetRequest(helper.Request,
                                                   helper.Params,
                                                   helper.Device,
                                                   helper.Template,
                                                   false,
                                                   OnResult,
                                                   OnError);
        auto accessHandler = helper.GetAccessHandler();
        TRPCDeviceSetSerialClientTask(rpcRequest).Run(Port, accessHandler, PolledDevices);
    } catch (const std::runtime_error& e) {
        LOG(Error) << "device/Set RPC exception: " << e.what();
    }
}

EMSCRIPTEN_BINDINGS(module)
{
    emscripten::function("deviceLoadConfig", &DeviceLoadConfig);
    emscripten::function("deviceSet", &DeviceSet);
}

#endif