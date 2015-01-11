#pragma once

#include <string>
#include <exception>
#include <stdint.h>

class TUnielBusException: public std::exception {
public:
    TUnielBusException(std::string message): Message("Uniel bus error: " + message) {}
    const char* what () const throw ()
    {
        return Message.c_str();
    }

private:
    std::string Message;
};

class TUnielBusTransientErrorException: public TUnielBusException {
public:
    TUnielBusTransientErrorException(std::string message): TUnielBusException(message) {}
};

class TUnielBus {
public:
    static const int DefaultTimeoutMs = 1000;

    TUnielBus(const std::string& device, int timeout_ms = DefaultTimeoutMs);
    ~TUnielBus();
    void Open();
    void Close();
    bool IsOpen() const;
    uint8_t ReadRegister(uint8_t mod, uint8_t address);
    void WriteRegister(uint8_t mod, uint8_t address, uint8_t value);
    void SetBrightness(uint8_t mod, uint8_t address, uint8_t value);

private:
    void EnsurePortOpen();
    void SerialPortSetup();
    void WriteCommand(uint8_t cmd, uint8_t mod, uint8_t b1, uint8_t b2, uint8_t b3);
    uint8_t ReadByte();
    void ReadResponse(uint8_t cmd, uint8_t* response);
    void DoWriteRegister(uint8_t cmd, uint8_t mod, uint8_t address, uint8_t value);

    std::string Device;
    int TimeoutMs;
    int Fd;
};
