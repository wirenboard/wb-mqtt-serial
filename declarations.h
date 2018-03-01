#pragma once

#include "types.h"

#include <chrono>
#include <memory>
#include <list>


class IProtocol;
class TSerialDevice;
class TBinarySemaphore;
class TProtocolRegister;
class TAbstractVirtualRegister;
class TVirtualRegister;
class TVirtualRegisterSet;
struct TRegisterConfig;
struct TIRDeviceQuerySet;
struct TIRDeviceQuery;
struct TIRDeviceValueQuery;
struct TProtocolInfo;
struct TDeviceChannel;

/* protocol register - local bit shifts for virtual register */ //TODO: move somewhere else
struct TRegisterBindInfo
{
    enum EReadState {NotRead, ReadOk, ReadError};

    uint8_t     BitStart,
                BitEnd;
    EReadState  IsRead = NotRead;

    inline uint8_t BitCount() const
    {
        return BitEnd - BitStart;
    }
};

struct TRegisterType {
    TRegisterType(int index, const std::string& name, const std::string& defaultControlType,
                  ERegisterFormat defaultFormat = U16,
                  bool read_only = false, EWordOrder defaultWordOrder = EWordOrder::BigEndian):
        Index(index), Name(name), DefaultControlType(defaultControlType),
        DefaultFormat(defaultFormat), DefaultWordOrder(defaultWordOrder), ReadOnly(read_only) {}
    int Index;
    std::string Name, DefaultControlType;
    ERegisterFormat DefaultFormat;
    EWordOrder DefaultWordOrder;
    bool ReadOnly;
};


using TTimePoint                = std::chrono::steady_clock::time_point;
using TIntervalMs               = std::chrono::milliseconds;
using PProtocol                 = std::shared_ptr<IProtocol>;
using PSerialDevice             = std::shared_ptr<TSerialDevice>;
using PWSerialDevice            = std::weak_ptr<TSerialDevice>;
using PBinarySemaphore          = std::shared_ptr<TBinarySemaphore>;
using PProtocolRegister         = std::shared_ptr<TProtocolRegister>;
using PAbstractVirtualRegister  = std::shared_ptr<TAbstractVirtualRegister>;
using PVirtualRegister          = std::shared_ptr<TVirtualRegister>;
using PWVirtualRegister         = std::weak_ptr<TVirtualRegister>;
using PVirtualRegisterSet       = std::shared_ptr<TVirtualRegisterSet>;
using PWVirtualRegisterSet      = std::weak_ptr<TVirtualRegisterSet>;
using PRegisterConfig           = std::shared_ptr<TRegisterConfig>;
using PIRDeviceQuerySet         = std::shared_ptr<TIRDeviceQuerySet>;
using PIRDeviceQuery            = std::shared_ptr<TIRDeviceQuery>;
using PIRDeviceValueQuery       = std::shared_ptr<TIRDeviceValueQuery>;
using TQueries                  = std::list<PIRDeviceQuery>;   // allow queries in set have common registers
using PDeviceChannel            = std::shared_ptr<TDeviceChannel>;
