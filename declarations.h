#pragma once

#include <chrono>
#include <memory>


class IProtocol;
class TSerialDevice;
class TBinarySemaphore;
class TProtocolRegister;
class TVirtualRegister;
struct TRegisterConfig;
struct TIRDeviceQuerySet;
struct TIRDeviceQuery;
struct TIRDevice64BitQuery;
struct TIRDeviceSingleBitQuery;
struct TProtocolInfo;

using TTimePoint        = std::chrono::steady_clock::time_point;
using TIntervalMs       = std::chrono::milliseconds;
using PProtocol         = std::shared_ptr<IProtocol>;
using PSerialDevice     = std::shared_ptr<TSerialDevice>;
using PBinarySemaphore  = std::shared_ptr<TBinarySemaphore>;
using PProtocolRegister = std::shared_ptr<TProtocolRegister>;
using PVirtualRegister  = std::shared_ptr<TVirtualRegister>;
using PWVirtualRegister = std::weak_ptr<TVirtualRegister>;
using PRegisterConfig   = std::shared_ptr<TRegisterConfig>;
using PIRDeviceQuerySet = std::shared_ptr<TIRDeviceQuerySet>;
using PIRDeviceQuery    = std::shared_ptr<TIRDeviceQuery>;
