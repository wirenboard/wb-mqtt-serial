#include "serial_port_driver.h"
#include "log.h"

#include <wblib/wbmqtt.h>

#include <algorithm>
#include <sstream>
#include <iostream>
#include <cassert>

#include "serial_port.h"
#include "tcp_port.h"

using namespace std;
using namespace WBMQTT;

#define LOG(logger) ::logger.Log() << "[serial port driver] "

TSerialPortDriver::TSerialPortDriver(WBMQTT::PDeviceDriver mqttDriver, PPortConfig portConfig)
    : MqttDriver(mqttDriver),
      Config(portConfig)
{
    SerialClient = PSerialClient(new TSerialClient(Config->Port));
}

TSerialPortDriver::~TSerialPortDriver()
{}

void TSerialPortDriver::SetUpDevices()
{
    SerialClient->SetReadCallback([this](PRegister reg, bool changed) {
        OnValueRead(reg, changed);
    });
    SerialClient->SetErrorCallback([this](PRegister reg, TRegisterHandler::TErrorState state) {
        UpdateError(reg, state);
    });

    LOG(Debug) << "setting up devices at " << Config->Port->GetDescription();

    try {
        auto tx = MqttDriver->BeginTx();
        vector<TFuture<PControl>> futureControls;

        for (auto & deviceConfig: Config->DeviceConfigs) {
            auto device = SerialClient->CreateDevice(deviceConfig);
            auto mqttDevice = tx->CreateDevice(From(device)).GetValue();
            assert(mqttDevice);
            Devices.push_back(device);
            // init channels' registers
            for (auto & channelConfig: deviceConfig->DeviceChannelConfigs) {

                auto channel = std::make_shared<TDeviceChannel>(device, channelConfig);
                futureControls.push_back(mqttDevice->CreateControl(tx, From(channel)));

                for (auto & reg: channel->Registers) {
                    RegisterToChannelStateMap.emplace(reg, TDeviceChannelState{channel, TRegisterHandler::UnknownErrorState});
                    SerialClient->AddRegister(reg);
                }
            }
        }

        for (auto & futureControl: futureControls) {
            futureControl.GetValue();   // wait for control creation, receive exceptions if any
        }
    } catch (const exception & e) {
        LOG(Error) << "unable to create device or control: '" << e.what() << "' Cleaning.";
        ClearDevices();
        throw;
    } catch (...) {
        LOG(Error) << "unable to create device or control. Cleaning.";
        ClearDevices();
        throw;
    }
}

void TSerialPortDriver::HandleControlOnValueEvent(const WBMQTT::TControlOnValueEvent & event)
{
    const auto & value = event.RawValue;
    const auto & linkData = event.Control->GetUserData().As<TControlLinkData>();

    const auto & portDriver = linkData.PortDriver.lock();
    const auto & channel = linkData.DeviceChannel.lock();

    if (!portDriver || !channel) {
        if (!portDriver) {
            LOG(Error) << "event for non existent port driver from control '" << event.Control->GetDevice()->GetId() << "/" << event.Control->GetId() << "' value: '" << value << "'";
        }

        if (!channel) {
            LOG(Error) << "event for non existent device channel from control '" << event.Control->GetDevice()->GetId() << "/" << event.Control->GetId() << "' value: '" << value << "'";
        }

        return;
    }

    portDriver->SetValueToChannel(channel, value);
}

void TSerialPortDriver::SetValueToChannel(const PDeviceChannel & channel, const string & value)
{
    const auto & registers = channel->Registers;

    std::vector<std::string> valueItems = StringSplit(value, ';');

    if (valueItems.size() != registers.size()) {
        LOG(Warn) << "invalid value for " << channel->Describe() << ": '" << value << "'";
        return;
    }

    for (size_t i = 0; i < registers.size(); ++i) {
        PRegister reg = registers[i];
        LOG(Debug) << "setting device register: " << reg->ToString() << " <- " << valueItems[i];

        try {
            SerialClient->SetTextValue(reg,
                channel->OnValue.empty() ? valueItems[i]
                                       : (valueItems[i] == "1" ?  channel->OnValue : "0")
            );

        } catch (std::exception& err) {
            LOG(Warn) << "invalid value for " << channel->Describe() << ": '" << value << "' : " << err.what();
            return;
        }
    }
}

