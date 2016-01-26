#pragma once
#include <string>
#include <stdexcept>

class TSerialProtocolException: public std::runtime_error {
public:
    TSerialProtocolException(std::string message): std::runtime_error("Serial protocol error: " + message) {}
};

class TSerialProtocolTransientErrorException: public TSerialProtocolException {
public:
    TSerialProtocolTransientErrorException(std::string message): TSerialProtocolException(message) {}
    // XXX gcc-4.7 is too old for this:
    // using TSerialProtocolException::TSerialProtocolException;
};
