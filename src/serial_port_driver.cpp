#include "serial_port_driver.h"
#include "log.h"

#include <wblib/wbmqtt.h>

#include <algorithm>
#include <cassert>
#include <iostream>
#include <sstream>

#include "serial_port.h"
#include "tcp_port.h"

using namespace std;
using namespace WBMQTT;

#define LOG(logger) ::logger.Log() << "[serial port driver] "

TSerialPortDriver::TSerialPortDriver(WBMQTT::PDeviceDriver mqttDriver,
                                     PPortConfig portConfig,
                                     const WBMQTT::TPublishParameters& publishPolicy,
                                     Metrics::TMetrics& metrics)
    : MqttDriver(mqttDriver),
      Config(portConfig),
      PublishPolicy(publishPolicy)
{
    Description = Config->Port->GetDescription(false);
    SerialClient = PSerialClient(new TSerialClient(Config->Devices, Config->Port, Config->OpenCloseSettings, metrics));
}

const std::string& TSerialPortDriver::GetShortDescription() const
{
    return Description;
}

void TSerialPortDriver::SetUpDevices()
{
    SerialClient->SetReadCallback([this](PRegister reg) { OnValueRead(reg); });
    SerialClient->SetErrorCallback([this](PRegister reg) { UpdateError(reg); });

    LOG(Debug) << "setting up devices at " << Config->Port->GetDescription();

    try {
        auto tx = MqttDriver->BeginTx();

        for (auto device: Config->Devices) {
            auto mqttDevice = tx->CreateDevice(From(device)).GetValue();
            assert(mqttDevice);
            Devices.push_back(device);
            // init channels' registers
            for (auto& channelConfig: device->DeviceConfig()->DeviceChannelConfigs) {
                try {
                    auto channel = std::make_shared<TDeviceChannel>(device, channelConfig);
                    channel->Control = mqttDevice->CreateControl(tx, From(channel)).GetValue();
                    for (auto& reg: channel->Registers) {
                        RegisterToChannelMap.emplace(reg, channel);
                        SerialClient->AddRegister(reg);
                    }
                } catch (const exception& e) {
                    LOG(Error) << "unable to create control: '" << e.what() << "'";
                }
            }
            mqttDevice->RemoveUnusedControls(tx).Sync();
        }
    } catch (const exception& e) {
        LOG(Error) << "unable to create device: '" << e.what() << "' Cleaning.";
        ClearDevices();
        throw;
    } catch (...) {
        LOG(Error) << "unable to create device or control. Cleaning.";
        ClearDevices();
        throw;
    }
}

void TSerialPortDriver::HandleControlOnValueEvent(const WBMQTT::TControlOnValueEvent& event)
{
    const auto& value = event.RawValue;
    const auto& linkData = event.Control->GetUserData().As<TControlLinkData>();

    const auto& portDriver = linkData.PortDriver.lock();
    const auto& channel = linkData.DeviceChannel.lock();

    if (!portDriver || !channel) {
        if (!portDriver) {
            LOG(Error) << "event for non existent port driver from control '" << event.Control->GetDevice()->GetId()
                       << "/" << event.Control->GetId() << "' value: '" << value << "'";
        }

        if (!channel) {
            LOG(Error) << "event for non existent device channel from control '" << event.Control->GetDevice()->GetId()
                       << "/" << event.Control->GetId() << "' value: '" << value << "'";
        }

        return;
    }

    portDriver->SetValueToChannel(channel, value);
}

void TSerialPortDriver::SetValueToChannel(const PDeviceChannel& channel, const string& value)
{
    const auto& registers = channel->Registers;

    std::vector<std::string> valueItems = StringSplit(value, ';');

    if (valueItems.size() != registers.size()) {
        LOG(Warn) << "invalid value for " << channel->Describe() << ": '" << value << "'";
        return;
    }

    for (size_t i = 0; i < registers.size(); ++i) {
        PRegister reg = registers[i];
        LOG(Debug) << "setting device register: " << reg->ToString() << " <- " << valueItems[i];

        try {
            auto valueToSet = valueItems[i];
            if (!channel->OnValue.empty() && valueItems[i] == "1") {
                valueToSet = channel->OnValue;
            } else if (!channel->OffValue.empty() && valueItems[i] == "0") {
                valueToSet = channel->OffValue;
            }
            SerialClient->SetTextValue(reg, valueToSet);

        } catch (std::exception& err) {
            LOG(Warn) << "invalid value for " << channel->Describe() << ": '" << value << "' : " << err.what();
            return;
        }
    }
}

void TSerialPortDriver::OnValueRead(PRegister reg)
{
    auto it = RegisterToChannelMap.find(reg);
    if (it == RegisterToChannelMap.end()) {
        LOG(Warn) << "got unexpected register from serial client";
        return;
    }
    if (it->second->HasValuesOfAllRegisters()) {
        it->second->UpdateValueAndError(*MqttDriver, PublishPolicy);
    }
}

void TSerialPortDriver::UpdateError(PRegister reg)
{
    auto it = RegisterToChannelMap.find(reg);
    if (it == RegisterToChannelMap.end()) {
        LOG(Warn) << "got unexpected register from serial client";
        return;
    }

    it->second->UpdateError(*MqttDriver);
}

void TSerialPortDriver::Cycle(std::chrono::steady_clock::time_point now)
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

            for (const auto& device: Devices) {
                try {
                    tx->RemoveDeviceById(device->DeviceConfig()->Id).Sync();
                    LOG(Debug) << "device " << device->DeviceConfig()->Id << " removed successfully";
                } catch (const exception& e) {
                    LOG(Warn) << "exception during device removal: " << e.what();
                } catch (...) {
                    LOG(Warn) << "unknown exception during device removal";
                }
            }
        }
        Devices.clear();
        SerialClient->ClearDevices();
    } catch (const exception& e) {
        LOG(Warn) << "TSerialPortDriver::ClearDevices(): " << e.what();
    } catch (...) {
        LOG(Warn) << "TSerialPortDriver::ClearDevices(): unknown exception";
    }
}

