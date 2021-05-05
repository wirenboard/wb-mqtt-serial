#include "serial_exc.h"
#include <string.h>

std::string FormatErrno(int errnoValue)
{
    return std::string(strerror(errnoValue)) + " (" + std::to_string(errnoValue) + ")";
}

TSerialDeviceException::TSerialDeviceException(const std::string& message)
    : std::runtime_error("Serial protocol error: " + message) 
{}


TSerialDeviceErrnoException::TSerialDeviceErrnoException(const std::string& message, int errnoValue): 
    TSerialDeviceException(message + FormatErrno(errnoValue)),
    ErrnoValue(errnoValue)
{}

int TSerialDeviceErrnoException::GetErrnoValue() const
{
    return ErrnoValue;
}


TSerialDeviceTransientErrorException::TSerialDeviceTransientErrorException(const std::string& message)
    : TSerialDeviceException(message)
{}

TSerialDevicePermanentRegisterException::TSerialDevicePermanentRegisterException(const std::string& message)
    : TSerialDeviceException(message)
{}
