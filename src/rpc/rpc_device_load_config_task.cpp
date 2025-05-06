#include "rpc_device_load_config_task.h"
#include "config_merge_template.h"
#include "rpc_helpers.h"
#include "serial_config.h"
#include "serial_port.h"
#include "wb_registers.h"
#include <string>

#define LOG(logger) ::logger.Log() << "[RPC] "

namespace
{
    const auto MAX_RREGISTERS_HOLE = 10;
    const auto MAX_BIT_HOLE = 80;
    const auto MAX_READ_RREGISTERS = 125;
    const auto MAX_RETRIES = 2;

    typedef std::vector<std::pair<std::string, PRegister>> TRPCRegisterList;

    bool ReadRegister(Modbus::IModbusTraits& traits,
                      PPort port,
                      PRPCDeviceLoadConfigRequest rpcRequest,
                      PRegisterConfig registerConfig,
                      TRegisterValue& value)
    {
        for (int i = 0; i <= MAX_RETRIES; ++i) {
            try {
                value = Modbus::ReadRegister(traits,
                                             *port,
                                             rpcRequest->SlaveId,
                                             *registerConfig,
                                             std::chrono::microseconds(0),
                                             rpcRequest->ResponseTimeout,
                                             rpcRequest->FrameTimeout);
                return true;
            } catch (const Modbus::TModbusExceptionError& err) {
                if (err.GetExceptionCode() == Modbus::ILLEGAL_FUNCTION ||
                    err.GetExceptionCode() == Modbus::ILLEGAL_DATA_ADDRESS ||
                    err.GetExceptionCode() == Modbus::ILLEGAL_DATA_VALUE)
                {
                    break;
                }
            } catch (const Modbus::TErrorBase& err) {
                if (i == MAX_RETRIES) {
                    throw;
                }
            } catch (const TResponseTimeoutException& e) {
                if (i == MAX_RETRIES) {
                    throw;
                }
            }
        }

        return false;
    }

    void ReadParametersContinuously(Modbus::IModbusTraits& traits,
                                    PPort port,
                                    PRPCDeviceLoadConfigRequest rpcRequest,
                                    TRPCRegisterList& registerList,
                                    Json::Value& parameters)
    {
        std::sort(registerList.begin(),
                  registerList.end(),
                  [](std::pair<std::string, PRegister>& a, std::pair<std::string, PRegister>& b) {
                      if (a.second->Type != b.second->Type) {
                          return a.second->Type < a.second->Type;
                      }
                      int compare = a.second->GetAddress().Compare(b.second->GetAddress());
                      if (compare != 0) {
                          return compare < 0;
                      }
                      return a.second->GetDataOffset() < b.second->GetDataOffset();
                  });

        size_t i = 0;
        while (i < registerList.size()) {
            Modbus::TModbusRegisterRange range(rpcRequest->ResponseTimeout);
            while (i < registerList.size() && range.Add(registerList[i].second, std::chrono::milliseconds::max())) {
                ++i;
            }
            if (range.GetCount() == 0) {
                break;
            }
            Modbus::TRegisterCache cache;
            bool success = false;
            std::string error;
            for (int j = 0; j <= MAX_RETRIES; ++j) {
                try {
                    range.ReadRange(traits, *port, rpcRequest->SlaveId, 0, cache);
                    success = true;
                    break;
                } catch (const Modbus::TModbusExceptionError& err) {
                    if (err.GetExceptionCode() == Modbus::ILLEGAL_FUNCTION ||
                        err.GetExceptionCode() == Modbus::ILLEGAL_DATA_ADDRESS ||
                        err.GetExceptionCode() == Modbus::ILLEGAL_DATA_VALUE)
                    {
                        error = err.what();
                        break;
                    }
                } catch (const Modbus::TErrorBase& err) {
                    if (i <= MAX_RETRIES) {
                        continue;
                    }
                    error = err.what();
                } catch (const TResponseTimeoutException& e) {
                    if (i <= MAX_RETRIES) {
                        continue;
                    }
                    error = e.what();
                }
            }
            if (!success) {
                LOG(Warn) << port->GetDescription() << " modbus:" << rpcRequest->SlaveId
                          << " unable to read register range " << range.GetStart() << ", " << range.GetCount() << ": "
                          << error;
                throw TRPCException(error, TRPCResultCode::RPC_WRONG_PARAM_VALUE);
            }
        }
        for (i = 0; i < registerList.size(); ++i) {
            auto& reg = registerList[i];
            parameters[reg.first] = RawValueToJSON(*reg.second, reg.second->GetValue());
        }
    }

