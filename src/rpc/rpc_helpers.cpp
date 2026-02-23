#include "rpc_helpers.h"
#include "log.h"
#include "modbus_base.h"
#include "modbus_common.h"
#include "rpc_device_handler.h"
#include "rpc_exception.h"

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

void ReadModbusRegister(TPort& port,
                        TRPCDeviceRequest& request,
                        PRegisterConfig registerConfig,
                        TRegisterValue& value)
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

bool IsAllFFFE(const TRegisterValue& value)
{
    if (value.GetType() == TRegisterValue::ValueType::Integer) {
        return value.Get<uint16_t>() == 0xFFFE;
    }
    if (value.GetType() == TRegisterValue::ValueType::String) {
        // String format extracts lower byte of each 16-bit register word.
        // All-0xFFFE registers produce a string of all '\xFE' bytes.
        // String8 format extracts both bytes, producing '\xFF\xFE' pairs.
        // An empty string means GetStringRegisterValue hit '\xFF' at the
        // first character — which also indicates unsupported.
        const auto& s = value.Get<std::string>();
        if (s.empty()) {
            return true;
        }
        for (auto ch: s) {
            if (static_cast<uint8_t>(ch) != 0xFE) {
                return false;
            }
        }
        return true;
    }
    return false;
}
