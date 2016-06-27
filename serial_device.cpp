#include <iostream>
#include "serial_device.h"

TSerialDevice::TSerialDevice(PDeviceConfig config, PAbstractSerialPort port)
    : Delay(config->Delay), SerialPort(port) {}

TSerialDevice::~TSerialDevice() {}

std::list<PRegisterRange> TSerialDevice::SplitRegisterList(const std::list<PRegister> reg_list) const
{
    std::list<PRegisterRange> r;
    for (auto reg: reg_list)
        r.push_back(std::make_shared<TSimpleRegisterRange>(reg));
    return r;
}

void TSerialDevice::Prepare()
{
    SerialPort->Sleep(Delay);
}

void TSerialDevice::EndPollCycle() {}

void TSerialDevice::ReadRegisterRange(PRegisterRange range)
{
    PSimpleRegisterRange simple_range = std::dynamic_pointer_cast<TSimpleRegisterRange>(range);
    if (!simple_range)
        throw std::runtime_error("simple range expected");
    simple_range->Reset();
    for (auto reg: simple_range->RegisterList()) {
        try {
            simple_range->SetValue(reg, ReadRegister(reg));
        } catch (const TSerialDeviceTransientErrorException& e) {
            simple_range->SetError(reg);
            std::ios::fmtflags f(std::cerr.flags());
            std::cerr << "TSerialDevice::ReadRegisterRange(): warning: " << e.what() << " [slave_id is "
                      << reg->Slave->Id << "(0x" << std::hex << reg->Slave->Id << ")]" << std::endl;
            std::cerr.flags(f);
        }
    }
}

std::unordered_map<std::string, TSerialProtocolEntry>
    *TSerialDeviceFactory::Protocols = 0;

void TSerialDeviceFactory::RegisterProtocol(const std::string& name, TSerialDeviceMaker maker,
                                            const TRegisterTypes& register_types)
{
    if (!Protocols)
        Protocols = new std::unordered_map<std::string, TSerialProtocolEntry>();

    auto reg_map = std::make_shared<TRegisterTypeMap>();
    for (const auto& rt : register_types)
        reg_map->insert(std::make_pair(rt.Name, rt));

    Protocols->insert(std::make_pair(name, TSerialProtocolEntry(maker, reg_map)));
}

const TSerialProtocolEntry& TSerialDeviceFactory::GetProtocolEntry(PDeviceConfig device_config)
{
    auto it = Protocols->find(device_config->Protocol);
    if (it == Protocols->end())
        throw TSerialDeviceException("unknown serial protocol");
    return it->second;
}

PSerialDevice TSerialDeviceFactory::CreateDevice(PDeviceConfig device_config, PAbstractSerialPort port)
{
    return GetProtocolEntry(device_config).Maker(device_config, port);
}

PRegisterTypeMap TSerialDeviceFactory::GetRegisterTypes(PDeviceConfig device_config)
{
    return GetProtocolEntry(device_config).RegisterTypes;
}
