#include <iostream>
#include <stdexcept>

#include "modbus_device.h"
#include "modbus_common.h"


const static std::chrono::milliseconds FrameTimeout(500);   // libmodbus default

REGISTER_BASIC_INT_PROTOCOL("modbus", TModbusDevice, TRegisterTypes({
            { Modbus::REG_COIL, "coil", "switch", U8 },
            { Modbus::REG_DISCRETE, "discrete", "switch", U8, true },
            { Modbus::REG_HOLDING, "holding", "text", U16 },
            { Modbus::REG_INPUT, "input", "text", U16, true }
        }));

TModbusDevice::TModbusDevice(PDeviceConfig config, PAbstractSerialPort port, PProtocol protocol)
    : TBasicProtocolSerialDevice<TBasicProtocol<TModbusDevice>>(config, port, protocol)
{}

std::list<PRegisterRange> TModbusDevice::SplitRegisterList(const std::list<PRegister> reg_list) const
{
    return Modbus::SplitRegisterList(reg_list, DeviceConfig(), Port()->Debug());
}

uint64_t TModbusDevice::ReadRegister(PRegister reg)
{
    throw TSerialDeviceException("modbus: single register reading is not supported");
}

void TModbusDevice::WriteRegister(PRegister reg, uint64_t value)
{
    int w = reg->Width();

    if (Port()->Debug())
        std::cerr << "modbus: write " << w << " " << reg->TypeName << "(s) @ " << reg->Address <<
            " of device " << reg->Device()->ToString() << std::endl;
    std::string exception_message;
    try {
        {   // Send request
            ModbusRTU::TWriteRequest request;
            ModbusRTU::ComposeWriteRequest(request, reg, SlaveId, value);
            Port()->WriteBytes(request.data(), request.size());
        }

        {   // Receive response
            ModbusRTU::TWriteResponse response;
            if (Port()->ReadFrame(response.data(), response.size(), FrameTimeout, ModbusRTU::ExpectNBytes(response.size())) > 0) {
                ModbusRTU::ParseWriteResponse(response);
                return;
            }
        }
    } catch (TSerialDeviceTransientErrorException& e) {
        exception_message = ": ";
        exception_message += e.what();
    }

    throw TSerialDeviceTransientErrorException(
        "failed to write " + reg->TypeName +
        " @ " + std::to_string(reg->Address) + exception_message);
}

void TModbusDevice::ReadRegisterRange(PRegisterRange range)
{
    auto modbus_range = std::dynamic_pointer_cast<Modbus::TModbusRegisterRange>(range);
    if (!modbus_range) {
        throw std::runtime_error("modbus range expected");
    }

    if (Port()->Debug())
        std::cerr << "modbus: read " << modbus_range->GetCount() << " " <<
            modbus_range->TypeName() << "(s) @ " << modbus_range->GetStart() <<
            " of device " << modbus_range->Device()->ToString() << std::endl;

    std::string exception_message;
    try {
        {   // Send request
            ModbusRTU::TReadRequest request;
            ModbusRTU::ComposeReadRequest(request, modbus_range, SlaveId);
            Port()->WriteBytes(request.data(), request.size());
        }

        {   // Receive response
            auto byte_count = ModbusRTU::InferReadResponseSize(modbus_range);
            std::vector<uint8_t> response(byte_count);

            auto rc = Port()->ReadFrame(response.data(), response.size(), FrameTimeout, ModbusRTU::ExpectNBytes(response.size()));
            if (rc > 0) {
                ModbusRTU::ParseReadResponse(response, modbus_range);
                modbus_range->SetError(false);
                return;
            }
        }
    } catch (TSerialDeviceTransientErrorException& e) {
        exception_message = e.what();
    }

    modbus_range->SetError(true);
    std::ios::fmtflags f(std::cerr.flags());
    std::cerr << "TModbusDevice::ReadRegisterRange(): failed to read " << modbus_range->GetCount() << " " <<
        modbus_range->TypeName() << "(s) @ " << modbus_range->GetStart() <<
        " of device " << modbus_range->Device()->ToString();
    if (!exception_message.empty()) {
        std::cerr << ": " << exception_message;
    }
    std::cerr << std::endl;
    std::cerr.flags(f);
}
