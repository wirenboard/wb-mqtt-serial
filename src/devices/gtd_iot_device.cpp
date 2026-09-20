#include "gtd_iot_device.h"
#include "bin_utils.h"
#include "log.h"

/**
 * GTD IOT panels protocol
 *
 * Requests are Modbus RTU: read holding registers (0x03) and write single register (0x06).
 * Write response is the echo of the request. Read response has non-standard header:
 * +--------+------+-----------------------------------------------+--------------+-------+
 * |Slave ID|0x03  |Register count, byte count or register address |Values        |CRC    |
 * |1 byte  |1 byte|2 bytes                                        |2 bytes each  |2 bytes|
 * +--------+------+-----------------------------------------------+--------------+-------+
 * The panel doesn't send Modbus exceptions, it just doesn't answer.
 *
 * Key code register (0x100B) gets the mask with the bit of the pressed key. The mask is reset by reading of
 * the register itself, by reading of key statuses and by the panel about 2 s after the press, so the keys must
 * be polled more often. Key status registers (0x1310...) show the key state only while the key is pressed.
 * So key registers are polled together: key code first and then statuses of the configured keys. A key pressed
 * between these requests is still pressed when statuses are read.
 */

#define LOG(logger) logger.Log() << "[gtd iot] "

using namespace BinUtils;

namespace
{
    enum TRegTypes
    {
        HOLDING,
        KEY_STATUS,
        SINGLE_PRESS_COUNTER,
        LONG_PRESS_COUNTER
    };

    const TRegisterTypes RegTypes{{HOLDING, "holding", "value", U16},
                                  {KEY_STATUS, "status", "value", U16, true},
                                  {SINGLE_PRESS_COUNTER, "single_press_counter", "value", U16, true},
                                  {LONG_PRESS_COUNTER, "long_press_counter", "value", U16, true}};

    // Function code (1 byte) + header (2 bytes) + values (2 bytes each)
    const size_t VALUE_POSITION = 3;
    const size_t RESPONSE_PDU_SIZE = VALUE_POSITION + 2;

    // Response is PDU with slave id and CRC
    const size_t PDU_TO_ADU_SIZE = 3;
    const size_t READ_REQUEST_SIZE = Modbus::READ_REQUEST_PDU_SIZE + PDU_TO_ADU_SIZE;

    const uint16_t KEY_CODE_ADDRESS = 0x100B;
    const uint16_t FIRST_KEY_STATUS_ADDRESS = 0x1310;

    const size_t REGISTER_POLL_BYTES = READ_REQUEST_SIZE + RESPONSE_PDU_SIZE + PDU_TO_ADU_SIZE;

    const uint16_t KEY_PRESSED = 1;
    const uint16_t KEY_HELD = 2;

    bool IsKeyRegister(int type)
    {
        return type != HOLDING;
    }

    size_t GetKeyStatusesPduSize(size_t keyCount)
    {
        return VALUE_POSITION + 2 * keyCount;
    }

    size_t GetKeysPollBytes(size_t keyCount)
    {
        return REGISTER_POLL_BYTES + READ_REQUEST_SIZE + GetKeyStatusesPduSize(keyCount) + PDU_TO_ADU_SIZE;
    }

    //! Key of a key register. The address is checked by TGtdIotAddressFactory, the limit is for registers
    //! created bypassing it
    size_t GetKeyIndex(const TRegisterConfig& reg)
    {
        return std::min<size_t>(GetUint32RegisterAddress(reg.GetAddress()) - FIRST_KEY_STATUS_ADDRESS,
                                TGtdIotDevice::MAX_KEYS - 1);
    }

    //! All key register types are addressed by the key status register, so the address range is limited
    class TGtdIotAddressFactory: public TUint32RegisterAddressFactory
    {
    public:
        TRegisterDesc LoadRegisterAddress(const Json::Value& regCfg,
                                          const IRegisterAddress& deviceBaseAddress,
                                          uint32_t stride,
                                          uint32_t registerByteWidth) const override
        {
            auto res = TUint32RegisterAddressFactory::LoadRegisterAddress(regCfg,
                                                                          deviceBaseAddress,
                                                                          stride,
                                                                          registerByteWidth);
            if (res.Address && regCfg.get("reg_type", "holding").asString() != "holding") {
                auto address = GetUint32RegisterAddress(*res.Address);
                if (address < FIRST_KEY_STATUS_ADDRESS || address >= FIRST_KEY_STATUS_ADDRESS + TGtdIotDevice::MAX_KEYS)
                {
                    throw TConfigParserException("key register address must be 0x1310-0x1317, not " +
                                                 regCfg["address"].asString());
                }
            }
            return res;
        }
    };

