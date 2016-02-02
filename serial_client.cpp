#include <unistd.h>

#include "serial_client.h"

TSerialClient::TSerialClient(PAbstractSerialPort port)
    : Port(port),
      Active(false),
      PollInterval(1000),
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
        throw TSerialProtocolException("can't add registers to the active client");
    ConfigMap[device_config->SlaveId] = device_config;
}

void TSerialClient::AddRegister(PRegister reg)
{
    if (Active)
        throw TSerialProtocolException("can't add registers to the active client");
    if (Handlers.find(reg) != Handlers.end())
        throw TSerialProtocolException("duplicate register");
    Handlers[reg] = CreateRegisterHandler(reg);
    RegList.push_back(reg);
}

void TSerialClient::Connect()
{
    if (Active)
        return;
    if (!Handlers.size())
        throw TSerialProtocolException("no registers defined");
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
        Flush();
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

        Port->USleep(PollInterval * 1000);
    }
    for (const auto& p: ProtoMap)
        p.second->EndPollCycle();
}

void TSerialClient::WriteSetupRegister(PRegister reg, uint64_t value)
{
    Connect();
    PrepareToAccessDevice(reg->Slave);
    GetProtocol(reg->Slave)->WriteRegister(reg, value);
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

PRegisterHandler TSerialClient::GetHandler(PRegister reg) const
{
    auto it = Handlers.find(reg);
    if (it == Handlers.end())
        throw TSerialProtocolException("register not found");
    return it->second;
}

PRegisterHandler TSerialClient::CreateRegisterHandler(PRegister reg)
{
    return std::make_shared<TRegisterHandler>(shared_from_this(), GetProtocol(reg->Slave), reg);
}

PSerialProtocol TSerialClient::GetProtocol(int slave_id)
{
    auto it = ProtoMap.find(slave_id);
    if (it != ProtoMap.end())
        return it->second;
    auto configIt = ConfigMap.find(slave_id);
    if (configIt == ConfigMap.end())
        throw TSerialProtocolException("slave not found");

    try {
        auto protocol = TSerialProtocolFactory::CreateProtocol(configIt->second, Port);
        return ProtoMap[slave_id] = protocol;
    } catch (const TSerialProtocolException& e) {
        Disconnect();
        throw TSerialProtocolException(e.what());
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
