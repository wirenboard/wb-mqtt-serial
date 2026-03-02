#include "rpc_fw_update_task.h"
#include "log.h"
#include "modbus_base.h"
#include "serial_exc.h"

#include <algorithm>
#include <thread>

#define LOG(logger) ::logger.Log() << "[fw-update] "

namespace
{
    std::string RegisterBytesToString(const std::vector<uint8_t>& data)
    {
        // Convert Modbus register data to string, taking every other byte (high byte is 0 for ASCII)
        // and stripping null/0xFF padding
        std::string result;
        for (size_t i = 1; i < data.size(); i += 2) {
            uint8_t ch = data[i];
            if (ch != 0 && ch != 0xFF) {
                result += static_cast<char>(ch);
            }
        }
        return result;
    }

    const auto FW_RESPONSE_TIMEOUT = std::chrono::milliseconds(500);
    const auto FW_FRAME_TIMEOUT = std::chrono::milliseconds(20);
    const auto TASK_TOTAL_TIMEOUT = std::chrono::seconds(30);

    std::vector<uint8_t> ReadRawRegister(TPort& port,
                                         Modbus::IModbusTraits& traits,
                                         uint8_t slaveId,
                                         uint16_t addr,
                                         uint16_t count,
                                         uint8_t fnCode)
    {
        auto function = static_cast<Modbus::EFunction>(fnCode);
        auto pdu = Modbus::MakePDU(function, addr, count, {});
        auto responsePduSize = Modbus::CalcResponsePDUSize(function, count);
        auto res = traits.Transaction(port, slaveId, pdu, responsePduSize, FW_RESPONSE_TIMEOUT, FW_FRAME_TIMEOUT);
        return Modbus::ExtractResponseData(function, res.Pdu);
    }

    bool TryReadRegister(TPort& port,
                         Modbus::IModbusTraits& traits,
                         uint8_t slaveId,
                         uint16_t addr,
                         uint16_t count,
                         uint8_t fnCode,
                         std::vector<uint8_t>& result)
    {
        try {
            result = ReadRawRegister(port, traits, slaveId, addr, count, fnCode);
            return true;
        } catch (const std::exception&) {
            return false;
        }
    }

    std::string ReadStringRegister(TPort& port,
                                   Modbus::IModbusTraits& traits,
                                   uint8_t slaveId,
                                   uint16_t addr,
                                   uint16_t count,
                                   uint8_t fnCode)
    {
        auto data = ReadRawRegister(port, traits, slaveId, addr, count, fnCode);
        return RegisterBytesToString(data);
    }

