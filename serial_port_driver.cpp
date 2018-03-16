#include "serial_port_driver.h"
#include "serial_port.h"
#include "tcp_port.h"
#include "virtual_register.h"
#include "virtual_register_set.h"

#include <wbmqtt/utils.h>

#include <algorithm>
#include <sstream>
#include <iostream>
#include <cassert>


TSerialPortDriver::TSerialPortDriver(PMQTTClientBase mqtt_client, PPortConfig port_config,
                                     PPort port_override)
    : MQTTClient(mqtt_client)
    , Config(port_config)
{
    if (port_override) {
        Port = port_override;
    } else if (auto serial_port_settings = std::dynamic_pointer_cast<TSerialPortSettings>(port_config->ConnSettings)) {
        Port = std::make_shared<TSerialPort>(serial_port_settings);
    } else if (auto tcp_port_settings = std::dynamic_pointer_cast<TTcpPortSettings>(port_config->ConnSettings)) {
        Port = std::make_shared<TTcpPort>(tcp_port_settings);
    } else {
        throw TSerialDeviceException("invalid connection settings");
    }

    SerialClient = std::make_shared<TSerialClient>(Port);

    SerialClient->SetDebug(Config->Debug);
    SerialClient->SetReadCallback([this](const PVirtualRegister & reg) {
        OnValueRead(reg);
    });
    SerialClient->SetErrorCallback([this](const PVirtualRegister & reg) {
        UpdateError(reg);
    });

    if (Config->Debug)
        std::cerr << "Setting up devices at " << port_config->ConnSettings->ToString() << std::endl;

    for (auto& device_config: Config->DeviceConfigs) {
        auto device = SerialClient->CreateDevice(device_config);
        Devices.push_back(device);

        for (auto& channel_config: device_config->DeviceChannelConfigs) {

            std::vector<PVirtualRegister> registers;
            registers.reserve(channel_config->RegisterConfigs.size());

            for (const auto &reg_config: channel_config->RegisterConfigs) {
                registers.push_back(TVirtualRegister::Create(reg_config, device));
            }

            PAbstractVirtualRegister abstractRegister;
            if (registers.size() == 1) {
                abstractRegister = registers[0];
            } else {
                auto virtualRegisterSet = std::make_shared<TVirtualRegisterSet>(registers);

                for (auto& reg: registers) {
                    reg->AssociateWithSet(virtualRegisterSet);
                }

                abstractRegister = virtualRegisterSet;
            }

            auto channel = std::make_shared<TDeviceChannel>(device, channel_config, abstractRegister);
            NameToChannelMap[device_config->Id + "/" + channel->Name] = channel;

            ChannelRegisterMap[channel] = abstractRegister;
            RegisterToChannelMap[abstractRegister] = channel;

            for (auto& reg: registers) {
                SerialClient->AddRegister(reg);
            }
        }
    }
}

TSerialPortDriver::~TSerialPortDriver()
{
}

