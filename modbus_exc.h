#pragma once
#include <stdexcept>

class TModbusException: public std::exception {
public:
    TModbusException(std::string _message): message("Modbus error: " + _message) {}
    const char* what () const throw ()
    {
        return message.c_str();
    }

private:
    std::string message;
};


