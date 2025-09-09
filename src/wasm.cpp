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

    PTemplateMap Templates = nullptr;
    auto Port = std::make_shared<TWASMPort>();
    std::list<PSerialDevice> PolledDevices;

    class THelper
    {
        Json::Value Request;

        TDeviceProtocolParams ProtocolParams;
        TSerialDeviceFactory DeviceFactory;

        PDeviceTemplate DeviceTemplate = nullptr;
        PSerialDevice Device = nullptr;

        void ParseRequest(const std::string& string)
        {
            std::istringstream stream(string);
            Json::CharReaderBuilder builder;
            Json::String errors;

            if (!Json::parseFromStream(builder, stream, &Request, &errors)) {
                throw std::runtime_error("Failed to parse JSON:" + errors);
            }
        }

    public:
        THelper(const std::string& request)
        {
            ParseRequest(request);
            RegisterProtocols(DeviceFactory);
            ProtocolParams = DeviceFactory.GetProtocolParams("modbus");

            if (!Templates) {
                auto schema = WBMQTT::JSON::Parse(COMMON_SCHEMA_FILE);
                Templates = std::make_shared<TTemplateMap>(LoadConfigTemplatesSchema(TEMPLATES_SCHEMA_FILE, schema));
                Templates->AddTemplatesDir(TEMPLATES_DIR);
            }
        }

        Json::Value GetRequest() const
        {
            return Request;
        }

        TDeviceProtocolParams GetProtocolParams() const
        {
            return ProtocolParams;
        }

        PDeviceTemplate GetDeviceTemplate()
        {
            auto deviceType = Request["device_type"].asString();

            if (!DeviceTemplate || DeviceTemplate->Type != deviceType) {
                DeviceTemplate = Templates->GetTemplate(deviceType);
            }

            return DeviceTemplate;
        }

        PSerialDevice GetDevice()
        {
            auto deviceType = Request["device_type"].asString();
            auto slaveId = Request["slave_id"].asString();

            if (!Device || Device->DeviceConfig()->SlaveId != slaveId ||
                Device->DeviceConfig()->DeviceType != deviceType)
            {
                auto config = std::make_shared<TDeviceConfig>("WASM Device", slaveId, "modbus");
                config->MaxRegHole = Modbus::MAX_HOLE_CONTINUOUS_16_BIT_REGISTERS;
                config->MaxBitHole = Modbus::MAX_HOLE_CONTINUOUS_1_BIT_REGISTERS;
                config->MaxReadRegisters = 10; // Modbus::MAX_READ_REGISTERS;
                Device = ProtocolParams.factory->CreateDevice(GetDeviceTemplate()->GetTemplate(),
                                                              config,
                                                              ProtocolParams.protocol);
            }

            return Device;
        }

        TSerialClientDeviceAccessHandler GetAccessHandler()
        {
            TSerialClientRegisterAndEventsReader client({GetDevice()}, 50ms, []() { return steady_clock::now(); });
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
        auto rpcRequest = ParseRPCDeviceLoadConfigRequest(helper.GetRequest(),
                                                          helper.GetProtocolParams(),
                                                          helper.GetDevice(),
                                                          helper.GetDeviceTemplate(),
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
        auto rpcRequest = ParseRPCDeviceSetRequest(helper.GetRequest(),
                                                   helper.GetProtocolParams(),
                                                   helper.GetDevice(),
                                                   helper.GetDeviceTemplate(),
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