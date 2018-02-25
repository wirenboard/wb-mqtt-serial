#pragma once

#include <chrono>
#include <memory>


class IProtocol;
class TSerialDevice;
class TBinarySemaphore;
class TProtocolRegister;
class TVirtualRegister;
class TVirtualRegisterSet;
struct TRegisterConfig;
struct TIRDeviceQuerySet;
struct TIRDeviceQuery;
struct TIRDeviceValueQuery;
struct TProtocolInfo;

/* protocol register - local bit shifts for virtual register */
struct TRegisterBindInfo
{
    uint8_t BitStart,
            BitEnd;
    bool    IsRead = false;

    inline uint8_t BitCount() const
    {
        return BitEnd - BitStart;
    }
};

using TTimePoint             = std::chrono::steady_clock::time_point;
using TIntervalMs            = std::chrono::milliseconds;
using PProtocol              = std::shared_ptr<IProtocol>;
using PSerialDevice          = std::shared_ptr<TSerialDevice>;
using PBinarySemaphore       = std::shared_ptr<TBinarySemaphore>;
using PProtocolRegister      = std::shared_ptr<TProtocolRegister>;
using PVirtualRegister       = std::shared_ptr<TVirtualRegister>;
using PVirtualRegisterSet    = std::shared_ptr<TVirtualRegisterSet>;
using PRegisterConfig        = std::shared_ptr<TRegisterConfig>;
using PIRDeviceQuerySet      = std::shared_ptr<TIRDeviceQuerySet>;
using PIRDeviceQuery         = std::shared_ptr<TIRDeviceQuery>;
using PIRDeviceValueQuery    = std::shared_ptr<TIRDeviceValueQuery>;
