#include <iostream>
#include <cmath>
#include <mutex>
#include <unistd.h>
#include <modbus/modbus.h>
#include "modbus_client.h"
#include <utility>

TModbusConnector::~TModbusConnector() {}

TModbusContext::~TModbusContext() {}

class TDefaultModbusContext: public TModbusContext {
public:
    TDefaultModbusContext(const TModbusConnectionSettings& settings);
    void Connect();
    void Disconnect();
    void SetDebug(bool debug);
    void SetSlave(int slave);
    void ReadCoils(int addr, int nb, uint8_t *dest);
    void WriteCoil(int addr, int value);
    void ReadDisceteInputs(int addr, int nb, uint8_t *dest);
    void ReadHoldingRegisters(int addr, int nb, uint16_t *dest);
    void WriteHoldingRegisters(int addr, int nb, const uint16_t *data);
    void ReadInputRegisters(int addr, int nb, uint16_t *dest);
    void USleep(int usec);
private:
    modbus_t* InnerContext;
};

TDefaultModbusContext::TDefaultModbusContext(const TModbusConnectionSettings& settings)
{
    InnerContext = modbus_new_rtu(settings.Device.c_str(), settings.BaudRate,
                                  settings.Parity, settings.DataBits, settings.StopBits);
    if (!InnerContext)
        throw TModbusException("failed to create modbus context");
    modbus_set_error_recovery(InnerContext, MODBUS_ERROR_RECOVERY_PROTOCOL); // FIXME

    if (settings.ResponseTimeoutMs > 0) {
        struct timeval tv;
        tv.tv_sec = settings.ResponseTimeoutMs / 1000;
        tv.tv_usec = (settings.ResponseTimeoutMs % 1000) * 1000;
        modbus_set_response_timeout(InnerContext, &tv);
    }
}

void TDefaultModbusContext::Connect()
{
    if (modbus_connect(InnerContext) != 0)
        throw TModbusException("couldn't initialize modbus connection");
    modbus_flush(InnerContext);
}

void TDefaultModbusContext::Disconnect()
{
    modbus_close(InnerContext);
}

void TDefaultModbusContext::SetDebug(bool debug)
{
    modbus_set_debug(InnerContext, debug);
}

void TDefaultModbusContext::SetSlave(int slave)
{
    modbus_set_slave(InnerContext, slave);
}

void TDefaultModbusContext::ReadCoils(int addr, int nb, uint8_t *dest)
{
    if (modbus_read_bits(InnerContext, addr, nb, dest) < nb)
        throw TModbusException("failed to read " + std::to_string(nb) +
                               " coil(s) @ " + std::to_string(addr));
}

void TDefaultModbusContext::WriteCoil(int addr, int value)
{
    if (modbus_write_bit(InnerContext, addr, value) < 0)
        throw TModbusException("failed to write coil @ " + std::to_string(addr));
}

void TDefaultModbusContext::ReadDisceteInputs(int addr, int nb, uint8_t *dest)
{
    if (modbus_read_input_bits(InnerContext, addr, nb, dest) < nb)
        throw TModbusException("failed to read " + std::to_string(nb) +
                               "discrete input(s) @ " + std::to_string(addr));
}

void TDefaultModbusContext::ReadHoldingRegisters(int addr, int nb, uint16_t *dest)
{
    if (modbus_read_registers(InnerContext, addr, nb, dest) < nb)
        throw TModbusException("failed to read " + std::to_string(nb) +
                               " holding register(s) @ " + std::to_string(addr));
}

void TDefaultModbusContext::WriteHoldingRegisters(int addr, int nb, const uint16_t *data)
{
    if (modbus_write_registers(InnerContext, addr, nb, data) < nb)
        throw TModbusException("failed to write " + std::to_string(nb) +
                               " holding register(s) @ " + std::to_string(addr));
}

void TDefaultModbusContext::ReadInputRegisters(int addr, int nb, uint16_t *dest)
{
    if (modbus_read_input_registers(InnerContext, addr, 1, dest) < nb)
        throw TModbusException("failed to read " + std::to_string(nb) +
                               " input register(s) @ " + std::to_string(addr));
}

void TDefaultModbusContext::USleep(int usec)
{
    usleep(usec);
}

PModbusContext TDefaultModbusConnector::CreateContext(const TModbusConnectionSettings& settings)
{
    return PModbusContext(new TDefaultModbusContext(settings));
}

typedef std::pair<bool, int> TErrorMessage;

class TRegisterHandler
{
public:
    TRegisterHandler(const TModbusClient* client, std::shared_ptr<TModbusRegister> _reg)
        : Client(client), reg(_reg) {}
    virtual ~TRegisterHandler() {}
    virtual int Read(PModbusContext ctx) = 0;
    virtual void Write(PModbusContext ctx, int v);
    std::shared_ptr<TModbusRegister> Register() const { return reg; }
    TErrorMessage Poll(PModbusContext ctx);
    bool Flush(PModbusContext ctx);
    int RawValue() const { return value; }
    double ScaledValue() const { return value * reg->Scale; }
    std::string TextValue() const;
    void SetRawValue(int v);
    void SetScaledValue(double v);
    void SetTextValue(const std::string& v);
    bool DidRead() const { return did_read; }
protected:
    int ConvertSlaveValue(uint16_t v) const;
    uint16_t ConvertMasterValue(int v) const;
    const TModbusClient* Client;
private:
    int value = 0;
    std::shared_ptr<TModbusRegister> reg;
    volatile bool dirty = false;
    bool did_read = false;
    std::mutex set_value_mutex;
};

