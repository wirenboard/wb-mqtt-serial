#pragma once
#include <string>
#include <stdexcept>

class TSerialProtocolException: public std::runtime_error {
public:
    TSerialProtocolException(std::string message): std::runtime_error("Serial protocol error: " + message) {}
};

class TSerialProtocolTransientErrorException: public TSerialProtocolException {
public:
    using TSerialProtocolException::TSerialProtocolException;
};
