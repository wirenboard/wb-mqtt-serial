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

namespace
{
    template <class T, class V>
    inline void UpdateOrMake(unique_ptr<T> & pointer, const V & value)
    {
        if (pointer) {
            *pointer = value;
        } else {
            pointer = MakeUnique<T>(value);
        }
    }
}


TSerialPortDriver::TSerialPortDriver(WBMQTT::PDeviceDriver mqttDriver, PPortConfig portConfig, PPort portOverride)
    : MqttDriver(mqttDriver),
      Config(portConfig)
{
    // FIXME: Settings classes hierarchy is completely broken. As a result we have ugly casts
    if (portOverride) {
        Port = portOverride;
    } else {
        auto serialCfg = std::dynamic_pointer_cast<TSerialPortSettings>(Config->ConnSettings);
        if (serialCfg) {
            Port = std::make_shared<TSerialPort>(serialCfg);
        } else {
            auto tcpCfg = std::dynamic_pointer_cast<TTcpPortSettings>(Config->ConnSettings);
            if (tcpCfg) {
                Port = std::make_shared<TTcpPort>(tcpCfg);
            } else {
                throw std::runtime_error("Can't create port instance");
            }
        }
    }
    SerialClient = PSerialClient(new TSerialClient(Port));
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

    LOG(Debug) << "setting up devices at " << Config->ConnSettings->ToString();

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
                    RegisterToChannelStateMap.emplace(reg, TDeviceChannelState{channel, TRegisterHandler::UnknownErrorState, nullptr});
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
    const auto & value = event.Control->GetRawValue();
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

bool TSerialPortDriver::NeedToPublish(PRegister reg, bool changed)
{
    // max_unchanged_interval = 0: always update, don't track last change time
    if (!Config->MaxUnchangedInterval)
        return true;

    // max_unchanged_interval < 0: update if changed, don't track last change time
    if (Config->MaxUnchangedInterval < 0)
        return changed;

    auto it = RegisterToChannelStateMap.find(reg);
    if (it == RegisterToChannelStateMap.end()) {
        return false;   // should not happen
    }

    // max_unchanged_interval > 0: update if changed or the time interval is exceeded
    auto now = Port->CurrentTime();
    if (changed) {
        UpdateOrMake(it->second.LastPublishTime, now);
        return true;
    }

    if (!it->second.LastPublishTime) {
        // too strange - unchanged, but not tracked yet, but ok, let's publish it
        UpdateOrMake(it->second.LastPublishTime, now);
        return true;
    }

    std::chrono::duration<double> elapsed = now - *it->second.LastPublishTime;
    if (elapsed.count() < Config->MaxUnchangedInterval)
        return false; // still fresh

    // unchanged interval elapsed
    UpdateOrMake(it->second.LastPublishTime, now);
    return true;
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

    if (!NeedToPublish(reg, changed))
        return;

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

    {
        auto tx = MqttDriver->BeginTx();
        tx->GetDevice(channel->DeviceId)->GetControl(channel->Name)->SetRawValue(tx, value);
    }
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
    bool readError = false, writeError = false;
    for (auto r: registers) {
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

    std::string errorStr = readError ? (writeError ? "rw" : "r") : (writeError ? "w" : "");
    {
        auto tx = MqttDriver->BeginTx();
        tx->GetDevice(channel->DeviceId)->GetControl(channel->Name)->SetError(tx, errorStr);
    }
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
