#pragma once

#include <unordered_map>
#include <string>
#include <memory>
#include <exception>
#include <stdint.h>

#include "portsettings.h"
#include "regformat.h"
#include "modbus_config.h"

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

private:
    void SerialPortSetup();
    bool Select(int ms);

    TSerialPortSettings Settings;
    bool Debug;
    int Fd;
    const int NoiseTimeoutMs = 10;
};

class TSerialProtocol: public std::enable_shared_from_this<TSerialProtocol> {
public:
    TSerialProtocol(PAbstractSerialPort port);
    virtual ~TSerialProtocol();

    virtual uint64_t ReadRegister(uint32_t mod, uint32_t address, RegisterFormat fmt, size_t width = 0) = 0;
    virtual void WriteRegister(uint32_t mod, uint32_t address, uint64_t value, RegisterFormat fmt) = 0;
    // XXX FIXME: leaky abstraction (need to refactor)
    // Perhaps add 'brightness' register format
    virtual void SetBrightness(uint32_t mod, uint32_t address, uint8_t value) = 0;
    virtual void EndPollCycle();

protected:
    PAbstractSerialPort Port() { return SerialPort; }

private:
    PAbstractSerialPort SerialPort;
};

typedef std::shared_ptr<TSerialProtocol> PSerialProtocol;

class TSerialProtocolFactory {
public:
    typedef PSerialProtocol (*TSerialProtocolMaker)(PDeviceConfig device_config,
                                                    PAbstractSerialPort port);
    static void RegisterProtocol(const std::string& name, TSerialProtocolMaker maker);
    static PSerialProtocol CreateProtocol(PDeviceConfig device_config, PAbstractSerialPort port);

private:
    static std::unordered_map<std::string, TSerialProtocolMaker> *ProtoMakers;
};

template<class Proto>
class TSerialProtocolRegistrator {
public:
    TSerialProtocolRegistrator(const std::string name)
    {
        TSerialProtocolFactory::RegisterProtocol(
            name, [](PDeviceConfig device_config, PAbstractSerialPort port) {
                return PSerialProtocol(new Proto(device_config, port));
            });
    }
};

#define REGISTER_PROTOCOL(name, cls) TSerialProtocolRegistrator<cls> reg__##cls(name)
