#include "rpc_fw_update_task.h"
#include "log.h"
#include "rpc_helpers.h"
#include "serial_exc.h"

#define LOG(logger) ::logger.Log() << "[fw-update] "

namespace
{
    const auto FW_RESPONSE_TIMEOUT = std::chrono::milliseconds(500);
    const auto FW_FRAME_TIMEOUT = std::chrono::milliseconds(20);
    const auto FW_REBOOT_TIMEOUT = std::chrono::milliseconds(1000);

    const size_t FW_BLOCK_SIZE = FwRegisters::FW_DATA_BLOCK_COUNT * 2; // 68 registers * 2 bytes = 136 bytes
    const int MAX_WRITE_RETRIES = 3;

    std::string ReadStringRegister(Modbus::IModbusTraits& traits,
                                   TPort& port,
                                   uint8_t slaveId,
                                   uint16_t addr,
                                   uint16_t regCount,
                                   bool holding = false)
    {
        auto config = TRegisterConfig::Create(holding ? Modbus::REG_HOLDING : Modbus::REG_INPUT,
                                              TRegisterDesc{std::make_shared<TUint32RegisterAddress>(addr),
                                                            0,
                                                            static_cast<uint32_t>(regCount * sizeof(char) * 8)},
                                              RegisterFormat::String);
        return Modbus::ReadRegister(traits,
                                    port,
                                    slaveId,
                                    *config,
                                    std::chrono::microseconds(0),
                                    FW_RESPONSE_TIMEOUT,
                                    FW_FRAME_TIMEOUT)
            .Get<std::string>();
    }

    void WriteSingleRegister(Modbus::IModbusTraits& traits,
                             TPort& port,
                             uint8_t slaveId,
                             uint16_t addr,
                             uint16_t value,
                             std::chrono::milliseconds responseTimeout)
    {
        auto config = TRegisterConfig::Create(Modbus::REG_HOLDING_SINGLE, addr, RegisterFormat::U16);
        Modbus::TRegisterCache cache;
        Modbus::WriteRegister(traits,
                              port,
                              slaveId,
                              *config,
                              TRegisterValue(static_cast<uint64_t>(value)),
                              cache,
                              std::chrono::microseconds(0),
                              responseTimeout,
                              FW_FRAME_TIMEOUT);
    }