void TRegisterHandler::Write(PModbusContext, int)
{
    throw TModbusException("trying to write read-only register");
};

TErrorMessage TRegisterHandler::Poll(PModbusContext ctx)
{
    if (!reg->Poll || dirty)
        return std::make_pair(false, 0); // write-only register

    bool first_poll = !did_read;
    int new_value;
    ctx->SetSlave(reg->Slave);
    try {
        new_value = Read(ctx);
    } catch (const TModbusException& e) {
        std::cerr << "TRegisterHandler::Poll(): warning: " << e.what() << " slave_id is " << reg->Slave <<  std::endl;
        reg->ErrorMessage = "Poll";
        return std::make_pair(true, 1);
    }
    did_read = true;
    set_value_mutex.lock();
    if (value != new_value) {
        if (dirty) {
            set_value_mutex.unlock();
            return std::make_pair(true, 0);
        }
        value = new_value;
        set_value_mutex.unlock();
        if (Client->DebugEnabled())
            std::cerr << "new val for " << reg->ToString() << ": " << new_value << std::endl;
        return std::make_pair(true, 0);
    } else
        set_value_mutex.unlock();
    return std::make_pair(first_poll, 0);
}

bool TRegisterHandler::Flush(PModbusContext ctx)
{
    set_value_mutex.lock();
    if (dirty) {
        dirty = false;
        set_value_mutex.unlock();
        ctx->SetSlave(reg->Slave);
        try {
            Write(ctx, ConvertMasterValue(value));
        } catch (const TModbusException& e) {
            std::cerr << "TRegisterHandler::Flush(): warning: " << e.what() << " slave_id is " << reg->Slave <<  std::endl;
            return true;
        }
    }
    else {
        set_value_mutex.unlock();
    }
    return false;
}

std::string TRegisterHandler::TextValue() const
{
    return reg->Scale == 1 ? std::to_string(RawValue()) : std::to_string(ScaledValue());
}

void TRegisterHandler::SetRawValue(int v)
{
    std::lock_guard<std::mutex> lock(set_value_mutex);
    value = v;
    dirty = true;
}

void TRegisterHandler::SetScaledValue(double v)
{
    SetRawValue(round(v / reg->Scale));
}

void TRegisterHandler::SetTextValue(const std::string& v)
{
    if (reg->Scale == 1)
        SetRawValue(stoi(v));
    else
        SetScaledValue(stod(v));
}

int TRegisterHandler::ConvertSlaveValue(uint16_t v) const
{
    switch (reg->Format) {
    case TModbusRegister::U16:
        return v;
    case TModbusRegister::S16:
        return (int16_t)v;
    case TModbusRegister::U8:
        return v & 255;
    case TModbusRegister::S8:
        return (int8_t) v;
    default:
        return v;
    }
}

uint16_t TRegisterHandler::ConvertMasterValue(int v) const
{
    switch (reg->Format) {
    case TModbusRegister::S16:
        return v & 65535;
    case TModbusRegister::U8:
    case TModbusRegister::S8:
        return v & 255;
    case TModbusRegister::U16:
    default:
        return v;
    }
}

class TCoilHandler: public TRegisterHandler
{
public:
    TCoilHandler(const TModbusClient* client, std::shared_ptr<TModbusRegister> _reg)
        : TRegisterHandler(client, _reg) {}

    int Read(PModbusContext ctx) {
        unsigned char b;
        ctx->ReadCoils(Register()->Address, 1, &b);
        return b & 1;
    }

    void Write(PModbusContext ctx, int v) {
        ctx->WriteCoil(Register()->Address, v);
    }
};

class TDiscreteInputHandler: public TRegisterHandler
{
public:
    TDiscreteInputHandler(const TModbusClient* client, std::shared_ptr<TModbusRegister> _reg)
        : TRegisterHandler(client, _reg) {}

    int Read(PModbusContext ctx) {
        uint8_t b;
        ctx->ReadDisceteInputs(Register()->Address, 1, &b);
        return b & 1;
    }
};

class THoldingRegisterHandler: public TRegisterHandler
{
public:
    THoldingRegisterHandler(const TModbusClient* client, std::shared_ptr<TModbusRegister> _reg)
        : TRegisterHandler(client, _reg) {}

    int Read(PModbusContext ctx) {
        uint16_t v;
        ctx->ReadHoldingRegisters(Register()->Address, 1, &v);
        return ConvertSlaveValue(v);
    }

    void Write(PModbusContext ctx, int v) {
        // FIXME: use 
        uint16_t d = (uint16_t)v;
        if (Client->DebugEnabled())
            std::cerr << "write: " << Register()->ToString() << std::endl;
        ctx->WriteHoldingRegisters(Register()->Address, 1, &d);
    }
};

