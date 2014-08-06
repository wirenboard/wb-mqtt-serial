#include <iostream>
#include <unistd.h>
#include "modbus_client.h"

class TModbusHandler
{
public:
    TModbusHandler(const TModbusClient* client, const TModbusParameter& _param)
        : Client(client), param(_param) {}
    virtual ~TModbusHandler() {}
    virtual int Read(modbus_t* ctx) = 0;
    virtual void Write(modbus_t* ctx, int v);
    const TModbusParameter& Parameter() const { return param; }
    bool Poll(modbus_t* ctx);
    void Flush(modbus_t* ctx);
    int Value() const { return value; }
    void SetValue(int v);
protected:
    int ConvertValue(uint16_t v) const;
    const TModbusClient* Client;
private:
    int value = 0;
    TModbusParameter param;
    bool dirty = false;
    bool did_read = false;
};

void TModbusHandler::Write(modbus_t*, int)
{
    throw TModbusException("trying to write read-only parameter");
};

bool TModbusHandler::Poll(modbus_t* ctx)
{
    if (!param.poll || dirty)
        return false; // write-only register

    bool first_poll = !did_read;
    int new_value;
    modbus_set_slave(ctx, param.slave);
    try {
        new_value = Read(ctx);
    } catch (const TModbusException& e) {
        std::cerr << "TModbusHandler::Poll(): warning: " << e.what() << std::endl;
        return false;
    }
    did_read = true;
    if (value != new_value) {
        value = new_value;
        if (Client->DebugEnabled())
            std::cerr << "new val for " << param.str() << ": " << new_value << std::endl;
        return true;
    }
    return first_poll;
}

void TModbusHandler::Flush(modbus_t* ctx)
{
    if (dirty) {
        modbus_set_slave(ctx, param.slave);
        try {
            Write(ctx, value);
        } catch (const TModbusException& e) {
            std::cerr << "TModbusHandler::Flush(): warning: " << e.what() << std::endl;
            return;
        }
        dirty = false;
    }
}

void TModbusHandler::SetValue(int v)
{
    value = v;
    dirty = true;
}

int TModbusHandler::ConvertValue(uint16_t v) const
{
    switch (param.format) {
    case TModbusParameter::U16:
        return v;
    case TModbusParameter::S16:
        return (int16_t)v;
    case TModbusParameter::U8:
        return v & 255;
    case TModbusParameter::S8:
        return (int8_t) v;
    default:
        return v;
    }
}

class TCoilHandler: public TModbusHandler
{
public:
    TCoilHandler(const TModbusClient* client, const TModbusParameter& _param)
        : TModbusHandler(client, _param) {}

    int Read(modbus_t* ctx) {
        unsigned char b;
        if (modbus_read_bits(ctx, Parameter().address, 1, &b) < 1)
            throw TModbusException("failed to read coil " + Parameter().str());
        return b & 1;
    }

    void Write(modbus_t* ctx, int v) {
        if (modbus_write_bit(ctx, Parameter().address, v) < 0)
            throw TModbusException("failed to write coil " + Parameter().str());
    }
};

class TDiscreteInputHandler: public TModbusHandler
{
public:
    TDiscreteInputHandler(const TModbusClient* client, const TModbusParameter& _param)
        : TModbusHandler(client, _param) {}

    int Read(modbus_t* ctx) {
        uint8_t b;
        if (modbus_read_input_bits(ctx, Parameter().address, 1, &b) < 1)
            throw TModbusException("failed to read discrete input " + Parameter().str());
        return b & 1;
    }
};

class THoldingRegisterHandler: public TModbusHandler
{
public:
    THoldingRegisterHandler(const TModbusClient* client, const TModbusParameter& _param)
        : TModbusHandler(client, _param) {}

    int Read(modbus_t* ctx) {
        uint16_t v;
        if (modbus_read_registers(ctx, Parameter().address, 1, &v) < 1)
            throw TModbusException("failed to read holding register " + Parameter().str());
        return ConvertValue(v);
    }