    void ReadParametersConsistently(Modbus::IModbusTraits& traits,
                                    PPort port,
                                    PRPCDeviceLoadConfigRequest rpcRequest,
                                    TRPCRegisterList& registerList,
                                    Json::Value& parameters)
    {
        for (size_t i = 0; i < registerList.size(); ++i) {
            auto& reg = registerList[i];
            try {
                TRegisterValue value;
                if (ReadRegister(traits, port, rpcRequest, reg.second, value)) {
                    parameters[reg.first] = RawValueToJSON(*reg.second, value);
                }
            } catch (const Modbus::TErrorBase& err) {
                LOG(Warn) << port->GetDescription() << " modbus:" << rpcRequest->SlaveId << " unable to read \""
                          << reg.first << "\" setting: " << err.what();
                throw TRPCException(err.what(), TRPCResultCode::RPC_WRONG_PARAM_VALUE);
            }
        }
    }

} // namespace

TRPCDeviceLoadConfigRequest::TRPCDeviceLoadConfigRequest(const TSerialDeviceFactory& deviceFactory,
                                                         PDeviceTemplate deviceTemplate,
                                                         PHandlerConfig handlerConfig)
    : DeviceFactory(deviceFactory),
      DeviceType(deviceTemplate->Type),
      DeviceTemplate(deviceTemplate->GetTemplate()),
      HandlerConfig(handlerConfig)
{
    if (!deviceTemplate->GetHardware().empty()) {
        for (const auto& hardware: deviceTemplate->GetHardware()) {
            if (!hardware.Signature.empty()) {
                WBDevice = true;
                break;
            }
        }
    }

    if (WBDevice)
        WBContinuousRead = DeviceTemplate["enable_wb_continuous_read"].asBool();

    Json::Value responseTimeout = DeviceTemplate["response_timeout_ms"];
    if (responseTimeout.isInt())
        ResponseTimeout = std::chrono::milliseconds(responseTimeout.asInt());

    Json::Value frameTimeout = DeviceTemplate["frame_timeout_ms"];
    if (frameTimeout.isInt())
        FrameTimeout = std::chrono::milliseconds(frameTimeout.asInt());
}

PSerialDevice TRPCDeviceLoadConfigRequest::FindDevice(PPort port)
{
    for (const auto& portConfig: HandlerConfig->PortConfigs) {
        if (portConfig->Port != port) {
            continue;
        }
        for (const auto& device: portConfig->Devices) {
            auto deviceConfig = device->DeviceConfig();
            if (deviceConfig->SlaveId == std::to_string(SlaveId) && deviceConfig->DeviceType == DeviceType) {
                return device;
            }
        }
    }

    return nullptr;
}

PRPCDeviceLoadConfigRequest ParseRPCDeviceLoadConfigRequest(const Json::Value& request,
                                                            const TSerialDeviceFactory& deviceFactory,
                                                            PDeviceTemplate deviceTemplate,
                                                            PHandlerConfig handlerConfig)
{
    PRPCDeviceLoadConfigRequest res =
        std::make_shared<TRPCDeviceLoadConfigRequest>(deviceFactory, deviceTemplate, handlerConfig);
    res->SerialPortSettings = ParseRPCSerialPortSettings(request);
    WBMQTT::JSON::Get(request, "response_timeout", res->ResponseTimeout);
    WBMQTT::JSON::Get(request, "frame_timeout", res->FrameTimeout);
    WBMQTT::JSON::Get(request, "total_timeout", res->TotalTimeout);
    WBMQTT::JSON::Get(request, "slave_id", res->SlaveId);
    return res;
}

