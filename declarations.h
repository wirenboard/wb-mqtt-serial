#pragma once

#include <chrono>
#include <memory>


class IProtocol;
class TSerialDevice;
class TBinarySemaphore;
class TProtocolRegister;
class TVirtualRegister;
struct TRegisterConfig;
struct TIRDeviceReadQuery;
struct TIRDeviceWriteQuery;
struct TIRDeviceQueryEntry;
struct TIRDeviceReadQueryEntry;
struct TIRDeviceWriteQueryEntry;
struct TProtocolInfo;

using TTimePoint                = std::chrono::steady_clock::time_point;
using TIntervalMs               = std::chrono::milliseconds;
using PProtocol                 = std::shared_ptr<IProtocol>;
using PSerialDevice             = std::shared_ptr<TSerialDevice>;
using PBinarySemaphore          = std::shared_ptr<TBinarySemaphore>;
using PProtocolRegister         = std::shared_ptr<TProtocolRegister>;
using PVirtualRegister          = std::shared_ptr<TVirtualRegister>;
using PWVirtualRegister         = std::weak_ptr<TVirtualRegister>;
using PRegisterConfig           = std::shared_ptr<TRegisterConfig>;
using PIRDeviceReadQuery        = std::shared_ptr<TIRDeviceReadQuery>;
using PIRDeviceWriteQuery       = std::shared_ptr<TIRDeviceWriteQuery>;
using PIRDeviceQueryEntry       = std::shared_ptr<TIRDeviceQueryEntry>;
using PIRDeviceReadQueryEntry   = std::shared_ptr<TIRDeviceReadQueryEntry>;
using PIRDeviceWriteQueryEntry  = std::shared_ptr<TIRDeviceWriteQueryEntry>;
