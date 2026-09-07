#include "rpc_helpers.h"
#include "devices/modbus_device.h"
#include "json_common.h"
#include "log.h"
#include "modbus_base.h"
#include "modbus_common.h"
#include "port/serial_port.h"
#include "port/tcp_port.h"
#include "rpc_exception.h"
#include "wb_registers.h"

#define LOG(logger) ::logger.Log() << "[RPC] "

namespace
{
    const std::string MODBUS_TCP_MODE = "modbus-tcp";

    //! The suffix is added to a protocol name of a device on a Modbus TCP port.
    //! It describes the port transport, not the device protocol, so it is stripped in the response
    const std::string TCP_TRANSPORT_SUFFIX = "-tcp";

    /**
     * @brief Makes a JSON object with the connection settings of the port.
     *        The settings are taken as they are configured, the settings a serial port is working
     *        with may temporarily differ during port/Setup and firmware update.
     *
     * @param addressKey name of the field with the address of a TCP port, "address" in the
     *                   ports/Load response and "ip" in the ports/List response, as the requests
     *                   of the other methods name it
     */
    Json::Value MakePortJson(const TFeaturePort& port, const std::string& addressKey)
    {
        Json::Value res;
        const auto& basePort = *port.GetBasePort();
        if (const auto* serialPort = dynamic_cast<const TSerialPort*>(&basePort)) {
            auto settings = serialPort->GetInitialSettings();
            res["path"] = settings.Device;
            res["baud_rate"] = settings.BaudRate;
            res["data_bits"] = settings.DataBits;
            res["parity"] = std::string(1, settings.Parity);
            res["stop_bits"] = settings.StopBits;
        } else if (const auto* tcpPort = dynamic_cast<const TTcpPort*>(&basePort)) {
            const auto& settings = tcpPort->GetInitialSettings();
            res[addressKey] = settings.Address;
            res["port"] = settings.Port;
        }
        if (port.IsModbusTcp()) {
            res["mode"] = MODBUS_TCP_MODE;
        }
        return res;
    }

    Json::Value MakeDeviceJson(const TSerialDevice& device)
    {
        const auto& deviceConfig = *device.DeviceConfig();

        Json::Value res;
        res["device_id"] = deviceConfig.Id;
        // A hexadecimal address is reported as written in the configuration.
        // A device configured without an address, for broadcast mode, has none to report
        if (!deviceConfig.SlaveId.empty()) {
            res["slave_id"] = deviceConfig.SlaveId;
        }
        // Devices configured without a template have no device type
        if (!deviceConfig.DeviceType.empty()) {
            res["device_type"] = deviceConfig.DeviceType;
        }
        std::string protocol(device.Protocol()->GetName());
        if (protocol.ends_with(TCP_TRANSPORT_SUFFIX)) {
            protocol.erase(protocol.size() - TCP_TRANSPORT_SUFFIX.size());
        }
        res["protocol"] = protocol;
        return res;
    }
}

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

std::unique_ptr<Modbus::IModbusTraits> MakeModbusTraits(const std::string& protocol)
{
    if (protocol == "modbus-tcp") {
        return std::make_unique<Modbus::TModbusTCPTraits>();
    }
    return std::make_unique<Modbus::TModbusRTUTraits>();
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

uint32_t GetModbusSlaveId(const TSerialDevice& device)
{
    const auto* slaveId = dynamic_cast<const TUInt32SlaveId*>(&device);
    if (slaveId == nullptr) {
        throw TRPCException("Device protocol \"" + device.Protocol()->GetName() + "\" has no Modbus address",
                            TRPCResultCode::RPC_WRONG_PARAM_VALUE);
    }
    return slaveId->SlaveId;
}

void ReadModbusRegister(TPort& port, TRPCDeviceRequest& request, PRegisterConfig registerConfig, TRegisterValue& value)
{
    auto slaveId = GetModbusSlaveId(*request.Device);
    auto traits = MakeModbusTraits(request.ProtocolParams.protocol->GetName());
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
    auto slaveId = GetModbusSlaveId(*request.Device);
    auto traits = MakeModbusTraits(request.ProtocolParams.protocol->GetName());
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

void SetContinuousRead(TPort& port, TRPCDeviceRequest& request, TContinuousReadStatus value)
{
    std::string error;
    try {
        auto config = WbRegisters::GetRegisterConfig(WbRegisters::CONTINUOUS_READ_REGISTER_NAME);
        WriteModbusRegister(port, request, config, TRegisterValue(static_cast<uint16_t>(value)));
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
            auto width = config.Get16BitWidth();
            if (str.size() < width) {
                return false;
            }
            for (uint8_t i = 0; i < width; ++i) {
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
                                  Json::Value* data)
{
    auto device = dynamic_cast<TModbusDevice*>(request.Device.get());
    if (device == nullptr) {
        return;
    }
    auto status = device->GetContinuousReadStatus();
    if (status == TContinuousReadStatus::DISABLED) {
        return;
    }
    auto enabled = true;
    for (const auto& item: registerList) {
        try {
            if (item.CheckUnsupported && CheckUnsupportedValue(*item.Register->GetConfig(), item.Register->GetValue()))
            {
                if (enabled) {
                    SetContinuousRead(port, request, TContinuousReadStatus::DISABLED);
                    enabled = false;
                }
                try {
                    TRegisterValue value;
                    ReadModbusRegister(port, request, item.Register->GetConfig(), value);
                } catch (const Modbus::TModbusExceptionError& err) {
                    item.Register->SetSupported(false);
                    if (data != nullptr) {
                        (*data)[item.Id] = UNSUPPORTED_VALUE;
                    }
                }
            }
        } catch (const TRegisterValueException& e) {
        }
    }
    if (!enabled) {
        SetContinuousRead(port, request, status);
    }
}

Json::Value MakePortConfigsResponse(const THandlerConfig& config)
{
    Json::Value res(Json::arrayValue);
    for (const auto& portConfig: config.PortConfigs) {
        res.append(MakePortJson(*portConfig->Port, "address"));
    }
    return res;
}

Json::Value MakePortsListResponse(const THandlerConfig& config)
{
    Json::Value res;
    auto& ports = MakeArray("ports", res);
    for (const auto& portConfig: config.PortConfigs) {
        auto port = MakePortJson(*portConfig->Port, "ip");
        auto& devices = MakeArray("devices", port);
        for (const auto& device: portConfig->Devices) {
            devices.append(MakeDeviceJson(*device->Device));
        }
        ports.append(std::move(port));
    }
    return res;
}
