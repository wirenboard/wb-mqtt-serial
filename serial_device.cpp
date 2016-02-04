#include "serial_device.h"

TSerialDevice::TSerialDevice(PDeviceConfig config, PAbstractSerialPort port)
    : DelayUsec(config->DelayUSec), SerialPort(port) {}

TSerialDevice::~TSerialDevice() {}

void TSerialDevice::Prepare()
{
    SerialPort->USleep(DelayUsec);
}

void TSerialDevice::EndPollCycle() {}

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
