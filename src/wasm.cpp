#ifdef __EMSCRIPTEN__

#include "log.h"
#include "wasm_port.h"

#include "rpc/rpc_device_load_config_task.h"
#include "rpc/rpc_device_set_task.h"

#include <emscripten/bind.h>

#define LOG(logger) logger.Log() << "[wasm] "

using namespace std::chrono_literals;
using namespace std::chrono;

namespace
{
    const auto COMMON_SCHEMA_FILE = "wb-mqtt-serial-confed-common.schema.json";
    const auto TEMPLATES_SCHEMA_FILE = "wb-mqtt-serial-device-template.schema.json";
    const auto TEMPLATES_DIR = "wasm/templates";

    PTemplateMap TemplateMap = nullptr;
    auto Port = std::make_shared<TWASMPort>();
    std::list<PSerialDevice> PolledDevices;

    class THelper
    {
        void ParseRequest(const std::string& string)
        {
            std::istringstream stream(string);
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

        THelper(const std::string& request)
        {
            if (!TemplateMap) {
                auto schema = WBMQTT::JSON::Parse(COMMON_SCHEMA_FILE);
                TemplateMap = std::make_shared<TTemplateMap>(LoadConfigTemplatesSchema(TEMPLATES_SCHEMA_FILE, schema));
                TemplateMap->AddTemplatesDir(TEMPLATES_DIR);
            }

            ParseRequest(request);

            TSerialDeviceFactory deviceFactory;
            RegisterProtocols(deviceFactory);
            Params = deviceFactory.GetProtocolParams("modbus");

            auto config = std::make_shared<TDeviceConfig>("WASM Device", Request["slave_id"].asString(), "modbus");
            config->MaxRegHole = Modbus::MAX_HOLE_CONTINUOUS_16_BIT_REGISTERS;
            config->MaxBitHole = Modbus::MAX_HOLE_CONTINUOUS_1_BIT_REGISTERS;
            config->MaxReadRegisters = 10; // Modbus::MAX_READ_REGISTERS;

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

void DeviceLoadConfig(const std::string& request)
{
    try {
        THelper helper(request);
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
        LOG(Error) << "DeviceLoadConfig exception: " << e.what();
    }
}

void DeviceSet(const std::string& request)
{
    try {
        THelper helper(request);
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
        LOG(Error) << "DeviceSet exception: " << e.what();
    }
}

EMSCRIPTEN_BINDINGS(module)
{
    emscripten::function("deviceLoadConfig", &DeviceLoadConfig);
    emscripten::function("deviceSet", &DeviceSet);
}

#endif