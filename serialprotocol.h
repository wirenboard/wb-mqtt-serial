#pragma once

#include <string>
#include <exception>
#include <stdint.h>

#include "portsettings.h"

class TSerialProtocolException: public std::exception {
public:
    TSerialProtocolException(std::string message): Message("Uniel bus error: " + message) {}
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
    ~TSerialProtocol();
    void Open();
    void Close();
    bool IsOpen() const;

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
