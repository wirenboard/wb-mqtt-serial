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

    bool ReadParameter(Modbus::IModbusTraits& traits,
                       TPort& port,
                       PRPCDeviceLoadConfigRequest rpcRequest,
                       PRegisterConfig registerConfig,
                       TRegisterValue& value)
    {
        for (int i = 0; i <= MAX_RETRIES; i++) {
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

    void WriteParameter(Modbus::IModbusTraits& traits,
                        TPort& port,
                        PRPCDeviceLoadConfigRequest rpcRequest,
                        PRegisterConfig registerConfig,
                        const TRegisterValue& registerValue)
    {
        Modbus::TRegisterCache cache;
        for (int i = 0; i <= MAX_RETRIES; i++) {
            try {
                Modbus::WriteRegister(traits,
                                      port,
                                      rpcRequest->SlaveId,
                                      *registerConfig,
                                      registerValue,
                                      cache,
                                      std::chrono::microseconds(0),
                                      rpcRequest->ResponseTimeout,
                                      rpcRequest->FrameTimeout);
                break;
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
    }

} // namespace

TRPCDeviceLoadConfigRequest::TRPCDeviceLoadConfigRequest(const Json::Value& parameters,
                                                         const TSerialDeviceFactory& deviceFactory)
    : Parameters(parameters),
      DeviceFactory(deviceFactory)
{}

PRPCDeviceLoadConfigRequest ParseRPCDeviceLoadConfigRequest(const Json::Value& request,
                                                            const Json::Value& parameters,
                                                            const TSerialDeviceFactory& deviceFactory)
{
    PRPCDeviceLoadConfigRequest res = std::make_shared<TRPCDeviceLoadConfigRequest>(parameters, deviceFactory);
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
    auto modeConfig = WbRegisters::GetRegisterConfig("continuous_read");
    TRegisterValue modeValue;
    uint16_t mode = 0;
    bool setMode = false;

    try {
        bool read = ReadParameter(traits, port, rpcRequest, modeConfig, modeValue);
        if (read) {
            mode = modeValue.Get<uint16_t>();
            setMode = mode == 1 || mode == 2;
        }
    } catch (const Modbus::TErrorBase& err) {
        LOG(Warn) << port.GetDescription() << " modbus:" << rpcRequest->SlaveId
                  << " unable to read \"continuous_read\" setting" << err.what();
        throw;
    }

    if (setMode) {
        try {
            modeValue.Set(0);
            WriteParameter(traits, port, rpcRequest, modeConfig, modeValue);
        } catch (const Modbus::TErrorBase& err) {
            LOG(Warn) << port.GetDescription() << " modbus:" << rpcRequest->SlaveId
                      << " unable to disable \"continuous_read\" setting" << err.what();
            throw;
        }
    }

    TDeviceProtocolParams protocolParams = rpcRequest->DeviceFactory.GetProtocolParams("modbus");
    Json::Value configData;
    bool success = true;
    for (auto it = rpcRequest->Parameters.begin(); it != rpcRequest->Parameters.end(); ++it) {
        const Json::Value& registerData = *it;
        std::string id = rpcRequest->Parameters.isObject() ? it.key().asString() : registerData["id"].asString();
        if (registerData["readonly"].asInt() != 0) {
            continue;
        }
        try {
            auto config =
                LoadRegisterConfig(registerData,
                                   *protocolParams.protocol->GetRegTypes(),
                                   std::string(),
                                   *protocolParams.factory,
                                   protocolParams.factory->GetRegisterAddressFactory().GetBaseRegisterAddress(),
                                   0);
            TRegisterValue value;
            bool read = ReadParameter(traits, port, rpcRequest, config.RegisterConfig, value);
            if (read) {
                configData[id] = RawValueToJSON(*config.RegisterConfig, value);
            }
        } catch (const Modbus::TErrorBase& err) {
            LOG(Warn) << port.GetDescription() << " modbus:" << rpcRequest->SlaveId << " unable to read \"" << it.key()
                      << "\" setting: " << err.what();
            success = false;
            break;
        }
    }

    if (setMode) {
        try {
            modeValue.Set(mode);
            WriteParameter(traits, port, rpcRequest, modeConfig, modeValue);
        } catch (const Modbus::TErrorBase& err) {
            LOG(Warn) << port.GetDescription() << " modbus:" << rpcRequest->SlaveId
                      << " unable to restore \"continuous_read\" setting: " << err.what();
        }
    }

    if (!success) {
        throw TRPCException(port.GetDescription() + " modbus:" + std::to_string(rpcRequest->SlaveId) +
                                " failed to read settings",
                            TRPCResultCode::RPC_WRONG_IO);
    }

    TJsonParams jsonParams(configData);
    TExpressionsCache expressionsCache;
    for (auto it = rpcRequest->Parameters.begin(); it != rpcRequest->Parameters.end(); ++it) {
        const Json::Value& registerData = *it;
        std::string id = rpcRequest->Parameters.isObject() ? it.key().asString() : registerData["id"].asString();
        if (registerData["readonly"].asInt() != 0 && !CheckCondition(registerData, jsonParams, &expressionsCache)) {
            configData.removeMember(id);
        }
    }
    rpcRequest->OnResult(configData);
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
