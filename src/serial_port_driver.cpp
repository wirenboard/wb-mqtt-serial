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
                                     size_t lowPriorityRateLimit)
    : MqttDriver(mqttDriver),
      Config(portConfig),
      PublishPolicy(publishPolicy)
{
    Description = Config->Port->GetDescription(false);
    SerialClient = PSerialClient(new TSerialClient(Config->Port,
                                                   Config->OpenCloseSettings,
                                                   std::chrono::steady_clock::now,
                                                   lowPriorityRateLimit));
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

        for (const auto& device: Config->Devices) {
            device->Device->AddOnConnectionStateChangedCallback(
                [this](PSerialDevice dev) { OnDeviceConnectionStateChanged(dev); });
            auto mqttDevice = tx->CreateDevice(From(device->Device)).GetValue();
            Devices.push_back(device->Device);
            std::vector<PDeviceChannel> channels;
            // init channels' registers
            for (const auto& channelConfig: device->Channels) {
                try {
                    auto channel = std::make_shared<TDeviceChannel>(device->Device, channelConfig);
                    channel->Control = mqttDevice->CreateControl(tx, From(channel)).GetValue();
                    for (const auto& reg: channel->Registers) {
                        RegisterToChannelMap.emplace(reg, channel);
                    }
                    channels.push_back(channel);
                } catch (const exception& e) {
                    LOG(Error) << "unable to create control: '" << e.what() << "'";
                }
            }
            mqttDevice->RemoveUnusedControls(tx).Sync();
            SerialClient->AddDevice(device->Device);
            DeviceToChannelsMap.emplace(device->Device, channels);
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
        // change publish policy for sporadic registers to publish register data on every read, even if it not changed
        // needed for DALI bus devices polling
        auto publishPolicy = PublishPolicy;
        if ((reg->GetConfig()->SporadicMode == TRegisterConfig::TSporadicMode::ONLY_EVENTS &&
             reg->IsExcludedFromPolling()) ||
            reg->Device()->IsSporadicOnly())
        {
            publishPolicy.Policy = TPublishParameters::PublishAll;
        }
        it->second->UpdateValueAndError(*MqttDriver, publishPolicy);
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

void TSerialPortDriver::OnDeviceConnectionStateChanged(PSerialDevice device)
{
    if (device->GetConnectionState() == TDeviceConnectionState::DISCONNECTED) {
        auto it = DeviceToChannelsMap.find(device);
        if (it != DeviceToChannelsMap.end()) {
            for (auto& channel: it->second) {
                channel->DoNotPublishNextZeroPressCounter();
            }
        }
    }

    auto tx = MqttDriver->BeginTx();
    auto mqttDevice = tx->GetDevice(device->DeviceConfig()->Id);
    auto localMqttDevice = dynamic_pointer_cast<TLocalDevice>(mqttDevice);
    if (localMqttDevice) {
        localMqttDevice->SetError(tx, (device->GetConnectionState() == TDeviceConnectionState::DISCONNECTED) ? "r" : "")
            .Sync();
    }
}

void TSerialPortDriver::Cycle(std::chrono::steady_clock::time_point now)
{
    try {
        SerialClient->Cycle();
    } catch (const TSerialDeviceException& e) {
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
        RegisterToChannelMap.clear();
        DeviceToChannelsMap.clear();
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

    for (const auto& it: channel->GetEnumTitles()) {
        args.SetEnumValueTitles(it.first, it.second);
    }

    if (std::any_of(channel->Registers.cbegin(), channel->Registers.cend(), [](const auto& reg) {
            return reg->GetConfig()->TypeName == "press_counter";
        }))
    {
        args.SetDurable();
    }

    return args;
}

PSerialClient TSerialPortDriver::GetSerialClient()
{
    return SerialClient;
}

TDeviceChannel::TDeviceChannel(PSerialDevice device, PDeviceChannelConfig config)
    : TDeviceChannelConfig(*config),
      Device(device),
      PublishNextZeroPressCounter(true)
{}

std::string TDeviceChannel::Describe() const
{
    const auto& name = GetName();
    if (name != MqttId) {
        return "channel '" + name + "' (MQTT control '" + MqttId + "') of device '" + DeviceId + "'";
    }
    return "channel '" + name + "' of device '" + DeviceId + "'";
}

void TDeviceChannel::UpdateValueAndError(WBMQTT::TDeviceDriver& deviceDriver,
                                         const WBMQTT::TPublishParameters& publishPolicy)
{
    std::string value;
    try {
        value = GetTextValue();
    } catch (const TRegisterValueException& err) {
        // Register value is not defined, still able to update error
        // This can happen on successful events read after unsuccessful events read
        // when some registers aren't yet polled for the first time
        if (::Debug.IsEnabled()) {
            LOG(Debug) << "Trying to publish " << Describe() << " with undefined value";
        }
        UpdateError(deviceDriver);
        return;
    }
    auto error = GetErrorText();
    bool errorIsChanged = (CachedErrorText != error);
    if (ShouldNotPublishPressCounter()) {
        if (errorIsChanged) {
            PublishError(deviceDriver, error);
        }
        CachedCurrentValue = value;
        return;
    }
    LOG(Debug) << "Publish " << Describe() << " using policy " << static_cast<int>(publishPolicy.Policy);
    PublishNextZeroPressCounter = true;
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
        std::stringstream ss;
        ss << Describe() << " <-- " << value;
        if (!error.empty()) {
            ss << ", error: \"" << error << "\"";
        }
        LOG(Debug) << ss.str();
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
            if (ConvertFromRawValue(*Registers.front()->GetConfig(), Registers.front()->GetValue()) == OnValue) {
                LOG(Debug) << "OnValue: " << OnValue << "; value: 1";
                return "1";
            }
        }
        if (!OffValue.empty()) {
            if (ConvertFromRawValue(*Registers.front()->GetConfig(), Registers.front()->GetValue()) == OffValue) {
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
        value += ConvertFromRawValue(*r->GetConfig(), r->GetValue());
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

bool TDeviceChannel::ShouldNotPublishPressCounter() const
{
    for (const auto& r: Registers) {
        if (r->GetConfig()->TypeName == "press_counter" && !PublishNextZeroPressCounter) {
            try {
                if (r->GetValue().Get<uint16_t>() == 0) {
                    return true;
                }
            } catch (const TRegisterValueException&) {
            }
        }
    }
    return false;
}

void TDeviceChannel::DoNotPublishNextZeroPressCounter()
{
    PublishNextZeroPressCounter = false;
}