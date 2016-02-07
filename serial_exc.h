#pragma once
#include <string>
#include <stdexcept>

class TSerialDeviceException: public std::runtime_error {
public:
    TSerialDeviceException(std::string message): std::runtime_error("Serial protocol error: " + message) {}
};

class TSerialDeviceTransientErrorException: public TSerialDeviceException {
public:
    TSerialDeviceTransientErrorException(std::string message): TSerialDeviceException(message) {}
    // XXX gcc-4.7 is too old for this:
    // using TSerialDeviceException::TSerialDeviceException;
};