    //! All key registers of the device are read together, other registers with the same address are read together
    class TGtdIotRegisterRange: public TSameAddressRegisterRange
    {
        std::chrono::microseconds AverageResponseTime;
        size_t KeyCount;

    public:
        TGtdIotRegisterRange(std::chrono::microseconds averageResponseTime, size_t keyCount)
            : AverageResponseTime(averageResponseTime),
              KeyCount(keyCount)
        {}

        bool Add(TPort& port, PRegister reg, std::chrono::milliseconds pollLimit) override
        {
            auto keyRegister = IsKeyRegister(reg->GetConfig()->Type);
            if (RegisterList().empty()) {
                // Keys are read with two requests: key code and statuses of the keys, other registers with one
                auto requests = keyRegister ? 2 : 1;
                auto pollTime = port.GetSendTimeBytes(keyRegister ? GetKeysPollBytes(KeyCount) : REGISTER_POLL_BYTES) +
                                requests * (AverageResponseTime + reg->Device()->DeviceConfig()->RequestDelay);
                if (std::chrono::ceil<std::chrono::milliseconds>(pollTime) > pollLimit) {
                    LOG(Debug) << "Poll time for " << reg->ToString() << " is too long: " << pollTime.count()
                               << " us, limit is " << pollLimit.count() << " ms";
                    return false;
                }
            } else if (keyRegister && IsKeyRegister(RegisterList().front()->GetConfig()->Type) &&
                       reg->Device() == RegisterList().front()->Device())
            {
                // More registers of the range don't increase poll time
                RegisterList().push_back(reg);
                return true;
            }
            return TSameAddressRegisterRange::Add(port, reg, pollLimit);
        }
    };
}

void TGtdIotDevice::Register(TSerialDeviceFactory& factory)
{
    factory.RegisterProtocol(
        new TUint32SlaveIdProtocol("gtd_iot", RegTypes),
        new TBasicDeviceFactory<TGtdIotDevice, TGtdIotAddressFactory>("#/definitions/simple_device_no_channels"));
}

TGtdIotDevice::TGtdIotDevice(PDeviceConfig config, PProtocol protocol)
    : TSerialDevice(config, protocol),
      TUInt32SlaveId(config->SlaveId),
      ResponseTime(std::chrono::microseconds::zero())
{
    AddOnConnectionStateChangedCallback([this](PSerialDevice) {
        if (GetConnectionState() == TDeviceConnectionState::DISCONNECTED) {
            // The panel could be pressed or restarted while it was not polled
            Keys = {};
            PendingKeyCode = 0;
        }
    });
}

std::vector<uint8_t> TGtdIotDevice::ExecTransaction(TPort& port,
                                                    const std::vector<uint8_t>& requestPdu,
                                                    size_t responsePduSize)
{
    std::vector<uint8_t> responsePdu;
    port.SleepSinceLastInteraction(DeviceConfig()->RequestDelay);
    try {
        auto res = ModbusTraits.Transaction(port,
                                            SlaveId,
                                            requestPdu,
                                            responsePduSize,
                                            GetResponseTimeout(port),
                                            GetFrameTimeout(port));
        ResponseTime.AddValue(res.ResponseTime);
        responsePdu = res.Pdu;
    } catch (const Modbus::TErrorBase& e) {
        port.SkipNoise();
        throw TSerialDeviceTransientErrorException(e.what());
    }
    if (responsePdu.size() != responsePduSize || responsePdu[0] != requestPdu[0]) {
        throw TSerialDeviceTransientErrorException("unexpected response");
    }
    return responsePdu;
}

uint16_t TGtdIotDevice::ReadValue(TPort& port, uint16_t address)
{
    auto responsePdu =
        ExecTransaction(port, Modbus::MakePDU(Modbus::FN_READ_HOLDING, address, 1, {}), RESPONSE_PDU_SIZE);
    return GetFromBigEndian<uint16_t>(responsePdu.begin() + VALUE_POSITION);
}

void TGtdIotDevice::UpdateKey(TKey& key, bool newPress, uint16_t status)
{
    if (newPress && key.Pressed && !key.Long) {
        // The key was released and pressed again between polls
        ++key.SinglePresses;
    }
    // Key code of a press started between the requests is reset by reading of statuses
    newPress = newPress || (status && !key.Pressed);
    if (newPress) {
        key.Pressed = true;
        key.Long = false;
    }
    key.Status = status;
    if (status == KEY_HELD && !key.Long) {
        key.Long = true;
        ++key.LongPresses;
    }
    if (key.Pressed && !status) {
        if (!key.Long) {
            ++key.SinglePresses;
        }
        key.Pressed = false;
        if (newPress) {
            // The key is already released, but its press must be shown
            key.Status = KEY_PRESSED;
        }
    }
}

