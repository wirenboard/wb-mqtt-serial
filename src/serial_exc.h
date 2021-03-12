#pragma once
#include <string>
#include <stdexcept>
#include <string.h>

class TSerialDeviceException: public std::runtime_error {
public:
    TSerialDeviceException(const std::string& message): std::runtime_error("Serial protocol error: " + message) {}
};

class TSerialDeviceErrnoException: public TSerialDeviceException {
    int ErrnoValue;
public:
    TSerialDeviceErrnoException(const std::string& message, int errnoValue): 
        TSerialDeviceException(message + strerror(errnoValue) + " (" + std::to_string(errnoValue) + ")"),
        ErrnoValue(errnoValue)
    {}

    int GetErrnoValue() const
    {
        return ErrnoValue;
    }
};

class TSerialDeviceTransientErrorException: public TSerialDeviceException {
public:
    TSerialDeviceTransientErrorException(const std::string& message): TSerialDeviceException(message) {}
    // XXX gcc-4.7 is too old for this:
    // using TSerialDeviceException::TSerialDeviceException;
};

class TSerialDevicePermanentRegisterException: public TSerialDeviceException {
public:
	TSerialDevicePermanentRegisterException(const std::string& message): TSerialDeviceException(message) {}
};