    // Cf. firmware_update.py:412 reboot_to_bootloader()
    void RebootToBootloader(Modbus::IModbusTraits& traits, TPort& port, uint8_t slaveId, bool canPreservePortSettings)
    {
        if (canPreservePortSettings) {
            WriteSingleRegister(traits,
                                port,
                                slaveId,
                                FwRegisters::REBOOT_PRESERVE_PORT_SETTINGS_ADDR,
                                1,
                                FW_REBOOT_TIMEOUT);
        } else {
            try {
                WriteSingleRegister(traits,
                                    port,
                                    slaveId,
                                    FwRegisters::REBOOT_TO_BOOTLOADER_ADDR,
                                    1,
                                    FW_REBOOT_TIMEOUT);
            } catch (const TResponseTimeoutException&) {
                // Device has rebooted and doesn't send response (expected for old firmware)
                LOG(Debug) << "Device doesn't respond to reboot command, probably it has rebooted";
            }
        }

        // Delay before going to bootloader
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

    // Cf. firmware_update.py:382 flash_fw() - info block write part
    void WriteInfo(Modbus::IModbusTraits& traits, TPort& port, uint8_t slaveId, const std::vector<uint8_t>& infoData)
    {
        auto infoBlockTimeout = std::chrono::milliseconds(1000);
        auto frameTimeout = std::chrono::milliseconds(20);

        auto pdu = Modbus::MakePDU(Modbus::FN_WRITE_MULTIPLE_REGISTERS,
                                   FwRegisters::FW_INFO_BLOCK_ADDR,
                                   FwRegisters::FW_INFO_BLOCK_COUNT,
                                   infoData);
        auto responsePduSize =
            Modbus::CalcResponsePDUSize(Modbus::FN_WRITE_MULTIPLE_REGISTERS, FwRegisters::FW_INFO_BLOCK_COUNT);
        port.SleepSinceLastInteraction(std::chrono::microseconds(0));
        traits.Transaction(port, slaveId, pdu, responsePduSize, infoBlockTimeout, frameTimeout);
    }

    // Cf. firmware_update.py:349 write_fw_data_block()
    void WriteDataBlock(Modbus::IModbusTraits& traits, TPort& port, uint8_t slaveId, const std::vector<uint8_t>& block)
    {
        uint16_t regCount = static_cast<uint16_t>(block.size() / 2);
        std::exception_ptr lastException;

        for (int i = 0; i < MAX_WRITE_RETRIES; ++i) {
            try {
                auto pdu = Modbus::MakePDU(Modbus::FN_WRITE_MULTIPLE_REGISTERS,
                                           FwRegisters::FW_DATA_BLOCK_ADDR,
                                           regCount,
                                           block);
                auto responsePduSize = Modbus::CalcResponsePDUSize(Modbus::FN_WRITE_MULTIPLE_REGISTERS, regCount);
                port.SleepSinceLastInteraction(std::chrono::microseconds(0));
                auto res =
                    traits.Transaction(port, slaveId, pdu, responsePduSize, FW_RESPONSE_TIMEOUT, FW_FRAME_TIMEOUT);
                Modbus::ExtractResponseData(Modbus::FN_WRITE_MULTIPLE_REGISTERS, res.Pdu);
                return; // Success
            } catch (const Modbus::TModbusExceptionError& e) {
                // Slave device failure (0x04) means block is already written — treat as success
                // Cf. firmware_update.py:349 write_fw_data_block()
                if (e.GetExceptionCode() == 0x04) {
                    return;
                }
                lastException = std::current_exception();
            } catch (const std::exception&) {
                lastException = std::current_exception();
            }
        }

        if (lastException) {
            std::rethrow_exception(lastException);
        }
    }

    void WriteData(Modbus::IModbusTraits& traits,
                   TPort& port,
                   uint8_t slaveId,
                   const std::vector<uint8_t>& firmwareData,
                   std::function<void(int)> onProgress)
    {
        if (firmwareData.empty()) {
            if (onProgress) {
                onProgress(100);
            }
            return;
        }

        size_t totalBlocks = (firmwareData.size() + FW_BLOCK_SIZE - 1) / FW_BLOCK_SIZE;
        for (size_t i = 0; i < totalBlocks; ++i) {
            size_t offset = i * FW_BLOCK_SIZE;
            size_t blockLen = std::min(FW_BLOCK_SIZE, firmwareData.size() - offset);
            std::vector<uint8_t> block(firmwareData.begin() + offset, firmwareData.begin() + offset + blockLen);

            // Pad to even length if needed
            if (block.size() % 2 != 0) {
                block.push_back(0xFF);
            }

            WriteDataBlock(traits, port, slaveId, block);

            if (onProgress) {
                onProgress(static_cast<int>((i + 1) * 100 / totalBlocks));
            }
        }
    }
}

// ============================================================
//                     Public free functions
// ============================================================

// Cf. firmware_update.py:311 FirmwareInfoReader.read()
TFwDeviceInfo ReadFwDeviceInfo(Modbus::IModbusTraits& traits, TPort& port, uint8_t slaveId)
{
    TFwDeviceInfo info;

    // Read firmware signature - Cf. firmware_update.py:252 read_fw_signature()
    info.FwSignature = ReadStringRegister(traits,
                                          port,
                                          slaveId,
                                          FwRegisters::FW_SIGNATURE_ADDR,
                                          FwRegisters::FW_SIGNATURE_COUNT,
                                          true); // holding

    // Read firmware version - Cf. firmware_update.py:255 read_fw_version()
    // In bootloader mode, firmware version registers may not be available
    try {
        info.FwVersion = ReadStringRegister(traits,
                                            port,
                                            slaveId,
                                            FwRegisters::FW_VERSION_ADDR,
                                            FwRegisters::FW_VERSION_COUNT,
                                            true); // holding
    } catch (const std::exception& e) {
        LOG(Debug) << "Cannot read firmware version: " << e.what();
    }

    // Read bootloader version - Cf. firmware_update.py:234 read_bootloader_info()
    try {
        info.BootloaderVersion = ReadStringRegister(traits,
                                                    port,
                                                    slaveId,
                                                    FwRegisters::BOOTLOADER_VERSION_ADDR,
                                                    FwRegisters::BOOTLOADER_VERSION_COUNT,
                                                    true); // holding
    } catch (const std::exception& e) {
        LOG(Debug) << "Cannot read bootloader version: " << e.what();
    }

    // Check if bootloader can preserve port settings (register 131 readable means yes)
    try {
        auto config = TRegisterConfig::Create(Modbus::REG_HOLDING,
                                              FwRegisters::REBOOT_PRESERVE_PORT_SETTINGS_ADDR,
                                              RegisterFormat::U16);
        auto val = Modbus::ReadRegister(traits,
                                        port,
                                        slaveId,
                                        *config,
                                        std::chrono::microseconds(0),
                                        FW_RESPONSE_TIMEOUT,
                                        FW_FRAME_TIMEOUT);
        info.CanPreservePortSettings = (val.Get<uint64_t>() == 0);
    } catch (const std::exception&) {
        info.CanPreservePortSettings = false;
    }

    // Read device model (try extended first, then standard) - Cf. firmware_update.py:574 read_device_model()
    try {
        info.DeviceModel = ReadStringRegister(traits,
                                              port,
                                              slaveId,
                                              FwRegisters::DEVICE_MODEL_EXTENDED_ADDR,
                                              FwRegisters::DEVICE_MODEL_EXTENDED_COUNT,
                                              true); // holding
    } catch (const std::exception&) {
        try {
            info.DeviceModel = ReadStringRegister(traits,
                                                  port,
                                                  slaveId,
                                                  FwRegisters::DEVICE_MODEL_ADDR,
                                                  FwRegisters::DEVICE_MODEL_COUNT,
                                                  true); // holding
        } catch (const std::exception& e) {
            LOG(Debug) << "Cannot read device model: " << e.what();
        }
    }

    // Clean up device model - remove 0x02 characters (like Python's replace("\x02", ""))
    info.DeviceModel.erase(std::remove(info.DeviceModel.begin(), info.DeviceModel.end(), '\x02'),
                           info.DeviceModel.end());

    // Read components presence (function 2 = READ_DISCRETE) - Cf. firmware_update.py:278 read_components_presence()
    // Using raw PDU here because ReadRegister doesn't support multi-bit discrete reads
    try {
        auto pdu = Modbus::MakePDU(Modbus::FN_READ_DISCRETE,
                                   FwRegisters::COMPONENTS_PRESENCE_ADDR,
                                   FwRegisters::COMPONENTS_PRESENCE_COUNT,
                                   {});
        auto responsePduSize =
            Modbus::CalcResponsePDUSize(Modbus::FN_READ_DISCRETE, FwRegisters::COMPONENTS_PRESENCE_COUNT);
        port.SleepSinceLastInteraction(std::chrono::microseconds(0));
        auto res = traits.Transaction(port, slaveId, pdu, responsePduSize, FW_RESPONSE_TIMEOUT, FW_FRAME_TIMEOUT);
        auto presenceData = Modbus::ExtractResponseData(Modbus::FN_READ_DISCRETE, res.Pdu);

        std::vector<int> presentComponents;
        for (size_t byteIdx = 0; byteIdx < presenceData.size(); ++byteIdx) {
            for (int bit = 0; bit < 8; ++bit) {
                if (presenceData[byteIdx] & (1 << bit)) {
                    presentComponents.push_back(static_cast<int>(byteIdx * 8 + bit));
                }
            }
        }

        // Read info for each present component - Cf. firmware_update.py:258 read_component_info()
        for (int compNum: presentComponents) {
            try {
                auto signature = ReadStringRegister(traits,
                                                    port,
                                                    slaveId,
                                                    FwRegisters::COMPONENT_SIGNATURE_BASE +
                                                        static_cast<uint16_t>(compNum) * FwRegisters::COMPONENT_STEP,
                                                    FwRegisters::COMPONENT_SIGNATURE_COUNT);
                // Check for unavailable component (all 0xFE in signature string)
                if (!signature.empty() && signature == std::string(signature.size(), '\xFE')) {
                    continue;
                }
                auto fwVer = ReadStringRegister(traits,
                                                port,
                                                slaveId,
                                                FwRegisters::COMPONENT_FW_VERSION_BASE +
                                                    static_cast<uint16_t>(compNum) * FwRegisters::COMPONENT_STEP,
                                                FwRegisters::COMPONENT_FW_VERSION_COUNT);
                auto model = ReadStringRegister(traits,
                                                port,
                                                slaveId,
                                                FwRegisters::COMPONENT_MODEL_BASE +
                                                    static_cast<uint16_t>(compNum) * FwRegisters::COMPONENT_STEP,
                                                FwRegisters::COMPONENT_MODEL_COUNT);
                info.Components.push_back({compNum, signature, fwVer, model});
            } catch (const std::exception& e) {
                LOG(Debug) << "Cannot read component " << compNum << " info: " << e.what();
            }
        }
    } catch (const std::exception&) {
        // Components presence register not available — skip
    }

    return info;
}

// Cf. firmware_update.py:382 flash_fw() and firmware_update.py:444 update_software()
void FlashFirmware(Modbus::IModbusTraits& traits,
                   TPort& port,
                   uint8_t slaveId,
                   const TParsedWBFW& firmware,
                   bool rebootToBootloader,
                   bool canPreservePortSettings,
                   std::function<void(int)> onProgress)
{
    if (rebootToBootloader) {
        RebootToBootloader(traits, port, slaveId, canPreservePortSettings);
    }

    WriteInfo(traits, port, slaveId, firmware.Info);
    WriteData(traits, port, slaveId, firmware.Data, std::move(onProgress));
}

// ============================================================
//                     TFwGetInfoTask
// ============================================================

TFwGetInfoTask::TFwGetInfoTask(uint8_t slaveId,
                               const std::string& protocol,
                               TFwGetInfoCallback onResult,
                               TFwGetInfoErrorCallback onError)
    : SlaveId(slaveId),
      Protocol(protocol),
      OnResult(std::move(onResult)),
      OnError(std::move(onError))
{}

ISerialClientTask::TRunResult TFwGetInfoTask::Run(PFeaturePort port,
                                                  TSerialClientDeviceAccessHandler& lastAccessedDevice,
                                                  const std::list<PSerialDevice>& polledDevices)
{
    try {
        if (!port->IsOpen()) {
            port->Open();
        }
        lastAccessedDevice.PrepareToAccess(*port, nullptr);
        port->SkipNoise();

        auto traits = MakeModbusTraits(Protocol);
        auto info = ReadFwDeviceInfo(*traits, *port, SlaveId);

        if (OnResult) {
            OnResult(info);
        }
    } catch (const TResponseTimeoutException& e) {
        if (OnError) {
            OnError(std::string("Device not responding: ") + e.what());
        }
    } catch (const std::exception& e) {
        if (OnError) {
            OnError(std::string("Error reading firmware info: ") + e.what());
        }
    }
    return ISerialClientTask::TRunResult::OK;
}

// ============================================================
//                     TFwFlashTask
// ============================================================

TFwFlashTask::TFwFlashTask(uint8_t slaveId,
                           const std::string& protocol,
                           TParsedWBFW firmware,
                           bool rebootToBootloader,
                           bool canPreservePortSettings,
                           TFwFlashProgressCallback onProgress,
                           TFwFlashCompleteCallback onComplete,
                           TFwFlashErrorCallback onError)
    : SlaveId(slaveId),
      Protocol(protocol),
      Firmware(std::move(firmware)),
      RebootToBootloader(rebootToBootloader),
      CanPreservePortSettings(canPreservePortSettings),
      OnProgress(std::move(onProgress)),
      OnComplete(std::move(onComplete)),
      OnError(std::move(onError))
{}

ISerialClientTask::TRunResult TFwFlashTask::Run(PFeaturePort port,
                                                TSerialClientDeviceAccessHandler& lastAccessedDevice,
                                                const std::list<PSerialDevice>& polledDevices)
{
    try {
        if (!port->IsOpen()) {
            port->Open();
        }
        lastAccessedDevice.PrepareToAccess(*port, nullptr);
        port->SkipNoise();

        auto traits = MakeModbusTraits(Protocol);

        FlashFirmware(*traits, *port, SlaveId, Firmware, RebootToBootloader, CanPreservePortSettings, OnProgress);

        if (OnComplete) {
            OnComplete();
        }
    } catch (const std::exception& e) {
        if (OnError) {
            OnError(std::string("Firmware flash error: ") + e.what());
        }
    }
    return ISerialClientTask::TRunResult::OK;
}