void TSerialPortDriver::PubSubSetup()
{
    for (auto device_config : Config->DeviceConfigs) {
        /* Only attempt to Subscribe on a successful connect. */
        std::string id = device_config->Id.empty() ? MQTTClient->Id() : device_config->Id;
        std::string prefix = std::string("/devices/") + id + "/";
        // Meta
        MQTTClient->Publish(NULL, prefix + "meta/name", device_config->Name, 0, true);
        for (const auto& channel : device_config->DeviceChannelConfigs) {
            std::string control_prefix = prefix + "controls/" + channel->Name;
            MQTTClient->Publish(NULL, control_prefix + "/meta/type", channel->Type, 0, true);
            if (channel->ReadOnly)
                MQTTClient->Publish(NULL, control_prefix + "/meta/readonly", "1", 0, true);
            if (channel->Type == "range" || channel->Type == "dimmer")
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

bool TSerialPortDriver::HandleMessage(const std::string& topic, const std::string& payload)
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

    const auto& reg = ChannelRegisterMap[it->second];

    try {
        reg->SetTextValue(payload);
    } catch (std::exception& err) {
        std::cerr << "warning: invalid payload for topic '" << topic <<
            "': '" << payload << "' : " << err.what()  << std::endl;

        return true;
    }

    MQTTClient->Publish(NULL, GetChannelTopic(*it->second), payload, 0, true);
    return true;
}

std::string TSerialPortDriver::GetChannelTopic(const TDeviceChannelConfig& channel)
{
    std::string controls_prefix = std::string("/devices/") + channel.DeviceId + "/controls/";
    return (controls_prefix + channel.Name);
}

bool TSerialPortDriver::NeedToPublish(const PAbstractVirtualRegister & reg)
{
    // max_unchanged_interval = 0: always update, don't track last change time
    if (!Config->MaxUnchangedInterval)
        return true;

    bool valueChanged = reg->IsChanged(EPublishData::Value);

    // max_unchanged_interval < 0: update if changed, don't track last change time
    if (Config->MaxUnchangedInterval < 0)
        return valueChanged;

    // max_unchanged_interval > 0: update if changed or the time interval is exceeded
    auto now = Port->CurrentTime();
    if (valueChanged) {
        RegLastPublishTimeMap[reg] = now;
        return true;
    }

    auto it = RegLastPublishTimeMap.find(reg);
    if (it == RegLastPublishTimeMap.end()) {
        // too strange - unchanged, but not tracked yet, but ok, let's publish it
        RegLastPublishTimeMap[reg] = now;
        return true;
    }

    std::chrono::duration<double> elapsed = now - it->second;
    if (elapsed.count() < Config->MaxUnchangedInterval)
        return false; // still fresh

    // unchanged interval elapsed
    it->second = now;
    return true;
}

void TSerialPortDriver::OnValueRead(const PVirtualRegister & reg)
{
    const auto & abstractRegister = reg->GetTopLevel();

    auto it = RegisterToChannelMap.find(abstractRegister);
    if (it == RegisterToChannelMap.end()) {
        std::cerr << "warning: got unexpected register from serial client" << std::endl;
        return;
    }

    bool valueChanged = abstractRegister->IsChanged(EPublishData::Value);
    bool valueIsRead = abstractRegister->GetValueIsRead();

    if (!valueIsRead) {
        return;
    }

    if (Config->Debug && valueChanged)
        std::cerr << "register value change: " << reg->ToString() << " <- " <<
            reg->GetTextValue() << std::endl;

    if (!NeedToPublish(abstractRegister)) {
        return;
    }

    const auto & payload = abstractRegister->GetTextValue();

    if (Config->Debug && !reg->OnValue.empty())
        std::cerr << "OnValue: " << reg->OnValue << "; payload: " << payload << std::endl;

    // Publish current value (make retained)
    if (Config->Debug)
        std::cerr << "channel " << it->second->Name << " device id: " <<
            it->second->DeviceId << " -- topic: " << GetChannelTopic(*it->second) <<
            " <-- " << payload << std::endl;

    MQTTClient->Publish(NULL, GetChannelTopic(*it->second), payload, 0, true);
    abstractRegister->ResetChanged(EPublishData::Value);
}

void TSerialPortDriver::UpdateError(const PVirtualRegister & reg)
{
    const auto & abstractRegister = reg->GetTopLevel();

    auto it = RegisterToChannelMap.find(abstractRegister);
    if (it == RegisterToChannelMap.end()) {
        std::cerr << "warning: got unexpected register from serial client" << std::endl;
        return;
    }

    auto errorState = abstractRegister->GetErrorState();

    bool readError  = Has(errorState, EErrorState::ReadError),
         writeError = Has(errorState, EErrorState::WriteError);

    std::string errorStr   = readError ? (writeError ? "rw" : "r") : (writeError ? "w" : ""),
                errorTopic = GetChannelTopic(*it->second) + "/meta/error";

    auto errMapIt = PublishedErrorMap.find(errorTopic);
    if (errMapIt == PublishedErrorMap.end() || errMapIt->second != errorStr) {
        PublishedErrorMap[errorTopic] = errorStr;
        MQTTClient->Publish(NULL, errorTopic, errorStr.c_str(), 0, true);
    }

    abstractRegister->ResetChanged(EPublishData::Error);
}

void TSerialPortDriver::Cycle()
{
    try {
        SerialClient->Cycle();
    } catch (TSerialDeviceException& e) {
        std::cerr << "FATAL: " << e.what() << ". Stopping event loops." << std::endl;
        exit(1);
    }
}

bool TSerialPortDriver::WriteInitValues()
{
    bool did_write = false;
    for (auto& device : Devices) {
    	did_write |= SerialClient->WriteSetupRegisters(device);
    }

    return did_write;
}
