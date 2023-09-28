#pragma once

#include <stdexcept>
#include <string>

std::string FormatErrno(int errnoValue);

/** The exception class should be used for indicating any error during interaction with device.
 *  Future polling result and polling of other registers depends on type of exception.
 *  Polling of other registers of the device should be stopped until next poll session.
 *  Derived classes could define more precise polling policy.
 *  Current register read should be marked as read error.
 *  Example: read() error.
 */
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

/** The exception class should be used for indicating reception of a invalid answer from device.
 *  Future polling can be successful.
 *  Polling of other registers of the device is allowed.
 *  Current register read should be marked as read error.
 *  Current register write should be marked as write error, retry to write.
 *  Example: crc error, answer timeout.
 */
class TSerialDeviceTransientErrorException: public TSerialDeviceException
{
public:
    TSerialDeviceTransientErrorException(const std::string& message);
};

/** The exception class should be used for indicating reception of a valid answer from device,
 *  but with information about internal device's error.
 *  Future polling can be successful.
 *  Polling of other registers of the device is allowed.
 *  Current register read should be marked as read error.
 *  Current register write should be marked as write error, but do not retry to write.
 *  Example: reading curtain motor position returns invalid position due to motor misconfiguration.
 */
class TSerialDeviceInternalErrorException: public TSerialDeviceTransientErrorException
{
public:
    TSerialDeviceInternalErrorException(const std::string& message);
};

/** The exception class should be used for indicating reception of a valid answer from device,
 *  but with information about error.
 *  The error indicates that the register is unsupported by the device
 *  Future polling will lead to the same error.
 *  Polling of other registers of the device is allowed,
 *  Current register read should be marked as read error.
 *  Register should be marked as unavailable
 *  and excluded from poll until next connection to device.
 */
class TSerialDevicePermanentRegisterException: public TSerialDeviceException
{
public:
    TSerialDevicePermanentRegisterException(const std::string& message);
};

//! Response timeout expired exception
class TResponseTimeoutException: public TSerialDeviceTransientErrorException
{
public:
    TResponseTimeoutException();
};
