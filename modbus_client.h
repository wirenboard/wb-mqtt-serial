#pragma once

#include <map>
#include <list>
#include <string>
#include <vector>
#include <memory>
#include <exception>
#include <functional>

// #include <modbus/modbus.h>

#include "regformat.h"
#include "portsettings.h"
#include "modbus_config.h"

class TRegisterHandler;

class TModbusContext
{
public:
    virtual ~TModbusContext();
    virtual void Connect() = 0;
    virtual void Disconnect() = 0;
    virtual void AddDevice(PDeviceConfig device_config) = 0;
    virtual void SetDebug(bool debug) = 0;
    virtual void SetSlave(int slave) = 0;
    virtual void ReadCoils(int addr, int nb, uint8_t *dest) = 0;
    virtual void WriteCoil(int addr, int value) = 0;
    virtual void ReadDisceteInputs(int addr, int nb, uint8_t *dest) = 0;
    virtual void ReadHoldingRegisters(int addr, int nb, uint16_t *dest) = 0;
    virtual void WriteHoldingRegisters(int addr, int nb, const uint16_t *data) = 0;
    virtual void WriteHoldingRegister(int addr, uint16_t value) = 0;
    virtual void ReadInputRegisters(int addr, int nb, uint16_t *dest) = 0;
    virtual void ReadDirectRegister(int addr, uint64_t* dest, RegisterFormat format, size_t width) = 0;
    virtual void WriteDirectRegister(int addr, uint64_t value, RegisterFormat format) = 0;
    virtual void EndPollCycle(int usecDelay) = 0;
};

typedef std::shared_ptr<TModbusContext> PModbusContext;

class TModbusConnector: public std::enable_shared_from_this<TModbusConnector>
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

class TModbusClient
{
public:
    enum TErrorState {
        NoError,
        WriteError,
        ReadError,
        ReadWriteError,
        UnknownErrorState,
        ErrorStateUnchanged
    };
    typedef std::function<void(std::shared_ptr<TModbusRegister> reg)> TCallback;
    typedef std::function<void(std::shared_ptr<TModbusRegister> reg, TErrorState errorState)> TErrorCallback;

    TModbusClient(const TSerialPortSettings& settings,
                  PModbusConnector connector = 0);
    TModbusClient(const TModbusClient& client) = delete;
    TModbusClient& operator=(const TModbusClient&) = delete;
    ~TModbusClient();
    void AddDevice(PDeviceConfig device_config);
    void AddRegister(std::shared_ptr<TModbusRegister> reg);
    void Connect();
    void Disconnect();
    void Cycle();
    void SetTextValue(std::shared_ptr<TModbusRegister> reg, const std::string& value);
    std::string GetTextValue(std::shared_ptr<TModbusRegister> reg) const;
    bool DidRead(std::shared_ptr<TModbusRegister> reg) const;
    void SetCallback(const TCallback& callback);
    void SetErrorCallback(const TErrorCallback& callback);
    void SetPollInterval(int ms);
    void SetModbusDebug(bool debug);
    bool DebugEnabled() const;
    void WriteHoldingRegister(int slave, int address, uint16_t value);

private:
    const std::unique_ptr<TRegisterHandler>& GetHandler(std::shared_ptr<TModbusRegister>) const;
    TRegisterHandler* CreateRegisterHandler(std::shared_ptr<TModbusRegister> reg);
    void MaybeUpdateErrorState(std::shared_ptr<TModbusRegister> reg, TErrorState state);

    std::map<std::shared_ptr<TModbusRegister>, std::unique_ptr<TRegisterHandler> > Handlers;
    std::list<PDeviceConfig> DevicesToAdd;
    PModbusContext Context;
    bool Active;
    int PollInterval;
    const int MAX_REGS = 65536;
    TCallback Callback;
    TErrorCallback ErrorCallback;
    bool Debug = false;
};

typedef std::shared_ptr<TModbusClient> PModbusClient;