TLocalDeviceArgs TSerialPortDriver::From(const PSerialDevice& device)
{
    return TLocalDeviceArgs{}
        .SetId(device->DeviceConfig()->Id)
        .SetTitle(device->DeviceConfig()->Name)
        .SetIsVirtual(true)
        .SetDoLoadPrevious(true);
}

TControlArgs TSerialPortDriver::From(const PDeviceChannel& channel)
{
    auto args = TControlArgs{}
                    .SetId(channel->MqttId)
                    .SetOrder(channel->Order)
                    .SetType(channel->Type)
                    .SetReadonly(channel->ReadOnly)
                    .SetUserData(TControlLinkData{shared_from_this(), channel})
                    .SetUnits(channel->Units);

    if (isnan(channel->Max)) {
        if (channel->Type == "range" || channel->Type == "dimmer") {
            args.SetMax(65535);
        }
    } else {
        args.SetMax(channel->Max);
    }

    if (!isnan(channel->Min)) {
        args.SetMin(channel->Min);
    }

    if (channel->Precision != 0.0) {
        args.SetPrecision(channel->Precision);
    }

    for (const auto& tr: channel->GetTitles()) {
        args.SetTitle(tr.second, tr.first);
    }

    return args;
}

PSerialClient TSerialPortDriver::GetSerialClient()
{
    return SerialClient;
}

void TDeviceChannel::UpdateValueAndError(WBMQTT::TDeviceDriver& deviceDriver,
                                         const WBMQTT::TPublishParameters& publishPolicy)
{
    auto value = GetTextValue();
    auto error = GetErrorText();
    bool errorIsChanged = (CachedErrorText != error);
    switch (publishPolicy.Policy) {
        case TPublishParameters::PublishOnlyOnChange: {
            if (CachedCurrentValue != value) {
                PublishValueAndError(deviceDriver, value, error);
            } else {
                if (errorIsChanged) {
                    PublishError(deviceDriver, error);
                }
            }
            break;
        }
        case TPublishParameters::PublishAll: {
            PublishValueAndError(deviceDriver, value, error);
            break;
        }
        case TPublishParameters::PublishSomeUnchanged: {
            auto now = std::chrono::steady_clock::now();
            if (errorIsChanged || (CachedCurrentValue != value) ||
                (now - LastControlUpdate >= publishPolicy.PublishUnchangedInterval))
            {
                PublishValueAndError(deviceDriver, value, error);
            }
            break;
        }
    }
}

void TDeviceChannel::UpdateError(WBMQTT::TDeviceDriver& deviceDriver)
{
    PublishError(deviceDriver, GetErrorText());
}

std::string TDeviceChannel::GetErrorText() const
{
    TRegister::TErrorState errorState;
    for (auto r: Registers) {
        errorState |= r->GetErrorState();
    }

    const std::unordered_map<TRegister::TError, std::string> errorNames = {
        {TRegister::TError::ReadError, "r"},
        {TRegister::TError::WriteError, "w"},
        {TRegister::TError::PollIntervalMissError, "p"}};
    std::string errorText;
    for (size_t i = 0; i < TRegister::TError::MAX_ERRORS; ++i) {
        if (errorState.test(i)) {
            auto itName = errorNames.find(static_cast<TRegister::TError>(i));
            if (itName != errorNames.end()) {
                errorText += itName->second;
            }
        }
    }
    return errorText;
}

void TDeviceChannel::PublishValueAndError(WBMQTT::TDeviceDriver& deviceDriver,
                                          const std::string& value,
                                          const std::string& error)
{
    if (::Debug.IsEnabled()) {
        LOG(Debug) << Describe() << " <-- " << value << ", error: \"" << error << "\"";
    }
    CachedCurrentValue = value;
    CachedErrorText = error;
    LastControlUpdate = std::chrono::steady_clock::now();
    {
        auto tx = deviceDriver.BeginTx();
        Control->UpdateRawValueAndError(tx, value, error).Sync();
    }
}

void TDeviceChannel::PublishError(WBMQTT::TDeviceDriver& deviceDriver, const std::string& error)
{
    if (CachedErrorText.empty() || (CachedErrorText != error)) {
        CachedErrorText = error;
        auto tx = deviceDriver.BeginTx();
        Control->SetError(tx, error).Sync();
    }
}

std::string TDeviceChannel::GetTextValue() const
{
    if (Registers.size() == 1) {
        if (!OnValue.empty()) {
            if (ConvertFromRawValue(*Registers.front(), Registers.front()->GetValue()) == OnValue) {
                LOG(Debug) << "OnValue: " << OnValue << "; value: 1";
                return "1";
            }
        }
        if (!OffValue.empty()) {
            if (ConvertFromRawValue(*Registers.front(), Registers.front()->GetValue()) == OffValue) {
                LOG(Debug) << "OnValue: " << OffValue << "; value: 0";
                return "0";
            }
        }
    }
    std::string value;
    bool first = true;
    for (const auto& r: Registers) {
        if (!first) {
            value += ";";
        }
        first = false;
        value += ConvertFromRawValue(*r, r->GetValue());
    }
    return value;
}

bool TDeviceChannel::HasValuesOfAllRegisters() const
{
    for (const auto& r: Registers) {
        if (r->GetAvailable() == TRegisterAvailability::UNKNOWN) {
            return false;
        }
    }
    return true;
}