void ExecRPCDeviceLoadConfigRequest(PPort port, PRPCDeviceLoadConfigRequest rpcRequest)
{
    if (!rpcRequest->OnResult) {
        return;
    }

    TDeviceProtocolParams protocolParams = rpcRequest->DeviceFactory.GetProtocolParams("modbus");
    auto device = rpcRequest->FindDevice(port);
    if (device == nullptr) {
        auto config = std::make_shared<TDeviceConfig>("RPC Device", std::to_string(rpcRequest->SlaveId), "modbus");
        config->MaxRegHole = MAX_RREGISTERS_HOLE;
        config->MaxBitHole = MAX_BIT_HOLE;
        config->MaxReadRegisters = MAX_READ_RREGISTERS;
        device =
            protocolParams.factory->CreateDevice(rpcRequest->DeviceTemplate, config, port, protocolParams.protocol);
    }

    Json::Value parameters;
    for (const auto& config: device->DeviceConfig()->SetupItemConfigs) {
        auto& id = config->GetParameterId();
        if (!id.empty()) {
            parameters[id] = RawValueToJSON(*config->GetRegisterConfig(), config->GetRawValue());
        }
    }

    Modbus::TModbusRTUTraits traits;
    port->SkipNoise();

    std::string fwVersion;
    if (rpcRequest->WBDevice) {
        try {
            auto config = WbRegisters::GetRegisterConfig("fw_version");
            TRegisterValue value;
            if (ReadRegister(traits, port, rpcRequest, config, value)) {
                fwVersion = value.Get<std::string>();
            }
        } catch (const Modbus::TErrorBase& err) {
            LOG(Warn) << port->GetDescription() << " modbus:" << rpcRequest->SlaveId
                      << " unable to read \"fw_version\" register" << err.what();
            throw;
        }
    }

    bool continuousRead = false;
    if (rpcRequest->WBContinuousRead) {
        try {
            auto config = WbRegisters::GetRegisterConfig("continuous_read");
            TRegisterValue value;
            if (ReadRegister(traits, port, rpcRequest, config, value)) {
                uint16_t mode = value.Get<uint16_t>();
                continuousRead = mode == 1 || mode == 2;
            }
        } catch (const Modbus::TErrorBase& err) {
            LOG(Warn) << port->GetDescription() << " modbus:" << rpcRequest->SlaveId
                      << " unable to read \"continuous_read\" register" << err.what();
            throw;
        }
    }

    Json::Value deviceParams = rpcRequest->DeviceTemplate["parameters"];
    TRPCRegisterList registerList;
    for (auto it = deviceParams.begin(); it != deviceParams.end(); ++it) {
        const Json::Value& registerData = *it;
        std::string id = deviceParams.isObject() ? it.key().asString() : registerData["id"].asString();
        if (!parameters[id].isNull() || registerData["readonly"].asInt() != 0) {
            continue;
        }
        if (!fwVersion.empty()) {
            std::string fw = registerData["fw"].asString();
            if (!fw.empty() && CompareVersionString(fw, fwVersion) > 0) {
                continue;
            }
        }
        auto config = LoadRegisterConfig(registerData,
                                         *protocolParams.protocol->GetRegTypes(),
                                         std::string(),
                                         *protocolParams.factory,
                                         protocolParams.factory->GetRegisterAddressFactory().GetBaseRegisterAddress(),
                                         0);
        auto reg = std::make_shared<TRegister>(device, config.RegisterConfig);
        reg->SetAvailable(TRegisterAvailability::AVAILABLE);
        registerList.push_back(std::make_pair(id, reg));
    }
    if (continuousRead) {
        ReadParametersContinuously(traits, port, rpcRequest, registerList, parameters);
    } else {
        ReadParametersConsistently(traits, port, rpcRequest, registerList, parameters);
    }

    TJsonParams jsonParams(parameters);
    TExpressionsCache expressionsCache;
    for (auto it = deviceParams.begin(); it != deviceParams.end(); ++it) {
        const Json::Value& registerData = *it;
        std::string id = deviceParams.isObject() ? it.key().asString() : registerData["id"].asString();
        if (!CheckCondition(registerData, jsonParams, &expressionsCache)) {
            parameters.removeMember(id);
        }
    }

    Json::Value result;
    if (rpcRequest->WBDevice && !fwVersion.empty()) {
        result["fw"] = fwVersion;
    }
    result["parameters"] = parameters;
    rpcRequest->OnResult(result);
}

