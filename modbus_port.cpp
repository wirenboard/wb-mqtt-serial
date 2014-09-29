#include <algorithm>
#include <sstream>

#include "common/utils.h"
#include "modbus_port.h"

TModbusPort::TModbusPort(PMQTTClientBase mqtt_client, PPortConfig port_config, PModbusConnector connector)
    : MQTTClient(mqtt_client),
      Config(port_config),
      ModbusClient(new TModbusClient(Config->ConnSettings, connector))
{
    ModbusClient->SetCallback([this](const TModbusRegister& reg) {
            OnModbusValueChange(reg);
        });
    for (auto device_config: Config->DeviceConfigs) {
        for (auto channel: device_config->ModbusChannels) {
            for (auto reg: channel->Registers) {
                RegisterToChannelMap[reg] = channel;
                NameToChannelMap[device_config->Id + "/" + channel->Name] = channel;
                ModbusClient->AddRegister(reg);
            }
        }
    }
    ModbusClient->SetPollInterval(Config->PollInterval);
    ModbusClient->SetModbusDebug(Config->Debug);
};

void TModbusPort::PubSubSetup()
{
    for (auto device_config : Config->DeviceConfigs) {
        /* Only attempt to Subscribe on a successful connect. */
        std::string id = device_config->Id.empty() ? MQTTClient->Id() : device_config->Id;
        std::string prefix = std::string("/devices/") + id + "/";
        // Meta
        MQTTClient->Publish(NULL, prefix + "meta/name", device_config->Name, 0, true);
        for (const auto& channel : device_config->ModbusChannels) {
            std::string control_prefix = prefix + "controls/" + channel->Name;
            MQTTClient->Publish(NULL, control_prefix + "/meta/type", channel->Type, 0, true);
            if (channel->ReadOnly)
                MQTTClient->Publish(NULL, control_prefix + "/meta/readonly", "1", 0, true);
            if (channel->Type == "range")
                MQTTClient->Publish(NULL, control_prefix + "/meta/max",
                                 channel->Max < 0 ? "65535" : std::to_string(channel->Max),
                                 0, true);
            MQTTClient->Publish(NULL, control_prefix + "/meta/order",
                             std::to_string(channel->Order), 0, true);
            MQTTClient->Subscribe(NULL, control_prefix + "/on");
        }
    }

//~ /devices/293723-demo/controls/Demo-Switch 0
//~ /devices/293723-demo/controls/Demo-Switch/on 1
//~ /devices/293723-demo/controls/Demo-Switch/meta/type switch
}

bool TModbusPort::HandleMessage(const std::string& topic, const std::string& payload)
{
    const std::vector<std::string>& tokens = StringSplit(topic, '/');
    if ((tokens.size() != 6) ||
        (tokens[0] != "") || (tokens[1] != "devices") ||
        (tokens[3] != "controls") || (tokens[5] != "on"))
        return false;

    std::string device_id = tokens[2];
    std::string channel_name = tokens[4];
    const auto& dev_config_it =
        std::find_if(Config->DeviceConfigs.begin(),
                     Config->DeviceConfigs.end(),
                     [device_id](PDeviceConfig c) {
                         return c->Id == device_id;
                     });

    if (dev_config_it == Config->DeviceConfigs.end())
        return false;

    const auto& it = NameToChannelMap.find((*dev_config_it)->Id + "/" + channel_name);
    if (it == NameToChannelMap.end())
        return false;

    std::vector<std::string> payload_items = StringSplit(payload, ';');

    if (payload_items.size() != it->second->Registers.size()) {
        std::cerr << "warning: invalid payload for topic '" << topic <<
            "': '" << payload << "'" << std::endl;
        // here 'true' means that the message doesn't need to be passed
        // to other port handlers
        return true;
    }

    for (size_t i = 0; i < it->second->Registers.size(); ++i) {
        const TModbusRegister& reg = it->second->Registers[i];
        if (Config->Debug)
            std::cerr << "setting modbus register: " << reg.ToString() << " <- " <<
                payload_items[i] << std::endl;
        ModbusClient->SetTextValue(reg, payload_items[i]);
    }
    MQTTClient->Publish(NULL, GetChannelTopic(*it->second), payload, 0, true);
    return true;
}

std::string TModbusPort::GetChannelTopic(const TModbusChannel& channel)
{
    std::string controls_prefix = std::string("/devices/") + channel.DeviceId + "/controls/";
    return (controls_prefix + channel.Name);
}

void TModbusPort::OnModbusValueChange(const TModbusRegister& reg)
{
    if (Config->Debug)
        std::cerr << "modbus value change: " << reg.ToString() << " <- " <<
            ModbusClient->GetTextValue(reg) << std::endl;

    auto it = RegisterToChannelMap.find(reg);
    if (it == RegisterToChannelMap.end()) {
        std::cerr << "warning: unexpected register from modbus" << std::endl;
        return;
    }

    std::string payload;
    if (it->second->OnValue >= 0) {
        payload = ModbusClient->GetRawValue(reg) == it->second->OnValue ? "1" : "0";
        if (Config->Debug)
            std::cerr << "OnValue: " << it->second->OnValue << "; payload: " <<
                payload << std::endl;
    } else {
        std::stringstream s;
        for (size_t i = 0; i < it->second->Registers.size(); ++i) {
            if (i)
                s << ";";
            s << ModbusClient->GetTextValue(it->second->Registers[i]);
        }
        payload = s.str();
    }
    //     payload = std::to_string(int_value);
    // else {
    //     double value = int_value * it->second.Scale;
    //     if (Config.Debug)
    //         std::cerr << "after scaling: " << value << std::endl;
    //     payload = std::to_string(value);
    // }
    // Publish current value (make retained)
    if (Config->Debug)
        std::cerr << "channel " << it->second->Name << " device id: " <<
            it->second->DeviceId << " -- topic: " << GetChannelTopic(*it->second) <<
            " <-- " << payload << std::endl;
    MQTTClient->Publish(NULL, GetChannelTopic(*it->second), payload, 0, true);
}

void TModbusPort::Cycle()
{
    try {
        ModbusClient->Cycle();
    } catch (TModbusException& e) {
        std::cerr << "FATAL: " << e.what() << ". Stopping event loops." << std::endl;
        exit(1);
    }
}

bool TModbusPort::WriteInitValues()
{
    bool did_write = false;
    for (const auto& device_config : Config->DeviceConfigs) {
        for (const auto& setup_item : device_config->SetupItems) {
            if (Config->Debug)
                std::cerr << "Init: " << setup_item->Name << ": holding register " <<
                    setup_item->Address << " <-- " << setup_item->Value << std::endl;
            ModbusClient->WriteHoldingRegister(device_config->SlaveId,
                                         setup_item->Address,
                                         setup_item->Value);
            did_write = true;
        }
    }

    return did_write;
}
