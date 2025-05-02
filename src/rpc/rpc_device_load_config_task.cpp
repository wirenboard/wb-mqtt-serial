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
    const auto MAX_RETRIES = 2;

    bool ReadRegister(Modbus::IModbusTraits& traits,
                      TPort& port,
                      PRPCDeviceLoadConfigRequest rpcRequest,
                      PRegisterConfig registerConfig,
                      TRegisterValue& value)
    {
        for (int i = 0; i <= MAX_RETRIES; ++i) {
            try {
                value = Modbus::ReadRegister(traits,
                                             port,
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

    Json::Value ReadParametersConsistently(Modbus::IModbusTraits& traits,
                                           TPort& port,
                                           PRPCDeviceLoadConfigRequest rpcRequest,
                                           const std::string& fwVersion)
    {
        TDeviceProtocolParams protocolParams = rpcRequest->DeviceFactory.GetProtocolParams("modbus");
        Json::Value parameters;
        for (auto it = rpcRequest->Parameters.begin(); it != rpcRequest->Parameters.end(); ++it) {
            const Json::Value& registerData = *it;

            if (registerData["readonly"].asInt() != 0) {
                continue;
            }

            if (!fwVersion.empty()) {
                std::string fw = registerData["fw"].asString();
                if (!fw.empty() && CompareVersionString(fw, fwVersion) > 0) {
                    continue;
                }
            }

            std::string id = rpcRequest->Parameters.isObject() ? it.key().asString() : registerData["id"].asString();
            try {
                auto config =
                    LoadRegisterConfig(registerData,
                                       *protocolParams.protocol->GetRegTypes(),
                                       std::string(),
                                       *protocolParams.factory,
                                       protocolParams.factory->GetRegisterAddressFactory().GetBaseRegisterAddress(),
                                       0);
                TRegisterValue value;
                if (ReadRegister(traits, port, rpcRequest, config.RegisterConfig, value)) {
                    parameters[id] = RawValueToJSON(*config.RegisterConfig, value);
                }
            } catch (const Modbus::TErrorBase& err) {
                LOG(Warn) << port.GetDescription() << " modbus:" << rpcRequest->SlaveId << " unable to read \""
                          << it.key() << "\" setting: " << err.what();
                throw TRPCException(port.GetDescription() + " modbus:" + std::to_string(rpcRequest->SlaveId) +
                                        " failed to read settings",
                                    TRPCResultCode::RPC_WRONG_PARAM_VALUE);
                break;
            }
        }
        return parameters;
    }

    Json::Value ReadParametersContinuously(Modbus::IModbusTraits& traits,
                                           TPort& port,
                                           PRPCDeviceLoadConfigRequest rpcRequest,
                                           const std::string& fwVersion)
    {
        // TODO: add real continuously read
        return ReadParametersConsistently(traits, port, rpcRequest, fwVersion);
    }

} // namespace

TRPCDeviceLoadConfigRequest::TRPCDeviceLoadConfigRequest(const TSerialDeviceFactory& deviceFactory,
                                                         PDeviceTemplate deviceTemplate)
    : DeviceFactory(deviceFactory),
      Parameters(deviceTemplate->GetTemplate()["parameters"])

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
        WBContinuousRead = deviceTemplate->GetTemplate()["enable_wb_continuous_read"].asBool();

    Json::Value responseTimeout = deviceTemplate->GetTemplate()["response_timeout_ms"];
    if (responseTimeout.isInt())
        ResponseTimeout = std::chrono::milliseconds(responseTimeout.asInt());

    Json::Value frameTimeout = deviceTemplate->GetTemplate()["frame_timeout_ms"];
    if (frameTimeout.isInt())
        FrameTimeout = std::chrono::milliseconds(frameTimeout.asInt());
}

PRPCDeviceLoadConfigRequest ParseRPCDeviceLoadConfigRequest(const Json::Value& request,
                                                            const TSerialDeviceFactory& deviceFactory,
                                                            PDeviceTemplate deviceTemplate)
{
    PRPCDeviceLoadConfigRequest res = std::make_shared<TRPCDeviceLoadConfigRequest>(deviceFactory, deviceTemplate);
    res->SerialPortSettings = ParseRPCSerialPortSettings(request);
    WBMQTT::JSON::Get(request, "response_timeout", res->ResponseTimeout);
    WBMQTT::JSON::Get(request, "frame_timeout", res->FrameTimeout);
    WBMQTT::JSON::Get(request, "total_timeout", res->TotalTimeout);
    WBMQTT::JSON::Get(request, "slave_id", res->SlaveId);
    return res;
}

void ExecRPCDeviceLoadConfigRequest(TPort& port, PRPCDeviceLoadConfigRequest rpcRequest)
{
    if (!rpcRequest->OnResult) {
        return;
    }

    port.SkipNoise();

    Modbus::TModbusRTUTraits traits;
    std::string fwVersion;
    bool continuousRead = false;

    if (rpcRequest->WBDevice) {
        try {
            auto config = WbRegisters::GetRegisterConfig("fw_version");
            TRegisterValue value;
            if (ReadRegister(traits, port, rpcRequest, config, value)) {
                fwVersion = value.Get<std::string>();
            }
        } catch (const Modbus::TErrorBase& err) {
            LOG(Warn) << port.GetDescription() << " modbus:" << rpcRequest->SlaveId
                      << " unable to read \"fw_version\" register" << err.what();
            throw;
        }
    }

    if (rpcRequest->WBContinuousRead) {
        try {
            auto config = WbRegisters::GetRegisterConfig("continuous_read");
            TRegisterValue value;
            if (ReadRegister(traits, port, rpcRequest, config, value)) {
                uint16_t mode = value.Get<uint16_t>();
                continuousRead = mode == 1 || mode == 2;
            }
        } catch (const Modbus::TErrorBase& err) {
            LOG(Warn) << port.GetDescription() << " modbus:" << rpcRequest->SlaveId
                      << " unable to read \"continuous_read\" register" << err.what();
            throw;
        }
    }

    Json::Value parameters = continuousRead ? ReadParametersContinuously(traits, port, rpcRequest, fwVersion)
                                            : ReadParametersConsistently(traits, port, rpcRequest, fwVersion);
    TJsonParams jsonParams(parameters);
    TExpressionsCache expressionsCache;
    for (auto it = rpcRequest->Parameters.begin(); it != rpcRequest->Parameters.end(); ++it) {
        const Json::Value& registerData = *it;
        std::string id = rpcRequest->Parameters.isObject() ? it.key().asString() : registerData["id"].asString();
        if (registerData["readonly"].asInt() != 0 && !CheckCondition(registerData, jsonParams, &expressionsCache)) {
            parameters.removeMember(id);
        }
    }

    Json::Value reply;
    if (rpcRequest->WBDevice && !fwVersion.empty()) {
        reply["fw"] = fwVersion;
    }
    reply["parameters"] = parameters;
    rpcRequest->OnResult(reply);
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
        lastAccessedDevice.PrepareToAccess(nullptr);
        TSerialPortSettingsGuard settingsGuard(port, Request->SerialPortSettings);
        ExecRPCDeviceLoadConfigRequest(*port, Request);
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
