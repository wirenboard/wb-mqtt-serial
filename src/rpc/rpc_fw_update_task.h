#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "port/serial_port_settings.h"
#include "rpc_fw_downloader.h"
#include "serial_client.h"

namespace Modbus
{
    class IModbusTraits;
}

const TSerialPortConnectionSettings FACTORY_PORT_SETTINGS(9600, 'N', 8, 2);

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
TFwDeviceInfo ReadFwDeviceInfo(Modbus::IModbusTraits& traits, TPort& port, uint8_t slaveId);

//! A bootloader which cannot preserve the port settings starts with the factory ones, and the
//! line behind a Serial over TCP port cannot be switched to them by the driver
bool RequiresDefaultPortSettings(bool tcpPort, const std::string& protocol, bool canPreservePortSettings);

//! Asks the device for its baud rate and parity, the factory settings are 9600 without parity
bool HasDefaultPortSettings(Modbus::IModbusTraits& traits, TPort& port, uint8_t slaveId);

/**
 * @brief A bootloader gives its version only as a whole, a firmware answers a read of any number
 *        of the version registers. A device which does not answer at all is not in the bootloader.
 *
 * @throws Modbus::TModbusExceptionError if the device answers the read with an error
 */
bool IsInBootloaderMode(Modbus::IModbusTraits& traits, TPort& port, uint8_t slaveId);
void FlashFirmware(Modbus::IModbusTraits& traits,
                   TPort& port,
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