size_t TGtdIotDevice::GetKeyCount() const
{
    size_t keyCount = 0;
    for (const auto& reg: GetRegisters()) {
        const auto& config = *reg->GetConfig();
        // Key registers are read-only, but a channel type could make them write-only, such registers have no address
        if (IsKeyRegister(config.Type) && config.AccessType != TRegisterConfig::EAccessType::WRITE_ONLY) {
            keyCount = std::max(keyCount, GetKeyIndex(config) + 1);
        }
    }
    // Registers made for RPC requests are not added to the device, so poll all keys if there are no known ones
    return keyCount ? keyCount : MAX_KEYS;
}

void TGtdIotDevice::ReadKeys(TPort& port)
{
    auto keyCount = GetKeyCount();
    // Reading of key statuses resets key code, so the code is read first and kept if the reading fails
    PendingKeyCode |= ReadValue(port, KEY_CODE_ADDRESS);
    auto responsePdu = ExecTransaction(port,
                                       Modbus::MakePDU(Modbus::FN_READ_HOLDING, FIRST_KEY_STATUS_ADDRESS, keyCount, {}),
                                       GetKeyStatusesPduSize(keyCount));
    auto keyCode = PendingKeyCode;
    PendingKeyCode = 0;
    // Bits of the key code above the configured keys are ignored
    for (size_t i = 0; i < keyCount; ++i) {
        UpdateKey(Keys[i],
                  keyCode & (1 << i),
                  GetFromBigEndian<uint16_t>(responsePdu.begin() + VALUE_POSITION + 2 * i));
    }
}

TRegisterValue TGtdIotDevice::GetRegisterValue(const TRegisterConfig& reg, uint16_t value) const
{
    if (!IsKeyRegister(reg.Type)) {
        return TRegisterValue{(static_cast<uint64_t>(value) >> reg.GetDataOffset()) & GetLSBMask(reg.GetDataWidth())};
    }
    const auto& key = Keys[GetKeyIndex(reg)];
    switch (reg.Type) {
        case SINGLE_PRESS_COUNTER:
            return TRegisterValue{key.SinglePresses};
        case LONG_PRESS_COUNTER:
            return TRegisterValue{key.LongPresses};
    }
    return TRegisterValue{key.Status};
}

PRegisterRange TGtdIotDevice::CreateRegisterRange() const
{
    return std::make_shared<TGtdIotRegisterRange>(ResponseTime.GetValue(), GetKeyCount());
}

void TGtdIotDevice::ReadRegisterRange(TPort& port, PRegisterRange range, bool breakOnError)
{
    const auto& regs = range->RegisterList();
    try {
        // A range is read from the device once, not once per register
        uint16_t value = 0;
        if (IsKeyRegister(regs.front()->GetConfig()->Type)) {
            ReadKeys(port);
        } else {
            value = ReadValue(port, GetUint32RegisterAddress(regs.front()->GetConfig()->GetAddress()));
        }
        SetTransferResult(true);
        for (const auto& reg: regs) {
            reg->SetValue(GetRegisterValue(*reg->GetConfig(), value));
        }
    } catch (const TSerialDeviceException& e) {
        for (const auto& reg: regs) {
            reg->SetError(TRegister::TError::ReadError);
        }
        auto& logger = (GetConnectionState() == TDeviceConnectionState::DISCONNECTED) ? Debug : Warn;
        LOG(logger) << "failed to read " << regs.front()->ToString() << ": " << e.what();
        SetTransferResult(false);
        if (breakOnError) {
            throw;
        }
    }
}

void TGtdIotDevice::WriteRegisterImpl(TPort& port, const TRegisterConfig& reg, const TRegisterValue& regValue)
{
    auto address = GetUint32RegisterAddress(reg.GetWriteAddress());
    auto mask = GetLSBMask(reg.GetDataWidth()) << reg.GetDataOffset();
    uint16_t value = (regValue.Get<uint64_t>() << reg.GetDataOffset()) & mask;
    if (reg.IsPartial()) {
        value |= ReadValue(port, address) & ~mask;
    }
    auto requestPdu = Modbus::MakePDU(Modbus::FN_WRITE_SINGLE_REGISTER,
                                      address,
                                      1,
                                      {static_cast<uint8_t>(value >> 8), static_cast<uint8_t>(value)});
    // The panel answers with the echo of the request only if the value is accepted,
    // so a rejected value looks like a response timeout
    if (ExecTransaction(port, requestPdu, RESPONSE_PDU_SIZE) != requestPdu) {
        throw TSerialDeviceTransientErrorException("write response doesn't match the request");
    }
}
