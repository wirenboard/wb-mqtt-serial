#include <iostream>
#include <stdexcept>

#include "modbus_io_device.h"
#include "modbus_common.h"


REGISTER_BASIC_PROTOCOL("modbus_io", TModbusIODevice, TAggregatedSlaveId, TRegisterTypes({
            { Modbus::REG_COIL, "coil", "switch", U8 },
            { Modbus::REG_DISCRETE, "discrete", "switch", U8, true },
            { Modbus::REG_HOLDING, "holding", "text", U16 },
            { Modbus::REG_INPUT, "input", "text", U16, true }
        }));

TModbusIODevice::TModbusIODevice(PDeviceConfig config, PAbstractSerialPort port, PProtocol protocol)
    : TBasicProtocolSerialDevice<TBasicProtocol<TModbusIODevice, TAggregatedSlaveId>>(config, port, protocol)
{
    Shift = SlaveId.Secondary * DeviceConfig()->Stride + DeviceConfig()->Shift;
}

std::list<PRegisterRange> TModbusIODevice::SplitRegisterList(const std::list<PRegister> reg_list) const
{
    return Modbus::SplitRegisterList(reg_list, DeviceConfig(), Port()->Debug());
}

uint64_t TModbusIODevice::ReadRegister(PRegister reg)
{
    throw TSerialDeviceException("modbus extension module: single register reading is not supported");
}

void TModbusIODevice::WriteRegister(PRegister reg, uint64_t value)
{
    int w = reg->Width();

    if (Port()->Debug())
        std::cerr << "modbus extension module: write " << w << " " << reg->TypeName << "(s) @ " << reg->Address <<
            " of device " << reg->Device()->ToString() << std::endl;
    std::string exception_message;
    try {
        {   // Send request
            ModbusRTU::TWriteRequest request;
            ModbusRTU::ComposeWriteRequest(request, reg, SlaveId.Primary, value, GetShift(reg));
            Port()->WriteBytes(request.data(), request.size());
        }

        {   // Receive response
            ModbusRTU::TWriteResponse response;
            if (Port()->ReadFrame(response.data(), response.size(), ModbusRTU::FrameTimeout, ModbusRTU::ExpectNBytes(response.size())) > 0) {
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

void TModbusIODevice::ReadRegisterRange(PRegisterRange range)
{
    auto modbus_range = std::dynamic_pointer_cast<Modbus::TModbusRegisterRange>(range);
    if (!modbus_range) {
        throw std::runtime_error("modbus range expected");
    }

    if (Port()->Debug())
        std::cerr << "modbus extension module: read " << modbus_range->GetCount() << " " <<
            modbus_range->TypeName() << "(s) @ " << modbus_range->GetStart() <<
            " of device " << modbus_range->Device()->ToString() << std::endl;

    std::string exception_message;
    try {
        {   // Send request
            ModbusRTU::TReadRequest request;
            ModbusRTU::ComposeReadRequest(request, modbus_range, SlaveId.Primary, GetShift(range));
            Port()->WriteBytes(request.data(), request.size());
        }

        {   // Receive response
            ModbusRTU::TReadResponse response(ModbusRTU::InferReadResponseSize(modbus_range));
            auto rc = Port()->ReadFrame(response.data(), response.size(), ModbusRTU::FrameTimeout, ModbusRTU::ExpectNBytes(response.size()));
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
    std::cerr << "TModbusIODevice::ReadRegisterRange(): failed to read " << modbus_range->GetCount() << " " <<
        modbus_range->TypeName() << "(s) @ " << modbus_range->GetStart() <<
        " of device " << modbus_range->Device()->ToString();
    if (!exception_message.empty()) {
        std::cerr << ": " << exception_message;
    }
    std::cerr << std::endl;
    std::cerr.flags(f);
}

int TModbusIODevice::GetShift(PRegister reg) const
{
    return reg->IsConfigFlag ? (SlaveId.Secondary - 1 + DeviceConfig()->Shift) : Shift;
}

int TModbusIODevice::GetShift(PRegisterRange range) const
{
    return Shift;
}