    // Cf. firmware_update.py:412 reboot_to_bootloader()
    void DoReboot(TPort& port, Modbus::IModbusTraits& traits, uint8_t slaveId, bool canPreservePortSettings)
    {
        auto rebootTimeout = std::chrono::milliseconds(1000);
        auto frameTimeout = std::chrono::milliseconds(20);

        if (canPreservePortSettings) {
            // Write 1 to register 131
            auto pdu = Modbus::MakePDU(Modbus::FN_WRITE_SINGLE_REGISTER,
                                       FwRegisters::REBOOT_PRESERVE_PORT_SETTINGS_ADDR,
                                       1,
                                       {0x00, 0x01});
            auto responsePduSize = Modbus::CalcResponsePDUSize(Modbus::FN_WRITE_SINGLE_REGISTER, 1);
            traits.Transaction(port, slaveId, pdu, responsePduSize, rebootTimeout, frameTimeout);
        } else {
            try {
                // Write 1 to register 129
                auto pdu = Modbus::MakePDU(Modbus::FN_WRITE_SINGLE_REGISTER,
                                           FwRegisters::REBOOT_TO_BOOTLOADER_ADDR,
                                           1,
                                           {0x00, 0x01});
                auto responsePduSize = Modbus::CalcResponsePDUSize(Modbus::FN_WRITE_SINGLE_REGISTER, 1);
                traits.Transaction(port, slaveId, pdu, responsePduSize, rebootTimeout, frameTimeout);
            } catch (const TResponseTimeoutException&) {
                // Device has rebooted and doesn't send response (expected for old firmware)
                LOG(Debug) << "Device doesn't respond to reboot command, probably it has rebooted";
            }
        }

        // Delay before going to bootloader
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

    // Cf. firmware_update.py:382 flash_fw() - info block write part
    void WriteInfoBlock(TPort& port,
                        Modbus::IModbusTraits& traits,
                        uint8_t slaveId,
                        const std::vector<uint8_t>& infoData)
    {
        auto infoBlockTimeout = std::chrono::milliseconds(1000);
        auto frameTimeout = std::chrono::milliseconds(20);

        auto pdu = Modbus::MakePDU(Modbus::FN_WRITE_MULTIPLE_REGISTERS,
                                   FwRegisters::FW_INFO_BLOCK_ADDR,
                                   FwRegisters::FW_INFO_BLOCK_COUNT,
                                   infoData);
        auto responsePduSize =
            Modbus::CalcResponsePDUSize(Modbus::FN_WRITE_MULTIPLE_REGISTERS, FwRegisters::FW_INFO_BLOCK_COUNT);
        traits.Transaction(port, slaveId, pdu, responsePduSize, infoBlockTimeout, frameTimeout);
    }

    // Cf. firmware_update.py:349 write_fw_data_block()
    void WriteDataBlock(TPort& port, Modbus::IModbusTraits& traits, uint8_t slaveId, const std::vector<uint8_t>& chunk)
    {
        const int MAX_RETRIES = 3;
        std::exception_ptr lastException;

        uint16_t regCount = static_cast<uint16_t>(chunk.size() / 2);

        for (int attempt = 0; attempt < MAX_RETRIES; ++attempt) {
            try {
                auto pdu = Modbus::MakePDU(Modbus::FN_WRITE_MULTIPLE_REGISTERS,
                                           FwRegisters::FW_DATA_BLOCK_ADDR,
                                           regCount,
                                           chunk);
                auto responsePduSize = Modbus::CalcResponsePDUSize(Modbus::FN_WRITE_MULTIPLE_REGISTERS, regCount);
                traits.Transaction(port, slaveId, pdu, responsePduSize, FW_RESPONSE_TIMEOUT, FW_FRAME_TIMEOUT);
                return; // Success
            } catch (const Modbus::TModbusExceptionError& e) {
                // Slave device failure (0x04) means chunk is already written
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

    void WriteDataBlocks(TPort& port,
                         Modbus::IModbusTraits& traits,
                         uint8_t slaveId,
                         const std::vector<uint8_t>& firmwareData,
                         std::function<void(int)> onProgress)
    {
        const size_t CHUNK_SIZE = FwRegisters::FW_DATA_BLOCK_COUNT * 2; // 68 registers * 2 bytes = 136 bytes

        size_t totalChunks = (firmwareData.size() + CHUNK_SIZE - 1) / CHUNK_SIZE;
        if (totalChunks == 0 && firmwareData.empty()) {
            if (onProgress) {
                onProgress(100);
            }
            return;
        }

        for (size_t i = 0; i < totalChunks; ++i) {
            size_t offset = i * CHUNK_SIZE;
            size_t chunkLen = std::min(CHUNK_SIZE, firmwareData.size() - offset);
            std::vector<uint8_t> chunk(firmwareData.begin() + offset, firmwareData.begin() + offset + chunkLen);

            // Pad to even length if needed
            if (chunk.size() % 2 != 0) {
                chunk.push_back(0xFF);
            }

            WriteDataBlock(port, traits, slaveId, chunk);

            if (onProgress) {
                onProgress(static_cast<int>((i + 1) * 100 / totalChunks));
            }
        }
    }
}

// ============================================================
//                     Public free functions
// ============================================================

std::unique_ptr<Modbus::IModbusTraits> MakeModbusTraits(const std::string& protocol)
{
    if (protocol == "modbus-tcp") {
        return std::make_unique<Modbus::TModbusTCPTraits>();
    }
    return std::make_unique<Modbus::TModbusRTUTraits>();
}

// Cf. firmware_update.py:311 FirmwareInfoReader.read()
TFwDeviceInfo ReadFwDeviceInfo(TPort& port, Modbus::IModbusTraits& traits, uint8_t slaveId)
{
    TFwDeviceInfo info;

    // Read firmware signature - Cf. firmware_update.py:252 read_fw_signature()
    info.FwSignature = ReadStringRegister(port,
                                          traits,
                                          slaveId,
                                          FwRegisters::FW_SIGNATURE_ADDR,
                                          FwRegisters::FW_SIGNATURE_COUNT,
                                          0x03);

    // Read firmware version - Cf. firmware_update.py:255 read_fw_version()
    // In bootloader mode, firmware version registers may not be available
    try {
        info.FwVersion = ReadStringRegister(port,
                                            traits,
                                            slaveId,
                                            FwRegisters::FW_VERSION_ADDR,
                                            FwRegisters::FW_VERSION_COUNT,
                                            0x03);
    } catch (const std::exception& e) {
        LOG(Debug) << "Cannot read firmware version: " << e.what();
    }

    // Read bootloader version - Cf. firmware_update.py:234 read_bootloader_info()
    try {
        info.BootloaderVersion = ReadStringRegister(port,
                                                    traits,
                                                    slaveId,
                                                    FwRegisters::BOOTLOADER_VERSION_ADDR,
                                                    FwRegisters::BOOTLOADER_VERSION_COUNT,
                                                    0x03);
    } catch (const std::exception& e) {
        LOG(Debug) << "Cannot read bootloader version: " << e.what();
    }

    // Check if bootloader can preserve port settings (register 131 readable means yes)
    try {
        auto pdu = Modbus::MakePDU(Modbus::FN_READ_HOLDING, FwRegisters::REBOOT_PRESERVE_PORT_SETTINGS_ADDR, 1, {});
        auto responsePduSize = Modbus::CalcResponsePDUSize(Modbus::FN_READ_HOLDING, 1);
        auto res = traits.Transaction(port, slaveId, pdu, responsePduSize, FW_RESPONSE_TIMEOUT, FW_FRAME_TIMEOUT);
        auto data = Modbus::ExtractResponseData(Modbus::FN_READ_HOLDING, res.Pdu);
        if (data.size() >= 2) {
            uint16_t val = (static_cast<uint16_t>(data[0]) << 8) | data[1];
            info.CanPreservePortSettings = (val == 0);
        }
    } catch (const std::exception&) {
        info.CanPreservePortSettings = false;
    }

    // Read device model (try extended first, then standard) - Cf. firmware_update.py:574 read_device_model()
    try {
        info.DeviceModel = ReadStringRegister(port,
                                              traits,
                                              slaveId,
                                              FwRegisters::DEVICE_MODEL_EXTENDED_ADDR,
                                              FwRegisters::DEVICE_MODEL_EXTENDED_COUNT,
                                              0x03);
    } catch (const std::exception&) {
        try {
            info.DeviceModel = ReadStringRegister(port,
                                                  traits,
                                                  slaveId,
                                                  FwRegisters::DEVICE_MODEL_ADDR,
                                                  FwRegisters::DEVICE_MODEL_COUNT,
                                                  0x03);
        } catch (const std::exception& e) {
            LOG(Debug) << "Cannot read device model: " << e.what();
        }
    }

    // Clean up device model - remove 0x02 characters (like Python's replace("\x02", ""))
    info.DeviceModel.erase(std::remove(info.DeviceModel.begin(), info.DeviceModel.end(), '\x02'),
                           info.DeviceModel.end());

    // Read components presence (function 2 = READ_DISCRETE) - Cf. firmware_update.py:278 read_components_presence()
    std::vector<uint8_t> presenceData;
    if (TryReadRegister(port,
                        traits,
                        slaveId,
                        FwRegisters::COMPONENTS_PRESENCE_ADDR,
                        FwRegisters::COMPONENTS_PRESENCE_COUNT,
                        0x02,
                        presenceData))
    {
        // Each byte has 8 bits, each bit is a component presence flag
        std::vector<int> presentComponents;
        for (size_t byteIdx = 0; byteIdx < presenceData.size(); ++byteIdx) {
            for (int bit = 0; bit < 8; ++bit) {
                if (presenceData[byteIdx] & (1 << bit)) {
                    presentComponents.push_back(static_cast<int>(byteIdx * 8 + bit));
                }
            }
        }

        // Read info for each present component - Cf. firmware_update.py:258 read_component_info()
        // Component registers use READ_INPUT (function 4)
        for (int compNum: presentComponents) {
            try {
                uint16_t sigAddr = FwRegisters::COMPONENT_SIGNATURE_BASE +
                                   static_cast<uint16_t>(compNum) * FwRegisters::COMPONENT_STEP;
                uint16_t fwAddr = FwRegisters::COMPONENT_FW_VERSION_BASE +
                                  static_cast<uint16_t>(compNum) * FwRegisters::COMPONENT_STEP;
                uint16_t modelAddr =
                    FwRegisters::COMPONENT_MODEL_BASE + static_cast<uint16_t>(compNum) * FwRegisters::COMPONENT_STEP;

                auto sigData =
                    ReadRawRegister(port, traits, slaveId, sigAddr, FwRegisters::COMPONENT_SIGNATURE_COUNT, 0x04);
                std::string signature = RegisterBytesToString(sigData);

                // Check for unavailable component (all 0xFE bytes)
                bool allFE = true;
                for (auto b: sigData) {
                    if (b != 0xFE) {
                        allFE = false;
                        break;
                    }
                }
                if (allFE) {
                    continue;
                }

                std::string fwVer =
                    ReadStringRegister(port, traits, slaveId, fwAddr, FwRegisters::COMPONENT_FW_VERSION_COUNT, 0x04);
                std::string model =
                    ReadStringRegister(port, traits, slaveId, modelAddr, FwRegisters::COMPONENT_MODEL_COUNT, 0x04);

                info.Components.push_back({compNum, signature, fwVer, model});
            } catch (const std::exception& e) {
                LOG(Debug) << "Cannot read component " << compNum << " info: " << e.what();
            }
        }
    }

    return info;
}

// Cf. firmware_update.py:382 flash_fw() and firmware_update.py:444 update_software()
void FlashFirmware(TPort& port,
                   Modbus::IModbusTraits& traits,
                   uint8_t slaveId,
                   const TParsedWBFW& firmware,
                   bool rebootToBootloader,
                   bool canPreservePortSettings,
                   std::function<void(int)> onProgress)
{
    if (rebootToBootloader) {
        DoReboot(port, traits, slaveId, canPreservePortSettings);
    }

    WriteInfoBlock(port, traits, slaveId, firmware.Info);
    WriteDataBlocks(port, traits, slaveId, firmware.Data, std::move(onProgress));
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
      OnError(std::move(onError)),
      ExpireTime(std::chrono::steady_clock::now() + TASK_TOTAL_TIMEOUT)
{}

ISerialClientTask::TRunResult TFwGetInfoTask::Run(PFeaturePort port,
                                                  TSerialClientDeviceAccessHandler& lastAccessedDevice,
                                                  const std::list<PSerialDevice>& polledDevices)
{
    if (std::chrono::steady_clock::now() > ExpireTime) {
        if (OnError) {
            OnError("Firmware info read timeout");
        }
        return ISerialClientTask::TRunResult::OK;
    }

    try {
        if (!port->IsOpen()) {
            port->Open();
        }
        lastAccessedDevice.PrepareToAccess(*port, nullptr);
        port->SkipNoise();

        auto traits = MakeModbusTraits(Protocol);
        auto info = ReadFwDeviceInfo(*port, *traits, SlaveId);

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

        FlashFirmware(*port, *traits, SlaveId, Firmware, RebootToBootloader, CanPreservePortSettings, OnProgress);

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