class TInputRegisterHandler: public TRegisterHandler
{
public:
    TInputRegisterHandler(const TModbusClient* client, std::shared_ptr<TModbusRegister> _reg)
        : TRegisterHandler(client, _reg) {}

    int Read(PModbusContext ctx) {
        uint16_t v;
        ctx->ReadInputRegisters(Register()->Address, 1, &v);
        return ConvertSlaveValue(v);
    }
};

TModbusClient::TModbusClient(const TModbusConnectionSettings& settings,
                             PModbusConnector connector)
    : Active(false),
      PollInterval(1000)
{
    if (!connector)
        connector = PModbusConnector(new TDefaultModbusConnector);
    Context = connector->CreateContext(settings);
}

TModbusClient::~TModbusClient()
{
    if (Active)
        Disconnect();
}

void TModbusClient::AddRegister(std::shared_ptr<TModbusRegister> reg)
{
    if (Active)
        throw TModbusException("can't add registers to the active client");
    if (handlers.find(reg) != handlers.end())
        throw TModbusException("duplicate register");
    handlers[reg] = std::unique_ptr<TRegisterHandler>(CreateRegisterHandler(reg));
}

void TModbusClient::Connect()
{
    if (Active)
        return;
    if (!handlers.size())
        throw TModbusException("no registers defined");
    Context->Connect();
    Active = true;
}

void TModbusClient::Disconnect()
{
    Context->Disconnect();
    Active = false;
}

void TModbusClient::Cycle()
{
    Connect();

    // FIXME: that's suboptimal polling implementation.
    // Need to implement bunching of Modbus registers.
    // Note that for multi-register values, all values
    // corresponding to single register should be retrieved
    // by single query.
    for (const auto& p: handlers) {
        for(const auto& q: handlers) {
            if ((q.second->Flush(Context)) && (ErrorCallback)) {
                ErrorCallback(q.first);
            }
        }
        const auto& message = p.second->Poll(Context);
        if (message.first && Callback) {
            if ((message.second != 0) && (ErrorCallback)) {
                ErrorCallback(p.first);
            } else {
                Callback(p.first);
            }
        }
        Context->USleep(PollInterval * 1000);
    }
}

void TModbusClient::WriteHoldingRegister(int slave, int address, uint16_t value)
{
    Connect();
    Context->SetSlave(slave);
    Context->WriteHoldingRegisters(address, 1, &value);
}

void TModbusClient::SetRawValue(std::shared_ptr<TModbusRegister> reg, int value)
{
    GetHandler(reg)->SetRawValue(value);
}

void TModbusClient::SetScaledValue(std::shared_ptr<TModbusRegister> reg, double value)
{
    GetHandler(reg)->SetScaledValue(value);
}

void TModbusClient::SetTextValue(std::shared_ptr<TModbusRegister> reg, const std::string& value)
{
    GetHandler(reg)->SetTextValue(value);
}

int TModbusClient::GetRawValue(std::shared_ptr<TModbusRegister> reg) const
{
    return GetHandler(reg)->RawValue();
}

double TModbusClient::GetScaledValue(std::shared_ptr<TModbusRegister> reg) const
{
    return GetHandler(reg)->ScaledValue();
}

std::string TModbusClient::GetTextValue(std::shared_ptr<TModbusRegister> reg) const
{
    return GetHandler(reg)->TextValue();
}

bool TModbusClient::DidRead(std::shared_ptr<TModbusRegister> reg) const
{
    return GetHandler(reg)->DidRead();
}

void TModbusClient::SetCallback(const TModbusCallback& callback)
{
    Callback = callback;
}

void TModbusClient::SetErrorCallback(const TModbusCallback& callback)
{
    ErrorCallback = callback;
}

void TModbusClient::SetPollInterval(int interval)
{
    PollInterval = interval;
}

void TModbusClient::SetModbusDebug(bool debug)
{
    Context->SetDebug(debug);
    Debug = debug;
}

bool TModbusClient::DebugEnabled() const {
    return Debug;
}

const std::unique_ptr<TRegisterHandler>& TModbusClient::GetHandler(std::shared_ptr<TModbusRegister> reg) const
{
    auto it = handlers.find(reg);
    if (it == handlers.end())
        throw TModbusException("register not found");
    return it->second;
}

TRegisterHandler* TModbusClient::CreateRegisterHandler(std::shared_ptr<TModbusRegister> reg)
{
    switch (reg->Type) {
    case TModbusRegister::RegisterType::COIL:
        return new TCoilHandler(this, reg);
    case TModbusRegister::RegisterType::DISCRETE_INPUT:
        return new TDiscreteInputHandler(this, reg);
    case TModbusRegister::RegisterType::HOLDING_REGISTER:
        return new THoldingRegisterHandler(this, reg);
    case TModbusRegister::RegisterType::INPUT_REGISTER:
        return new TInputRegisterHandler(this, reg);
    default:
        throw TModbusException("bad register type");
    }
}