    void Write(modbus_t* ctx, int v) {
        // FIXME: the following code causes an error:
        // if (modbus_write_register(ctx, Parameter().address, v) < 0)
        //     throw TModbusException("failed to write holding register");
        // setting modbus register: <1:holding: 1> <- 30146
        // <01><03><02><5A><1C><83><2D>
        // [01][06][00][01][75][C2][7F][0B]
        // Waiting for a confirmation...
        // <01><03><02><00><00><B8><44>
        // Message length not corresponding to the computed length (7 != 8)
        // Fatal: Modbus error: failed to write holding register
        // Exiting...
        uint16_t d = (uint16_t)v;
        if (Client->DebugEnabled())
            std::cerr << "write: " << Parameter().str() << std::endl;
        if (modbus_write_registers(ctx, Parameter().address, 1, &d) < 1)
            throw TModbusException("failed to write holding register " + Parameter().str());
    }
};

class TInputRegisterHandler: public TModbusHandler
{
public:
    TInputRegisterHandler(const TModbusClient* client, const TModbusParameter& _param)
        : TModbusHandler(client, _param) {}

    int Read(modbus_t* ctx) {
        uint16_t v;
        if (modbus_read_input_registers(ctx, Parameter().address, 1, &v) < 1)
            throw TModbusException("failed to read input register " + Parameter().str());
        return ConvertValue(v);
    }
};

TModbusClient::TModbusClient(std::string device,
                             int baud_rate,
                             char parity,
                             int data_bits,
                             int stop_bits)
    : active(false),
      poll_interval(1000)
{
    ctx = modbus_new_rtu(device.c_str(), baud_rate, parity, data_bits, stop_bits);
    if (!ctx)
        throw TModbusException("failed to create modbus context");
    modbus_set_error_recovery(ctx, MODBUS_ERROR_RECOVERY_PROTOCOL); // FIXME
}

TModbusClient::~TModbusClient()
{
    if (active)
        Disconnect();
    modbus_free(ctx);
}

void TModbusClient::AddParam(const TModbusParameter& param)
{
    if (active)
        throw TModbusException("can't add parameters to the active client");
    if (handlers.find(param) != handlers.end())
        throw TModbusException("duplicate parameter");
    handlers[param] = std::unique_ptr<TModbusHandler>(CreateParameterHandler(param));
}

void TModbusClient::Connect()
{
    if (active)
        return;
    if (modbus_connect(ctx) != 0)
        throw TModbusException("couldn't initialize the serial port");
    modbus_flush(ctx);
    active = true;
}

void TModbusClient::Disconnect()
{
    modbus_close(ctx);
    active = false;
}

void TModbusClient::Loop()
{
    if (!handlers.size())
        throw TModbusException("no parameters defined");

    Connect();

    // FIXME: that's suboptimal polling implementation.
    // Need to implement bunching of Modbus registers.
    // Note that for multi-register values, all values
    // corresponding to single parameter should be retrieved
    // by single query.
    for (;;) {
        for (const auto& p: handlers) {
            p.second->Flush(ctx);
            if (p.second->Poll(ctx) && callback)
                callback(p.first, p.second->Value());
            usleep(poll_interval * 1000);
        }
    }
}

void TModbusClient::WriteHoldingRegister(int slave, int address, uint16_t value)
{
    Connect();
    modbus_set_slave(ctx, slave);
    if (modbus_write_registers(ctx, address, 1, &value) < 1)
        throw TModbusException("failed to write holding register " +
                               std::to_string(address));
}

void TModbusClient::SetValue(const TModbusParameter& param, int value)
{
    auto it = handlers.find(param);
    if (it == handlers.end())
        throw TModbusException("parameter not found");
    it->second->SetValue(value);
}

void TModbusClient::SetCallback(const TModbusCallback& _callback)
{
    callback = _callback;
}

void TModbusClient::SetPollInterval(int interval)
{
    poll_interval = interval;
}

void TModbusClient::SetModbusDebug(bool debug)
{
    modbus_set_debug(ctx, debug);
    Debug = debug;
}

bool TModbusClient::DebugEnabled() const {
    return Debug;
}

TModbusHandler* TModbusClient::CreateParameterHandler(const TModbusParameter& param)
{
    switch (param.type) {
    case TModbusParameter::Type::COIL:
        return new TCoilHandler(this, param);
    case TModbusParameter::Type::DISCRETE_INPUT:
        return new TDiscreteInputHandler(this, param);
    case TModbusParameter::Type::HOLDING_REGITER:
        return new THoldingRegisterHandler(this, param);
    case TModbusParameter::Type::INPUT_REGISTER:
        return new TInputRegisterHandler(this, param);
    default:
        throw TModbusException("bad parameter type");
    }
}
