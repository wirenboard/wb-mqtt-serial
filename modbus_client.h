#pragma once

#include <map>
#include <string>
#include <vector>
#include <memory>
#include <sstream>
#include <exception>
#include <functional>

// #include <modbus/modbus.h>

#include "portsettings.h"
#include "regformat.h"

class TRegisterHandler;

class TModbusContext
{
public:
    virtual ~TModbusContext();
    virtual void Connect() = 0;
    virtual void Disconnect() = 0;
    virtual void SetDebug(bool debug) = 0;
    virtual void SetSlave(int slave) = 0;
    virtual void ReadCoils(int addr, int nb, uint8_t *dest) = 0;
    virtual void WriteCoil(int addr, int value) = 0;
    virtual void ReadDisceteInputs(int addr, int nb, uint8_t *dest) = 0;
    virtual void ReadHoldingRegisters(int addr, int nb, uint16_t *dest) = 0;
    virtual void WriteHoldingRegisters(int addr, int nb, const uint16_t *data) = 0;
    virtual void WriteHoldingRegister(int addr, uint16_t value) = 0;
    virtual void ReadInputRegisters(int addr, int nb, uint16_t *dest) = 0;
    virtual void ReadDirectRegister(int addr, uint64_t* dest, RegisterFormat format) = 0;
    virtual void WriteDirectRegister(int addr, uint64_t value, RegisterFormat format) = 0;
    virtual void USleep(int usec) = 0;
};

typedef std::shared_ptr<TModbusContext> PModbusContext;

class TModbusConnector
{
public:
    virtual ~TModbusConnector();
    virtual PModbusContext CreateContext(const TSerialPortSettings& settings) = 0;
};

typedef std::shared_ptr<TModbusConnector> PModbusConnector;

class TDefaultModbusConnector: public TModbusConnector {
public:
    PModbusContext CreateContext(const TSerialPortSettings& settings);
};

struct TModbusRegister
{
    enum RegisterType { COIL, DISCRETE_INPUT, HOLDING_REGISTER, INPUT_REGISTER, DIRECT_REGISTER };

    TModbusRegister(int slave = 0, RegisterType type = COIL, int address = 0,
                     RegisterFormat format = U16, double scale = 1,
                     bool poll = true, bool readonly = false)
        : Slave(slave), Type(type), Address(address), Format(format),
          Scale(scale), Poll(poll), ForceReadOnly(readonly), ErrorMessage("") {}

    int Slave;
    RegisterType Type;
    int Address;
    RegisterFormat Format;
    double Scale;
    bool Poll;
    bool ForceReadOnly;
    std::string ErrorMessage;

    bool IsReadOnly() const {
        return Type == RegisterType::DISCRETE_INPUT ||
            Type == RegisterType::INPUT_REGISTER || ForceReadOnly;
    }

    uint8_t Width() const {
        switch (Format) {
            case S64:
            case U64:
            case Double:
                return 4;
            case U24:
            case S24:
            case U32:
            case S32:
            case BCD24:
            case BCD32:
            case Float:
                return 2;
            default:
                return 1;
        }
    }

    std::string ToString() const {
        std::stringstream s;
        s << "<" << Slave << ":" <<
            (Type == COIL ? "coil" :
             Type == DISCRETE_INPUT ? "discrete" :
             Type == HOLDING_REGISTER ? "holding" :
             Type == INPUT_REGISTER ? "input" :
             Type == DIRECT_REGISTER ? "direct" :
             "bad") << ": " << Address << ">";
        return s.str();
    }

    bool operator< (const TModbusRegister& reg) const {
        if (Slave < reg.Slave)
            return true;
        if (Slave > reg.Slave)
            return false;
        if (Type < reg.Type)
            return true;
        if (Type > reg.Type)
            return false;
        return Address < reg.Address;
    }

    bool operator== (const TModbusRegister& reg) const {
        return Slave == reg.Slave && Type == reg.Type && Address == reg.Address;
    }


};

inline ::std::ostream& operator<<(::std::ostream& os, std::shared_ptr<TModbusRegister> reg) {
    return os << reg->ToString();
}

inline ::std::ostream& operator<<(::std::ostream& os, const TModbusRegister& reg) {
    return os << reg.ToString();
}

namespace std {
    template <> struct hash<std::shared_ptr<TModbusRegister>> {
        size_t operator()(std::shared_ptr<TModbusRegister> p) const {
            return hash<int>()(p->Slave) ^ hash<int>()(p->Type) ^ hash<int>()(p->Address);
        }
    };
}

class TModbusException: public std::exception {
public:
    TModbusException(std::string _message): message("Modbus error: " + _message) {}
    const char* what () const throw ()
    {
        return message.c_str();
    }

private:
    std::string message;
};

typedef std::function<void(std::shared_ptr<TModbusRegister> reg)> TModbusCallback;

class TModbusClient
{
public:
    TModbusClient(const TSerialPortSettings& settings,
                  PModbusConnector connector = 0);
    TModbusClient(const TModbusClient& client) = delete;
    TModbusClient& operator=(const TModbusClient&) = delete;
    ~TModbusClient();
    void AddRegister(std::shared_ptr<TModbusRegister> reg);
    void Connect();
    void Disconnect();
    void Cycle();
    void SetTextValue(std::shared_ptr<TModbusRegister> reg, const std::string& value);
    std::string GetTextValue(std::shared_ptr<TModbusRegister> reg) const;
    bool DidRead(std::shared_ptr<TModbusRegister> reg) const;
    void SetCallback(const TModbusCallback& callback);
    void SetErrorCallback(const TModbusCallback& callback);
    void SetDeleteErrorsCallback(const TModbusCallback& callback);
    void SetPollInterval(int ms);
    void SetModbusDebug(bool debug);
    bool DebugEnabled() const;
    void WriteHoldingRegister(int slave, int address, uint16_t value);

private:


    const std::unique_ptr<TRegisterHandler>& GetHandler(std::shared_ptr<TModbusRegister>) const;
    TRegisterHandler* CreateRegisterHandler(std::shared_ptr<TModbusRegister> reg);
    std::map<std::shared_ptr<TModbusRegister>, std::unique_ptr<TRegisterHandler> > handlers;
    PModbusContext Context;
    bool Active;
    int PollInterval;
    const int MAX_REGS = 65536;
    TModbusCallback Callback;
    TModbusCallback ErrorCallback;
    TModbusCallback DeleteErrorsCallback;
    bool Debug = false;
};

typedef std::shared_ptr<TModbusClient> PModbusClient;

