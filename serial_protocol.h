#pragma once

#include <vector>
#include <unordered_map>
#include <string>
#include <memory>
#include <exception>
#include <stdint.h>
#include <modbus/modbus.h>

#include "portsettings.h"
#include "register.h"
#include "serial_config.h"

struct TLibModbusContext {
    TLibModbusContext(const TSerialPortSettings& settings);
    ~TLibModbusContext() { modbus_free(Inner); }
    modbus_t* Inner;
};

typedef std::shared_ptr<TLibModbusContext> PLibModbusContext;

class TSerialProtocolException: public std::exception {
public:
    TSerialProtocolException(std::string message): Message("Serial protocol error: " + message) {}
    const char* what () const throw ()
    {
        return Message.c_str();
    }

private:
    std::string Message;
};

class TSerialProtocolTransientErrorException: public TSerialProtocolException {
public:
    TSerialProtocolTransientErrorException(std::string message): TSerialProtocolException(message) {}
};

const int FrameTimeoutMs = 15;

class TAbstractSerialPort: public std::enable_shared_from_this<TAbstractSerialPort> {
public:
    TAbstractSerialPort() {}
    TAbstractSerialPort(const TAbstractSerialPort&) = delete;
    TAbstractSerialPort& operator=(const TAbstractSerialPort&) = delete;
    virtual ~TAbstractSerialPort();
    virtual void SetDebug(bool debug) = 0;
    virtual void Open() = 0;
    virtual void Close() = 0;
    virtual bool IsOpen() const = 0;
    virtual void CheckPortOpen() = 0;
    virtual void WriteBytes(const uint8_t* buf, int count) = 0;
    virtual uint8_t ReadByte() = 0;
    virtual int ReadFrame(uint8_t* buf, int count, int timeout = FrameTimeoutMs) = 0;
    virtual void SkipNoise() =0;
    virtual void USleep(int usec) = 0;
    virtual PLibModbusContext LibModbusContext() const = 0;
};

typedef std::shared_ptr<TAbstractSerialPort> PAbstractSerialPort;

class TSerialPort: public TAbstractSerialPort {
public:
    TSerialPort(const TSerialPortSettings& settings);
    ~TSerialPort();
    void SetDebug(bool debug);
    void CheckPortOpen();
    void WriteBytes(const uint8_t* buf, int count);
    uint8_t ReadByte();
    int ReadFrame(uint8_t* buf, int count, int timeout);
    void SkipNoise();
    void USleep(int usec);
    void Open();
    void Close();
    bool IsOpen() const;
    PLibModbusContext LibModbusContext() const;
    bool Select(int ms);

private:
    TSerialPortSettings Settings;
    PLibModbusContext Context;
    bool Debug;
    int Fd;
    const int NoiseTimeoutMs = 10;
};

typedef std::shared_ptr<TSerialPort> PSerialPort;

class TSerialProtocol: public std::enable_shared_from_this<TSerialProtocol> {
public:
    TSerialProtocol(PAbstractSerialPort port);
    TSerialProtocol(const TSerialProtocol&) = delete;
    TSerialProtocol& operator=(const TSerialProtocol&) = delete;
    virtual ~TSerialProtocol();

    virtual uint64_t ReadRegister(PRegister reg) = 0;
    virtual void WriteRegister(PRegister reg, uint64_t value) = 0;
    virtual void EndPollCycle();

protected:
    PAbstractSerialPort Port() { return SerialPort; }

private:
    PAbstractSerialPort SerialPort;
};

typedef std::shared_ptr<TSerialProtocol> PSerialProtocol;

typedef PSerialProtocol (*TSerialProtocolMaker)(PDeviceConfig device_config,
                                                PAbstractSerialPort port);

struct TSerialProtocolEntry {
    TSerialProtocolEntry(TSerialProtocolMaker maker, PRegisterTypeMap register_types):
        Maker(maker), RegisterTypes(register_types) {}
    TSerialProtocolMaker Maker;
    PRegisterTypeMap RegisterTypes;
};

class TSerialProtocolFactory {
public:
    TSerialProtocolFactory() = delete;
    static void RegisterProtocol(const std::string& name, TSerialProtocolMaker maker,
                                 const TRegisterTypes& register_types);
    static PRegisterTypeMap GetRegisterTypes(PDeviceConfig device_config);
    static PSerialProtocol CreateProtocol(PDeviceConfig device_config, PAbstractSerialPort port);

private:
    static const TSerialProtocolEntry& GetProtocolEntry(PDeviceConfig device_config);
    static std::unordered_map<std::string, TSerialProtocolEntry> *Protocols;
};

template<class Proto>
class TSerialProtocolRegistrator {
public:
    TSerialProtocolRegistrator(const std::string& name, const TRegisterTypes& register_types)
    {
        TSerialProtocolFactory::RegisterProtocol(
            name, [](PDeviceConfig device_config, PAbstractSerialPort port) {
                return PSerialProtocol(new Proto(device_config, port));
            }, register_types);
    }
};

#define REGISTER_PROTOCOL(name, cls, regTypes) \
    TSerialProtocolRegistrator<cls> reg__##cls(name, regTypes)