void TSerialPortDriver::OnValueRead(PRegister reg, bool changed)
{
    auto it = RegisterToChannelStateMap.find(reg);
    if (it == RegisterToChannelStateMap.end()) {
        LOG(Warn) << "got unexpected register from serial client";
        return;
    }
    const auto & channel = it->second.Channel;
    const auto & registers = channel->Registers;

    if (changed)
        LOG(Debug) << "register value change: " << reg->ToString() << " <- " << SerialClient->GetTextValue(reg);

    std::string value;
    if (!channel->OnValue.empty()) {
        value = SerialClient->GetTextValue(reg) == channel->OnValue ? "1" : "0";
        LOG(Debug) << "OnValue: " << channel->OnValue << "; value: " << value;
    } else {
        std::stringstream s;
        for (size_t i = 0; i < registers.size(); ++i) {
            PRegister reg = registers[i];
            // avoid publishing incomplete value
            if (!SerialClient->DidRead(reg))
                return;
            if (i)
                s << ";";
            s << SerialClient->GetTextValue(reg);
        }
        value = s.str();
        // check if there any errors in this Channel
    }

    // Publish current value (make retained)
    LOG(Debug) << channel->Describe() << " <-- " << value;

    MqttDriver->AccessAsync([=](const PDriverTx & tx){
        tx->GetDevice(channel->DeviceId)->GetControl(channel->Name)->SetRawValue(tx, value);
    });
}

TRegisterHandler::TErrorState TSerialPortDriver::RegErrorState(PRegister reg)
{
    auto it = RegisterToChannelStateMap.find(reg);
    if (it == RegisterToChannelStateMap.end())
        return TRegisterHandler::UnknownErrorState;
    return it->second.ErrorState;
}

void TSerialPortDriver::UpdateError(PRegister reg, TRegisterHandler::TErrorState errorState)
{
    auto it = RegisterToChannelStateMap.find(reg);
    if (it == RegisterToChannelStateMap.end()) {
        LOG(Warn) << "got unexpected register from serial client";
        return;
    }
    const auto & channel = it->second.Channel;
    const auto & registers = channel->Registers;

    it->second.ErrorState = errorState;
    size_t errorMask = 0;
    for (auto r: registers) {
        auto error = RegErrorState(r);
        if (error <= TRegisterHandler::ReadWriteError) {
            errorMask |= error;
        }
    }

    const char* errorFlags[] = {"", "w", "r", "rw"};
    MqttDriver->AccessAsync([=](const PDriverTx & tx){
        tx->GetDevice(channel->DeviceId)->GetControl(channel->Name)->SetError(tx, errorFlags[errorMask]);
    });
}

void TSerialPortDriver::Cycle()
{
    try {
        SerialClient->Cycle();
    } catch (TSerialDeviceException& e) {
        LOG(Error) << "FATAL: " << e.what() << ". Stopping event loops.";
        exit(1);
    }
}

void TSerialPortDriver::ClearDevices() noexcept
{
    try {
        {
            auto tx = MqttDriver->BeginTx();

            for (const auto & device: Devices) {
                try {
                    tx->RemoveDeviceById(device->DeviceConfig()->Id).Sync();
                    LOG(Debug) << "device " << device->DeviceConfig()->Id << " removed successfully";
                } catch (const exception & e) {
                    LOG(Warn) << "exception during device removal: " << e.what();
                } catch (...) {
                    LOG(Warn) << "unknown exception during device removal";
                }
            }
        }
        Devices.clear();
        SerialClient->ClearDevices();
    } catch (const exception & e) {
        LOG(Warn) << "TSerialPortDriver::ClearDevices(): " << e.what();
    } catch (...) {
        LOG(Warn) << "TSerialPortDriver::ClearDevices(): unknown exception";
    }
}

TLocalDeviceArgs TSerialPortDriver::From(const PSerialDevice & device)
{
    return TLocalDeviceArgs{}.SetId(device->DeviceConfig()->Id)
                             .SetTitle(device->DeviceConfig()->Name);
}

TControlArgs TSerialPortDriver::From(const PDeviceChannel & channel)
{
    auto args = TControlArgs{}.SetId(channel->Name)
                              .SetOrder(channel->Order)
                              .SetType(channel->Type)
                              .SetReadonly(channel->ReadOnly)
                              .SetUserData(TControlLinkData{ shared_from_this(), channel });

    if (channel->Type == "range" || channel->Type == "dimmer") {
        args.SetMax(channel->Max < 0 ? 65535 : channel->Max);
    }

    return args;
}
