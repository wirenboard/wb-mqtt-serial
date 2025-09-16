#ifdef __EMSCRIPTEN__

#include "log.h"
#include "wasm_port.h"

#include "rpc/rpc_device_load_config_task.h"
#include "rpc/rpc_device_set_task.h"
#include "rpc/rpc_helpers.h"
#include "rpc/rpc_port_scan_serial_client_task.h"

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
            std::stringstream stream(requestString);
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

        THelper(const std::string& requestString,
                const std::string& schemaFilePath,
                const std::string& rpcName,
                bool deviceRequest = true)
        {
            if (!TemplateMap) {
                auto schema = WBMQTT::JSON::Parse(COMMON_SCHEMA_FILE);
                TemplateMap = std::make_shared<TTemplateMap>(LoadConfigTemplatesSchema(TEMPLATES_SCHEMA_FILE, schema));
                TemplateMap->AddTemplatesDir(TEMPLATES_DIR);
            }

            ParseRequest(requestString);

            // TODO: add validation for poerScat reuest
            // ValidateRPCRequest(Request, LoadRPCRequestSchema(schemaFilePath, rpcName));

            if (!deviceRequest) {
                return;
            }

            TSerialDeviceFactory deviceFactory;
            RegisterProtocols(deviceFactory);
            Params = deviceFactory.GetProtocolParams("modbus");

            auto config = std::make_shared<TDeviceConfig>("WASM Device", Request["slave_id"].asString(), "modbus");
            config->MaxRegHole = Modbus::MAX_HOLE_CONTINUOUS_16_BIT_REGISTERS;
            config->MaxBitHole = Modbus::MAX_HOLE_CONTINUOUS_1_BIT_REGISTERS;
            config->MaxReadRegisters = Modbus::MAX_READ_REGISTERS;

            Template = TemplateMap->GetTemplate(Request["device_type"].asString());
            Device = Params.factory->CreateDevice(Template->GetTemplate(), config, Params.protocol);
        }

        TSerialClientDeviceAccessHandler GetAccessHandler()
        {
            std::list<PSerialDevice> list;

            if (Device) {
                list.push_back(Device);
            }

            TSerialClientRegisterAndEventsReader client(list, 50ms, []() { return steady_clock::now(); });
            return TSerialClientDeviceAccessHandler(client.GetEventsReader());
        }
    };
}

void OnResult(const Json::Value& result)
{
    std::stringstream stream;
    WBMQTT::JSON::MakeWriter()->write(result, &stream);

    // clang-format off
    EM_ASM(
    {
        let data = new String();

        for (let i = 0; i < $1; ++i) {
            data += String.fromCharCode(getValue($0 + i, 'i8'));
        }

        OnResult(data);
    },
    stream.str().c_str(), stream.str().length());
    // clang-format on
}

void OnError(const WBMQTT::TMqttRpcErrorCode& errorCode, const std::string& errorString)
{
    // clang-format off
    EM_ASM(
    {
        let data = new String();

        for (let i = 0; i < $1; ++i) {
            data += String.fromCharCode(getValue($0 + i, 'i8'));
        }

        OnError(data);
    },
    errorString.c_str());
    // clang-format on
}

void PortScan(const std::string& requestString)
{
    try {
        THelper helper(requestString, std::string(), "port/Scan", false);
        auto accessHandler = helper.GetAccessHandler();
        TRPCPortScanSerialClientTask(helper.Request, OnResult, OnError).Run(Port, accessHandler, PolledDevices);
    } catch (const std::runtime_error& e) {
        LOG(Error) << "port/Scan RPC exception: " << e.what();
    }
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
    emscripten::function("portScan", &PortScan);
    emscripten::function("deviceLoadConfig", &DeviceLoadConfig);
    emscripten::function("deviceSet", &DeviceSet);
}

#endif