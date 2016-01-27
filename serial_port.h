#pragma once
#include <memory>
#include <modbus/modbus.h>
#include "portsettings.h"

struct TLibModbusContext {
    TLibModbusContext(const TSerialPortSettings& settings);
    ~TLibModbusContext() { modbus_free(Inner); }
    modbus_t* Inner;
};

typedef std::shared_ptr<TLibModbusContext> PLibModbusContext;

const int DefaultFrameTimeoutMs = 15;

class TAbstractSerialPort: public std::enable_shared_from_this<TAbstractSerialPort> {
public:
    typedef std::function<bool(uint8_t* buf, int size)> TFrameCompletePred;

    TAbstractSerialPort() {}
    TAbstractSerialPort(const TAbstractSerialPort&) = delete;
    TAbstractSerialPort& operator=(const TAbstractSerialPort&) = delete;
    virtual ~TAbstractSerialPort();
    virtual void SetDebug(bool debug) = 0;
    virtual bool Debug() const = 0;
    virtual void Open() = 0;
    virtual void Close() = 0;
    virtual bool IsOpen() const = 0;
    virtual void CheckPortOpen() = 0;
    virtual void WriteBytes(const uint8_t* buf, int count) = 0;
    virtual uint8_t ReadByte() = 0;
    virtual int ReadFrame(uint8_t* buf, int count, int timeout = -1, TFrameCompletePred frame_complete = 0) = 0;
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
    bool Debug() const;
    void CheckPortOpen();
    void WriteBytes(const uint8_t* buf, int count);
    uint8_t ReadByte();
    int ReadFrame(uint8_t* buf, int count, int timeout, TFrameCompletePred frame_complete = 0);
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
    bool Dbg;
    int Fd;
    const int NoiseTimeoutMs = 10;
};

typedef std::shared_ptr<TSerialPort> PSerialPort;
