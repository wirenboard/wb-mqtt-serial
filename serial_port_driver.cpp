#include <algorithm>
#include <sstream>
#include <iostream>

#include <wbmqtt/utils.h>
#include "serial_port_driver.h"
#include "serial_device.h"
#include "serial_client.h"

TSerialPortDriver::TSerialPortDriver(PMQTTClientBase mqtt_client, PPortConfig port_config,
                                     PAbstractSerialPort port_override)
    : MQTTClient(mqtt_client),
      Config(port_config),
      Port(port_override ? port_override : std::make_shared<TSerialPort>(Config->ConnSettings)),
      SerialClient(new TSerialClient(Port))
{
    SerialClient->SetDebug(Config->Debug);
    SerialClient->SetReadCallback([this](PRegister reg, bool changed) {
            OnValueRead(reg, changed);
        });
    SerialClient->SetErrorCallback(
        [this](PRegister reg, TRegisterHandler::TErrorState state) {
            UpdateError(reg, state);
        });

    if (Config->Debug)
        std::cerr << "Setting up devices at " << port_config->ConnSettings.Device << std::endl;

    for (auto& device_config: Config->DeviceConfigs) {
        auto device = SerialClient->CreateDevice(device_config);
        Devices.push_back(device);

        // init channels' registers
        for (auto& channel_config: device_config->DeviceChannelConfigs) {
            
            auto channel = std::make_shared<TDeviceChannel>(device, channel_config);
            NameToChannelMap[device_config->Id + "/" + channel->Name] = channel;
            
            ChannelRegistersMap[channel] = std::vector<PRegister>();
            
            for (auto& reg: channel->Registers) {
                RegisterToChannelMap[reg] = channel;
                SerialClient->AddRegister(reg);
                ChannelRegistersMap[channel].push_back(reg);
            }
        }

        // init setup items' registers
        for (auto& setup_item_config: device_config->SetupItemConfigs) {
            SetupItems.push_back(std::make_shared<TDeviceSetupItem>(device, setup_item_config));
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

    const auto& reglist = ChannelRegistersMap[it->second];

    std::vector<std::string> payload_items = StringSplit(payload, ';');

    if (payload_items.size() != reglist.size()) {
        std::cerr << "warning: invalid payload for topic '" << topic <<
            "': '" << payload << "'" << std::endl;
        // here 'true' means that the message doesn't need to be passed
        // to other port handlers
        return true;
    }

    for (size_t i = 0; i < reglist.size(); ++i) {
        PRegister reg = reglist[i];
        if (Config->Debug)
            std::cerr << "setting device register: " << reg->ToString() << " <- " <<
                payload_items[i] << std::endl;

        try {
			SerialClient->SetTextValue(reg,
				it->second->OnValue.empty() ? payload_items[i]
                                       : (payload_items[i] == "1" ?  it->second->OnValue : "0")
			);

		} catch (std::exception& err) {
			std::cerr << "warning: invalid payload for topic '" << topic <<
				"': '" << payload << "' : " << err.what()  << std::endl;

			return true;
		}

    }

    MQTTClient->Publish(NULL, GetChannelTopic(*it->second), payload, 0, true);
    return true;
}

std::string TSerialPortDriver::GetChannelTopic(const TDeviceChannelConfig& channel)
{
    std::string controls_prefix = std::string("/devices/") + channel.DeviceId + "/controls/";
    return (controls_prefix + channel.Name);
}

bool TSerialPortDriver::NeedToPublish(PRegister reg, bool changed)
{
    // max_unchanged_interval = 0: always update, don't track last change time
    if (!Config->MaxUnchangedInterval)
        return true;

    // max_unchanged_interval < 0: update if changed, don't track last change time
    if (Config->MaxUnchangedInterval < 0)
        return changed;

    // max_unchanged_interval > 0: update if changed or the time interval is exceeded
    auto now = Port->CurrentTime();
    if (changed) {
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

void TSerialPortDriver::OnValueRead(PRegister reg, bool changed)
{
    auto it = RegisterToChannelMap.find(reg);
    if (it == RegisterToChannelMap.end()) {
        std::cerr << "warning: got unexpected register from serial client" << std::endl;
        return;
    }

    const auto &reglist = it->second->Registers;

    if (Config->Debug && changed)
        std::cerr << "register value change: " << reg->ToString() << " <- " <<
            SerialClient->GetTextValue(reg) << std::endl;

    if (!NeedToPublish(reg, changed))
        return;

    std::string payload;
    if (!it->second->OnValue.empty()) {
        payload = SerialClient->GetTextValue(reg) == it->second->OnValue ? "1" : "0";
        if (Config->Debug)
            std::cerr << "OnValue: " << it->second->OnValue << "; payload: " <<
                payload << std::endl;
    } else {
        std::stringstream s;
        for (size_t i = 0; i < reglist.size(); ++i) {
            PRegister reg = reglist[i];
            // avoid publishing incomplete value
            if (!SerialClient->DidRead(reg))
                return;
            if (i)
                s << ";";
            s << SerialClient->GetTextValue(reg);
        }
        payload = s.str();
        // check if there any errors in this Channel
    }

    // Publish current value (make retained)
    if (Config->Debug)
        std::cerr << "channel " << it->second->Name << " device id: " <<
            it->second->DeviceId << " -- topic: " << GetChannelTopic(*it->second) <<
            " <-- " << payload << std::endl;

    MQTTClient->Publish(NULL, GetChannelTopic(*it->second), payload, 0, true);
}

TRegisterHandler::TErrorState TSerialPortDriver::RegErrorState(PRegister reg)
{
    auto it = RegErrorStateMap.find(reg);
    if (it == RegErrorStateMap.end())
        return TRegisterHandler::UnknownErrorState;
    return it->second;
}

void TSerialPortDriver::UpdateError(PRegister reg, TRegisterHandler::TErrorState errorState)
{
    auto it = RegisterToChannelMap.find(reg);
    if (it == RegisterToChannelMap.end()) {
        std::cerr << "warning: got unexpected register from serial client" << std::endl;
        return;
    }
    const auto &reglist = it->second->Registers;

    RegErrorStateMap[reg] = errorState;
    bool readError = false, writeError = false;
    for (auto r: reglist) {
        switch (RegErrorState(r)) {
        case TRegisterHandler::WriteError:
            writeError = true;
            break;
        case TRegisterHandler::ReadError:
            readError = true;
            break;
        case TRegisterHandler::ReadWriteError:
            readError = writeError = true;
            break;
        default:
            ; // make compiler happy
        }
    }
    std::string errorStr = readError ? (writeError ? "rw" : "r") : (writeError ? "w" : ""),
        errorTopic = GetChannelTopic(*it->second) + "/meta/error";
    auto errMapIt = PublishedErrorMap.find(errorTopic);
    if (errMapIt == PublishedErrorMap.end() || errMapIt->second != errorStr) {
        PublishedErrorMap[errorTopic] = errorStr;
        MQTTClient->Publish(NULL, errorTopic, errorStr.c_str(), 0, true);
    }
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
    for (const auto& setup_item : SetupItems) {
        try {
            if (Config->Debug)
                std::cerr << "Init: " << setup_item->Name << ": setup register " <<
                    setup_item->Register->ToString() << " <-- " << setup_item->Value << std::endl;
            SerialClient->WriteSetupRegister(setup_item->Register,
                                             setup_item->Value);
            did_write = true;
        } catch (const TSerialDeviceException& e) {
            std::cerr << "WARNING: device '" << setup_item->Device->ToString() <<
                "' register '" << setup_item->Register->ToString() << 
                "' setup failed: " << e.what() << std::endl;
        }
    }

    return did_write;
}
