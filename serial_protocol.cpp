#include "serial_protocol.h"

TSerialProtocol::TSerialProtocol(PAbstractSerialPort port)
    : SerialPort(port) {}

TSerialProtocol::~TSerialProtocol() {}

void TSerialProtocol::EndPollCycle() {}

std::unordered_map<std::string, TSerialProtocolEntry>
    *TSerialProtocolFactory::Protocols = 0;

void TSerialProtocolFactory::RegisterProtocol(const std::string& name, TSerialProtocolMaker maker,
                                              const TRegisterTypes& register_types)
{
    if (!Protocols)
        Protocols = new std::unordered_map<std::string, TSerialProtocolEntry>();

    auto reg_map = std::make_shared<TRegisterTypeMap>();
    for (const auto& rt : register_types)
        reg_map->emplace(rt.Name, rt);

    Protocols->emplace(name, TSerialProtocolEntry(maker, reg_map));
}

const TSerialProtocolEntry& TSerialProtocolFactory::GetProtocolEntry(PDeviceConfig device_config)
{
    auto it = Protocols->find(device_config->Protocol);
    if (it == Protocols->end())
        throw TSerialProtocolException("unknown serial protocol");
    return it->second;
}

PSerialProtocol TSerialProtocolFactory::CreateProtocol(PDeviceConfig device_config, PAbstractSerialPort port)
{
    return GetProtocolEntry(device_config).Maker(device_config, port);
}

PRegisterTypeMap TSerialProtocolFactory::GetRegisterTypes(PDeviceConfig device_config)
{
    return GetProtocolEntry(device_config).RegisterTypes;
}
