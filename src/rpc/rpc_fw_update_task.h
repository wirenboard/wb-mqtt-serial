#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "rpc_fw_downloader.h"
#include "serial_client.h"

namespace Modbus
{
    class IModbusTraits;
}

// Register addresses and counts for WB device firmware operations
namespace FwRegisters
{
    // Read registers (function 3 = READ_HOLDING)
    const uint16_t FW_SIGNATURE_ADDR = 290;
    const uint16_t FW_SIGNATURE_COUNT = 12;

    const uint16_t FW_VERSION_ADDR = 250;
    const uint16_t FW_VERSION_COUNT = 16;

    const uint16_t BOOTLOADER_VERSION_ADDR = 330;
    const uint16_t BOOTLOADER_VERSION_COUNT = 7;
    const uint16_t BOOTLOADER_VERSION_FULL_COUNT = 8;

    const uint16_t DEVICE_MODEL_EXTENDED_ADDR = 200;
    const uint16_t DEVICE_MODEL_EXTENDED_COUNT = 20;
    const uint16_t DEVICE_MODEL_ADDR = 200;
    const uint16_t DEVICE_MODEL_COUNT = 6;

    const uint16_t SN_ADDR = 270;
    const uint16_t SN_COUNT = 2;

    // Reboot registers (function 6 = WRITE_SINGLE_REGISTER)
    const uint16_t REBOOT_PRESERVE_PORT_SETTINGS_ADDR = 131;
    const uint16_t REBOOT_TO_BOOTLOADER_ADDR = 129;

    // Firmware flash registers (function 16 = WRITE_MULTIPLE_REGISTERS)
    const uint16_t FW_INFO_BLOCK_ADDR = 0x1000;
    const uint16_t FW_INFO_BLOCK_COUNT = 16;
    const uint16_t FW_DATA_BLOCK_ADDR = 0x2000;
    const uint16_t FW_DATA_BLOCK_COUNT = 68;

    // Components (function 2 = READ_DISCRETE for presence, function 4 = READ_INPUT for details)
    const uint16_t COMPONENTS_PRESENCE_ADDR = 65152;
    const uint16_t COMPONENTS_PRESENCE_COUNT = 8;

    // Component step parameters (function 4 = READ_INPUT)
    const uint16_t COMPONENT_SIGNATURE_BASE = 64788;
    const uint16_t COMPONENT_SIGNATURE_COUNT = 12;
    const uint16_t COMPONENT_FW_VERSION_BASE = 64800;
    const uint16_t COMPONENT_FW_VERSION_COUNT = 16;
    const uint16_t COMPONENT_MODEL_BASE = 64768;
    const uint16_t COMPONENT_MODEL_COUNT = 20;
    const uint16_t COMPONENT_STEP = 48;
}

struct TFwDeviceInfo
{
    std::string FwSignature;
    std::string FwVersion;
    std::string BootloaderVersion;
    bool CanPreservePortSettings = false;
    std::string DeviceModel;

    struct TComponentInfo
    {
        int Number;
        std::string Signature;
        std::string FwVersion;
        std::string Model;
    };
    std::vector<TComponentInfo> Components;
};

// Free functions for serial operations — used by task classes and higher-level tasks
std::unique_ptr<Modbus::IModbusTraits> MakeModbusTraits(const std::string& protocol);
TFwDeviceInfo ReadFwDeviceInfo(TPort& port, Modbus::IModbusTraits& traits, uint8_t slaveId);
void FlashFirmware(TPort& port,
                   Modbus::IModbusTraits& traits,
                   uint8_t slaveId,
                   const TParsedWBFW& firmware,
                   bool rebootToBootloader,
                   bool canPreservePortSettings,
                   std::function<void(int)> onProgress);

using TFwGetInfoCallback = std::function<void(const TFwDeviceInfo& info)>;
using TFwGetInfoErrorCallback = std::function<void(const std::string& error)>;

class TFwGetInfoTask: public ISerialClientTask
{
public:
    TFwGetInfoTask(uint8_t slaveId,
                   const std::string& protocol,
                   TFwGetInfoCallback onResult,
                   TFwGetInfoErrorCallback onError);

    ISerialClientTask::TRunResult Run(PFeaturePort port,
                                      TSerialClientDeviceAccessHandler& lastAccessedDevice,
                                      const std::list<PSerialDevice>& polledDevices) override;

private:
    uint8_t SlaveId;
    std::string Protocol;
    TFwGetInfoCallback OnResult;
    TFwGetInfoErrorCallback OnError;
    std::chrono::steady_clock::time_point ExpireTime;
};

using TFwFlashProgressCallback = std::function<void(int percent)>;
using TFwFlashCompleteCallback = std::function<void()>;
using TFwFlashErrorCallback = std::function<void(const std::string& error)>;

class TFwFlashTask: public ISerialClientTask
{
public:
    TFwFlashTask(uint8_t slaveId,
                 const std::string& protocol,
                 TParsedWBFW firmware,
                 bool rebootToBootloader,
                 bool canPreservePortSettings,
                 TFwFlashProgressCallback onProgress,
                 TFwFlashCompleteCallback onComplete,
                 TFwFlashErrorCallback onError);

    ISerialClientTask::TRunResult Run(PFeaturePort port,
                                      TSerialClientDeviceAccessHandler& lastAccessedDevice,
                                      const std::list<PSerialDevice>& polledDevices) override;

private:
    uint8_t SlaveId;
    std::string Protocol;
    TParsedWBFW Firmware;
    bool RebootToBootloader;
    bool CanPreservePortSettings;
    TFwFlashProgressCallback OnProgress;
    TFwFlashCompleteCallback OnComplete;
    TFwFlashErrorCallback OnError;
};
