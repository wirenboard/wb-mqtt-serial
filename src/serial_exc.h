#pragma once

#include <string>
#include <stdexcept>

std::string FormatErrno(int errnoValue);

class TSerialDeviceException: public std::runtime_error
{
public:
    TSerialDeviceException(const std::string& message);
};

class TSerialDeviceErrnoException: public TSerialDeviceException
{
    int ErrnoValue;
public:
    TSerialDeviceErrnoException(const std::string& message, int errnoValue);

    int GetErrnoValue() const;
};

class TSerialDeviceTransientErrorException: public TSerialDeviceException
{
public:
    TSerialDeviceTransientErrorException(const std::string& message);
};

class TSerialDevicePermanentRegisterException: public TSerialDeviceException
{
public:
	TSerialDevicePermanentRegisterException(const std::string& message);
};
