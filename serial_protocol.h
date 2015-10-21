#pragma once

#include <string>
#include <exception>
#include <stdint.h>

#include "portsettings.h"
#include "regformat.h"

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

class TSerialProtocol {
public:
    virtual ~TSerialProtocol();
    void Open();
    void Close();
    bool IsOpen() const;

    virtual uint64_t ReadRegister(uint8_t mod, uint8_t address, RegisterFormat fmt) = 0;
    virtual void WriteRegister(uint8_t mod, uint8_t address, uint64_t value, RegisterFormat fmt) = 0;
    // XXX FIXME: leaky abstraction (need to refactor)
    // Perhaps add 'brightness' register format
    virtual void SetBrightness(uint8_t mod, uint8_t address, uint8_t value) = 0;

protected:
    TSerialProtocol(const TSerialPortSettings& settings, bool debug = false);
    void EnsurePortOpen();
    void WriteBytes(uint8_t* buf, int count);
    uint8_t ReadByte();
    void SkipNoise();

private:
    void SerialPortSetup();
    bool Select(int ms);

    TSerialPortSettings Settings;
    bool Debug;
    int Fd;
    const int NoiseTimeoutMs = 10;
};
