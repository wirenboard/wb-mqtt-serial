#pragma once

#include <map>
#include <string>
#include <vector>
#include <memory>
#include <sstream>
#include <exception>
#include <functional>

// #include <modbus/modbus.h>

class TRegisterHandler;

struct TModbusConnectionSettings
{
    TModbusConnectionSettings(std::string device = "/dev/ttyS0",
                              int baud_rate = 9600,
                              char parity = 'N',
                              int data_bits = 8,
                              int stop_bits = 1)
        : Device(device), BaudRate(baud_rate), Parity(parity),
          DataBits(data_bits), StopBits(stop_bits) {}

    std::string Device;
    int BaudRate;
    char Parity;
    int DataBits;
    int StopBits;
};

inline ::std::ostream& operator<<(::std::ostream& os, const TModbusConnectionSettings& settings) {
    return os << "<" << settings.Device << " " << settings.BaudRate <<
        " " << settings.DataBits << " " << settings.Parity << settings.StopBits << ">";
}

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
    virtual void ReadInputRegisters(int addr, int nb, uint16_t *dest) = 0;
    virtual void USleep(int usec) = 0;
};

typedef std::shared_ptr<TModbusContext> PModbusContext;

class TModbusConnector
{
public:
    virtual ~TModbusConnector();
    virtual PModbusContext CreateContext(const TModbusConnectionSettings& settings) = 0;
};

typedef std::shared_ptr<TModbusConnector> PModbusConnector;

struct TModbusRegister
{
    enum RegisterFormat { U16, S16, U8, S8 };
    enum RegisterType { COIL, DISCRETE_INPUT, HOLDING_REGISTER, INPUT_REGISTER };
    TModbusRegister(int slave = 0, RegisterType type = COIL, int address = 0,
                     RegisterFormat format = U16, double scale = 1,
                     bool poll = true)
        : Slave(slave), Type(type), Address(address), Format(format),
          Scale(scale), Poll(poll) {}
    int Slave;
    RegisterType Type;
    int Address;
    RegisterFormat Format;
    double Scale;
    bool Poll;

    bool IsReadOnly() const {
        return Type == RegisterType::DISCRETE_INPUT ||
            Type == RegisterType::INPUT_REGISTER;
    }

    std::string ToString() const {
        std::stringstream s;
        s << "<" << Slave << ":" <<
            (Type == COIL ? "coil" :
             Type == DISCRETE_INPUT ? "discrete" :
             Type == HOLDING_REGISTER ? "holding" :
             Type == INPUT_REGISTER ? "input" :
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

inline ::std::ostream& operator<<(::std::ostream& os, const TModbusRegister& reg) {
    return os << reg.ToString();
}

namespace std {
    template <> struct hash<TModbusRegister> {
        size_t operator()(const TModbusRegister& p) const {
            return hash<int>()(p.Slave) ^ hash<int>()(p.Type) ^ hash<int>()(p.Address);
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

typedef std::function<void(const TModbusRegister& reg)> TModbusCallback;

class TModbusClient
{
public:
    TModbusClient(const TModbusConnectionSettings& settings,
                  PModbusConnector connector = 0);
    TModbusClient(const TModbusClient& client) = delete;
    TModbusClient& operator=(const TModbusClient&) = delete;
    ~TModbusClient();
    void AddRegister(const TModbusRegister& reg);
    void Connect();
    void Disconnect();
    void Cycle();
    void SetRawValue(const TModbusRegister& reg, int value);
    void SetScaledValue(const TModbusRegister& reg, double value);
    void SetTextValue(const TModbusRegister& reg, const std::string& value);
    int GetRawValue(const TModbusRegister& reg) const;
    double GetScaledValue(const TModbusRegister& reg) const;
    std::string GetTextValue(const TModbusRegister& reg) const;
    bool DidRead(const TModbusRegister& reg) const;
    void SetCallback(const TModbusCallback& callback);
    void SetPollInterval(int ms);
    void SetModbusDebug(bool debug);
    bool DebugEnabled() const;
    void WriteHoldingRegister(int slave, int address, uint16_t value);

private:
    const std::unique_ptr<TRegisterHandler>& GetHandler(const TModbusRegister&) const;
    TRegisterHandler* CreateRegisterHandler(const TModbusRegister& reg);
    std::map< TModbusRegister, std::unique_ptr<TRegisterHandler> > handlers;
    PModbusContext Context;
    bool Active;
    int PollInterval;
    const int MAX_REGS = 65536;
    TModbusCallback Callback;
    bool Debug = false;
};

typedef std::shared_ptr<TModbusClient> PModbusClient;
