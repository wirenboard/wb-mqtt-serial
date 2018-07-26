#pragma once

#include "global_variables.h"
#include "utils.h"
#include "types.h"

#include <chrono>
#include <memory>
#include <vector>
#include <list>

class IProtocol;
class TPort;
class TSerialDevice;
class TSerialClient;
class TBinarySemaphore;
class TMemoryBlock;
class TIRBindInfo;
class TAbstractVirtualRegister;
class TVirtualRegister;
class TVirtualRegisterSet;
struct TRegisterConfig;
struct TDeviceConfig;
struct TDeviceSetupItem;
struct TIRDeviceQuerySet;
struct TIRDeviceQuery;
struct TIRDeviceValueQuery;
struct TIRDeviceValueDesc;
struct TIRValue;
struct TIRDeviceValueContext;
struct TProtocolInfo;
struct TDeviceChannel;
struct TMemoryBlockType;
struct IVirtualValue;

using TTimePoint                = std::chrono::steady_clock::time_point;
using TIntervalMs               = std::chrono::milliseconds;
using PProtocol                 = std::shared_ptr<IProtocol>;
using PPort                     = std::shared_ptr<TPort>;
using PSerialDevice             = std::shared_ptr<TSerialDevice>;
using PSerialClient             = std::shared_ptr<TSerialClient>;
using PWSerialDevice            = std::weak_ptr<TSerialDevice>;
using PBinarySemaphore          = std::shared_ptr<TBinarySemaphore>;
using PMemoryBlock              = std::shared_ptr<TMemoryBlock>;
using CPMemoryBlock             = std::shared_ptr<const TMemoryBlock>;
using PAbstractVirtualRegister  = std::shared_ptr<TAbstractVirtualRegister>;
using PVirtualRegister          = std::shared_ptr<TVirtualRegister>;
using PWVirtualRegister         = std::weak_ptr<TVirtualRegister>;
using PVirtualRegisterSet       = std::shared_ptr<TVirtualRegisterSet>;
using PWVirtualRegisterSet      = std::weak_ptr<TVirtualRegisterSet>;
using PRegisterConfig           = std::shared_ptr<TRegisterConfig>;
using PDeviceConfig             = std::shared_ptr<TDeviceConfig>;
using PDeviceSetupItem          = std::shared_ptr<TDeviceSetupItem>;
using PIRDeviceQuerySet         = std::shared_ptr<TIRDeviceQuerySet>;
using PIRDeviceQuery            = std::shared_ptr<TIRDeviceQuery>;
using PIRDeviceValueQuery       = std::shared_ptr<TIRDeviceValueQuery>;
using PIRValue                  = std::unique_ptr<TIRValue>;
using TQueries                  = std::list<PIRDeviceQuery>;   // allow queries in set have common registers
using PDeviceChannel            = std::shared_ptr<TDeviceChannel>;
using PVirtualValue             = std::shared_ptr<IVirtualValue>;
using TAssociatedMemoryBlockSet = std::pair<TPSet<PMemoryBlock>, std::vector<PVirtualValue>>;
