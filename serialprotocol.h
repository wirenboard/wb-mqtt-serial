#pragma once

#include <string>
#include <exception>
#include <stdint.h>

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
    static const int DefaultTimeoutMs = 1000;

    ~TSerialProtocol();
    void Open();
    void Close();
    bool IsOpen() const;

protected:
    TSerialProtocol(const std::string& device, int timeout_ms = DefaultTimeoutMs);
    void EnsurePortOpen();
    uint8_t ReadByte();

private:
    void SerialPortSetup();

    std::string Device;
    int TimeoutMs;
    int Fd;
};