TRPCDeviceLoadConfigSerialClientTask::TRPCDeviceLoadConfigSerialClientTask(PRPCDeviceLoadConfigRequest request)
    : Request(request)
{
    ExpireTime = std::chrono::steady_clock::now() + Request->TotalTimeout;
}

ISerialClientTask::TRunResult TRPCDeviceLoadConfigSerialClientTask::Run(
    PPort port,
    TSerialClientDeviceAccessHandler& lastAccessedDevice)
{
    if (std::chrono::steady_clock::now() > ExpireTime) {
        if (Request->OnError) {
            Request->OnError(WBMQTT::E_RPC_REQUEST_TIMEOUT, "RPC request timeout");
        }
        return ISerialClientTask::TRunResult::OK;
    }

    try {
        if (!port->IsOpen()) {
            port->Open();
        }
        lastAccessedDevice.PrepareToAccess(nullptr);
        TSerialPortSettingsGuard settingsGuard(port, Request->SerialPortSettings);
        ExecRPCDeviceLoadConfigRequest(port, Request);
    } catch (const std::exception& error) {
        if (Request->OnError) {
            Request->OnError(WBMQTT::E_RPC_SERVER_ERROR, std::string("Port IO error: ") + error.what());
        }
    }

    return ISerialClientTask::TRunResult::OK;
}

Json::Value RawValueToJSON(const TRegisterConfig& reg, TRegisterValue val)
{
    switch (reg.Format) {
        case U8:
            return val.Get<uint8_t>();
        case S8:
            return val.Get<int8_t>();
        case S16:
            return val.Get<int16_t>();
        case S24: {
            uint32_t v = val.Get<uint64_t>() & 0xffffff;
            if (v & 0x800000)
                v |= 0xff000000;
            return static_cast<int32_t>(v);
        }
        case S32:
            return val.Get<int32_t>();
        case S64:
            return val.Get<int64_t>();
        case BCD8:
            return PackedBCD2Int(val.Get<uint64_t>(), WordSizes::W8_SZ);
        case BCD16:
            return PackedBCD2Int(val.Get<uint64_t>(), WordSizes::W16_SZ);
        case BCD24:
            return PackedBCD2Int(val.Get<uint64_t>(), WordSizes::W24_SZ);
        case BCD32:
            return PackedBCD2Int(val.Get<uint64_t>(), WordSizes::W32_SZ);
        case Float: {
            float v;
            auto rawValue = val.Get<uint64_t>();

            // codacy static code analysis fails on this memcpy call, have no idea how to fix it
            memcpy(&v, &rawValue, sizeof(v));

            return v;
        }
        case Double: {
            double v;
            auto rawValue = val.Get<uint64_t>();
            memcpy(&v, &rawValue, sizeof(v));
            return v;
        }
        case Char8:
            return std::string(1, val.Get<uint8_t>());
        case String:
        case String8:
            return val.Get<std::string>();
        default:
            return val.Get<uint64_t>();
    }
}

int CompareVersionString(const std::string& v1, const std::string& v2)
{
    std::string buffer;

    std::istringstream s1(v1);
    std::vector<int> n1;
    while (getline(s1, buffer, '.')) {
        n1.push_back(std::stoi(buffer));
    }

    std::istringstream s2(v2);
    std::vector<int> n2;
    while (getline(s2, buffer, '.')) {
        n2.push_back(std::stoi(buffer));
    }

    for (int i = 0; i < static_cast<int>(std::max(n1.size(), n2.size())); ++i) {
        if (n1[i] > n2[i])
            return 1;
        if (n1[i] < n2[i])
            return -1;
    }

    return 0;
}
