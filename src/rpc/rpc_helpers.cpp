#include "rpc_helpers.h"
#include "log.h"
#include "modbus_base.h"
#include "modbus_common.h"
#include "rpc_exception.h"
#include "wb_registers.h"

#define LOG(logger) ::logger.Log() << "[RPC] "

TSerialPortConnectionSettings ParseRPCSerialPortSettings(const Json::Value& request)
{
    TSerialPortConnectionSettings res;
    WBMQTT::JSON::Get(request, "baud_rate", res.BaudRate);
    if (request.isMember("parity")) {
        res.Parity = request["parity"].asCString()[0];
    }
    WBMQTT::JSON::Get(request, "data_bits", res.DataBits);
    WBMQTT::JSON::Get(request, "stop_bits", res.StopBits);
    return res;
}

void ValidateRPCRequest(const Json::Value& request, const Json::Value& schema)
{
    try {
        WBMQTT::JSON::Validate(request, schema);
    } catch (const std::runtime_error& e) {
        throw TRPCException(e.what(), TRPCResultCode::RPC_WRONG_PARAM_VALUE);
    }
}

Json::Value LoadRPCRequestSchema(const std::string& schemaFilePath, const std::string& rpcName)
{
    try {
        return WBMQTT::JSON::Parse(schemaFilePath);
    } catch (const std::runtime_error& e) {
        LOG(Error) << "RPC " + rpcName + " request schema reading error: " << e.what();
        throw;
    }
}

void ReadModbusRegister(TPort& port, TRPCDeviceRequest& request, PRegisterConfig registerConfig, TRegisterValue& value)
{
    auto slaveId = static_cast<uint8_t>(std::stoi(request.Device->DeviceConfig()->SlaveId));
    std::unique_ptr<Modbus::IModbusTraits> traits;
    if (request.ProtocolParams.protocol->GetName() == "modbus-tcp") {
        traits = std::make_unique<Modbus::TModbusTCPTraits>();
    } else {
        traits = std::make_unique<Modbus::TModbusRTUTraits>();
    }
    for (int i = 0; i <= MAX_RPC_RETRIES; ++i) {
        try {
            value = Modbus::ReadRegister(*traits,
                                         port,
                                         slaveId,
                                         *registerConfig,
                                         std::chrono::microseconds(0),
                                         request.ResponseTimeout,
                                         request.FrameTimeout);
            return;
        } catch (const Modbus::TModbusExceptionError& err) {
            if (err.GetExceptionCode() == Modbus::ILLEGAL_FUNCTION ||
                err.GetExceptionCode() == Modbus::ILLEGAL_DATA_ADDRESS ||
                err.GetExceptionCode() == Modbus::ILLEGAL_DATA_VALUE)
            {
                throw;
            }
            if (i == MAX_RPC_RETRIES) {
                throw;
            }
        } catch (const Modbus::TErrorBase& err) {
            if (i == MAX_RPC_RETRIES) {
                throw;
            }
        } catch (const TResponseTimeoutException& e) {
            if (i == MAX_RPC_RETRIES) {
                throw;
            }
        }
    }
}

void WriteModbusRegister(TPort& port,
                         TRPCDeviceRequest& request,
                         PRegisterConfig registerConfig,
                         const TRegisterValue& value)
{
    auto slaveId = static_cast<uint8_t>(std::stoi(request.Device->DeviceConfig()->SlaveId));
    std::unique_ptr<Modbus::IModbusTraits> traits;
    if (request.ProtocolParams.protocol->GetName() == "modbus-tcp") {
        traits = std::make_unique<Modbus::TModbusTCPTraits>();
    } else {
        traits = std::make_unique<Modbus::TModbusRTUTraits>();
    }
    Modbus::TRegisterCache cache;
    for (int i = 0; i <= MAX_RPC_RETRIES; ++i) {
        try {
            Modbus::WriteRegister(*traits,
                                  port,
                                  slaveId,
                                  *registerConfig,
                                  value,
                                  cache,
                                  std::chrono::microseconds(0),
                                  request.ResponseTimeout,
                                  request.FrameTimeout);
            return;
        } catch (const Modbus::TModbusExceptionError& err) {
            if (err.GetExceptionCode() == Modbus::ILLEGAL_FUNCTION ||
                err.GetExceptionCode() == Modbus::ILLEGAL_DATA_ADDRESS ||
                err.GetExceptionCode() == Modbus::ILLEGAL_DATA_VALUE)
            {
                throw;
            }
            if (i == MAX_RPC_RETRIES) {
                throw;
            }
        } catch (const Modbus::TErrorBase& err) {
            if (i == MAX_RPC_RETRIES) {
                throw;
            }
        } catch (const TResponseTimeoutException& e) {
            if (i == MAX_RPC_RETRIES) {
                throw;
            }
        }
    }
}

void SetContinuousRead(TPort& port, TRPCDeviceRequest& request, bool enabled)
{
    std::string error;
    try {
        auto config = WbRegisters::GetRegisterConfig(WbRegisters::CONTINUOUS_READ_REGISTER_NAME);
        WriteModbusRegister(port, request, config, TRegisterValue(enabled));
    } catch (const Modbus::TErrorBase& err) {
        error = err.what();
    } catch (const TResponseTimeoutException& e) {
        error = e.what();
    }
    if (!error.empty()) {
        LOG(Warn) << port.GetDescription() << " modbus:" << request.Device->DeviceConfig()->SlaveId
                  << " unable to write \"" << WbRegisters::CONTINUOUS_READ_REGISTER_NAME << "\" register: " << error;
    }
}

bool CheckUnsupportedValue(const TRegisterConfig& config, const TRegisterValue& value)
{
    switch (value.GetType()) {
        case TRegisterValue::ValueType::String: {
            auto str = value.Get<std::string>();
            if (str.size() < config.Get16BitWidth()) {
                return false;
            }
            for (uint8_t i = 0; i < config.Get16BitWidth(); ++i) {
                if (str[i] != '\xFE') {
                    return false;
                }
            }
            break;
        }
        case TRegisterValue::ValueType::Integer: {
            auto v = value.Get<uint64_t>();
            for (uint8_t i = 0; i < config.Get16BitWidth(); ++i) {
                if ((v & 0xFFFF) != 0xFFFE) {
                    return false;
                }
                v >>= 16;
            }
            break;
        }
        default:
            break;
    }
    return true;
}

void MarkUnsupportedRegisterItems(TPort& port,
                                  TRPCDeviceRequest& request,
                                  TRPCRegisterList& registerList,
                                  Json::Value& data)
{
    auto continuousRead = true;
    for (const auto& item: registerList) {
        try {
            if (item.CheckUnsupported && CheckUnsupportedValue(*item.Register->GetConfig(), item.Register->GetValue()))
            {
                if (continuousRead) {
                    SetContinuousRead(port, request, false);
                    continuousRead = false;
                }
                try {
                    TRegisterValue value;
                    ReadModbusRegister(port, request, item.Register->GetConfig(), value);
                } catch (const Modbus::TModbusExceptionError& err) {
                    data[item.Id] = UNSUPPORTED_VALUE;
                }
            }
        } catch (const TRegisterValueException& e) {
        }
    }
    if (!continuousRead) {
        SetContinuousRead(port, request, true);
    }
}
