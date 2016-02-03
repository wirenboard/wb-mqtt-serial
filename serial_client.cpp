#include <unistd.h>
#include <iostream>

#include "serial_client.h"

TSerialClient::TSerialClient(PAbstractSerialPort port)
    : Port(port),
      Active(false),
      PollInterval(20),
      Callback([](PRegister){}),
      ErrorCallback([](PRegister, bool){}) {}

TSerialClient::~TSerialClient()
{
    if (Active)
        Disconnect();
}

void TSerialClient::AddDevice(PDeviceConfig device_config)
{
    if (Active)
        throw TSerialDeviceException("can't add registers to the active client");
    ConfigMap[device_config->SlaveId] = device_config;
}

void TSerialClient::AddRegister(PRegister reg)
{
    if (Active)
        throw TSerialDeviceException("can't add registers to the active client");
    if (Handlers.find(reg) != Handlers.end())
        throw TSerialDeviceException("duplicate register");
    Handlers[reg] = CreateRegisterHandler(reg);
    RegList.push_back(reg);
}

void TSerialClient::Connect()
{
    if (Active)
        return;
    if (!Handlers.size())
        throw TSerialDeviceException("no registers defined");
    if (!Port->IsOpen())
        Port->Open();
    Active = true;
}

void TSerialClient::Disconnect()
{
    if (Port->IsOpen())
        Port->Close();
    Active = false;
}

void TSerialClient::MaybeUpdateErrorState(PRegister reg, TRegisterHandler::TErrorState state)
{
    if (state != TRegisterHandler::UnknownErrorState && state != TRegisterHandler::ErrorStateUnchanged)
        ErrorCallback(reg, state);
}

void TSerialClient::Flush()
{
    for (const auto& reg: RegList) {
        auto handler = Handlers[reg];
        if (!handler->NeedToFlush())
            continue;
        PrepareToAccessDevice(reg->Slave);
        MaybeUpdateErrorState(reg, handler->Flush());
    }
}

void TSerialClient::Cycle()
{
    Connect();

    // FIXME: that's suboptimal polling implementation.
    // Need to implement bunching of device registers.
    // Note that for multi-register values, all values
    // corresponding to single register should be retrieved
    // by single query.
    for (const auto& reg: RegList) {
        std::unique_lock<std::mutex> lock(FlushNeededLock);

        auto wait_until = std::chrono::steady_clock::now() +
            std::chrono::microseconds(PollInterval * 1000 / RegList.size());

        while (FlushNeededCond.wait_until(lock, 
            wait_until,
            [this](){return FlushNeeded;}
            ))
        {
            Flush();
            FlushNeeded = false;
        } 


        auto handler = Handlers[reg];
        bool changed = false;
        if (!handler->NeedToPoll())
            continue;
        PrepareToAccessDevice(reg->Slave);
        MaybeUpdateErrorState(reg, handler->Poll(&changed));
        // Note that p.second->CurrentErrorState() is not the
        // same as the value returned by p->second->Poll(...),
        // because the latter may be ErrorStateUnchanged.
        if (changed &&
            handler->CurrentErrorState() != TRegisterHandler::ReadError &&
            handler->CurrentErrorState() != TRegisterHandler::ReadWriteError)
            Callback(reg);

    }
    for (const auto& p: DeviceMap)
        p.second->EndPollCycle();
}

void TSerialClient::WriteSetupRegister(PRegister reg, uint64_t value)
{
    Connect();
    PrepareToAccessDevice(reg->Slave);
    GetDevice(reg->Slave)->WriteRegister(reg, value);
}

void TSerialClient::SetTextValue(PRegister reg, const std::string& value)
{
    GetHandler(reg)->SetTextValue(value);
}

std::string TSerialClient::GetTextValue(PRegister reg) const
{
    return GetHandler(reg)->TextValue();
}

bool TSerialClient::DidRead(PRegister reg) const
{
    return GetHandler(reg)->DidRead();
}

void TSerialClient::SetCallback(const TSerialClient::TCallback& callback)
{
    Callback = callback;
}

void TSerialClient::SetErrorCallback(const TSerialClient::TErrorCallback& callback)
{
    ErrorCallback = callback;
}

void TSerialClient::SetPollInterval(int interval)
{
    PollInterval = interval;
}

void TSerialClient::SetDebug(bool debug)
{
    Debug = debug;
    Port->SetDebug(debug);
}

bool TSerialClient::DebugEnabled() const {
    return Debug;
}

void TSerialClient::NotifyFlushNeeded() 
{
    std::unique_lock<std::mutex> lock(FlushNeededLock);
    FlushNeeded = true;
    FlushNeededCond.notify_all();
}

PRegisterHandler TSerialClient::GetHandler(PRegister reg) const
{
    auto it = Handlers.find(reg);
    if (it == Handlers.end())
        throw TSerialDeviceException("register not found");
    return it->second;
}

PRegisterHandler TSerialClient::CreateRegisterHandler(PRegister reg)
{
    return std::make_shared<TRegisterHandler>(shared_from_this(), GetDevice(reg->Slave), reg);
}

PSerialDevice TSerialClient::GetDevice(int slave_id)
{
    auto it = DeviceMap.find(slave_id);
    if (it != DeviceMap.end())
        return it->second;
    auto configIt = ConfigMap.find(slave_id);
    if (configIt == ConfigMap.end())
        throw TSerialDeviceException("slave not found");

    try {
        return DeviceMap[slave_id] = TSerialDeviceFactory::CreateDevice(configIt->second, Port);
    } catch (const TSerialDeviceException& e) {
        Disconnect();
        throw TSerialDeviceException(e.what());
    }
}

void TSerialClient::PrepareToAccessDevice(int slave)
{
    if (slave == LastAccessedSlave)
        return;
    LastAccessedSlave = slave;
    auto it = ConfigMap.find(slave);
    if (it == ConfigMap.end())
        return;
    if (it->second->DelayUSec > 0)
        Port->USleep(it->second->DelayUSec);
}
