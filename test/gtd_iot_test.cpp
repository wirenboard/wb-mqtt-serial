#include "devices/gtd_iot_device.h"
#include "fake_serial_port.h"
#include "modbus_expectations_base.h"

class TGtdIotDeviceTest: public TSerialDeviceTest, public TModbusExpectationsBase
{
protected:
    struct TKeyRegisters
    {
        PRegister Status;
        PRegister SinglePresses;
        PRegister LongPresses;
    };

    void SetUp() override;

    PRegister AddRegister(uint32_t address, uint32_t bitOffset = 0, uint32_t bitWidth = 0);
    PRegister AddRegister(const std::string& type, uint32_t address);
    TKeyRegisters AddKey(uint32_t address);
    void ReadRange(const std::vector<PRegister>& regs);
    std::vector<int> MakeReadRequest(uint16_t address, uint16_t count);
    void ExpectRead(uint16_t address, uint16_t header, const std::vector<uint16_t>& values);
    void ExpectReadWithoutAnswer(uint16_t address, uint16_t count);
    void ExpectKeys(uint16_t keyCode, std::vector<uint16_t> statuses);
    void CheckKey(const TKeyRegisters& key, uint16_t status, uint16_t singlePresses, uint16_t longPresses);

    PSerialDevice Dev;

    //! Number of keys added by AddKey, their statuses are read with one request
    size_t KeyCount = 0;
};

void TGtdIotDeviceTest::SetUp()
{
    TSerialDeviceTest::SetUp();
    Dev = std::make_shared<TGtdIotDevice>(std::make_shared<TDeviceConfig>("panel", "1", "gtd_iot"),
                                          DeviceFactory.GetProtocol("gtd_iot"));
    SerialPort->Open();
}

PRegister TGtdIotDeviceTest::AddRegister(uint32_t address, uint32_t bitOffset, uint32_t bitWidth)
{
    TRegisterDesc desc{std::make_shared<TUint32RegisterAddress>(address), bitOffset, bitWidth};
    return Dev->AddRegister(TRegisterConfig::Create(0, desc));
}

PRegister TGtdIotDeviceTest::AddRegister(const std::string& type, uint32_t address)
{
    TRegisterDesc desc{std::make_shared<TUint32RegisterAddress>(address)};
    return Dev->AddRegister(
        TRegisterConfig::Create(DeviceFactory.GetProtocol("gtd_iot")->GetRegTypes()->Find(type).Index, desc));
}

TGtdIotDeviceTest::TKeyRegisters TGtdIotDeviceTest::AddKey(uint32_t address)
{
    KeyCount = std::max<size_t>(KeyCount, address - 0x1310 + 1);
    return {AddRegister("status", address),
            AddRegister("single_press_counter", address),
            AddRegister("long_press_counter", address)};
}

void TGtdIotDeviceTest::ReadRange(const std::vector<PRegister>& regs)
{
    auto range = Dev->CreateRegisterRange();
    for (const auto& reg: regs) {
        ASSERT_TRUE(range->Add(*SerialPort, reg, std::chrono::milliseconds::max()));
    }
    Dev->ReadRegisterRange(*SerialPort, range);
}

std::vector<int> TGtdIotDeviceTest::MakeReadRequest(uint16_t address, uint16_t count)
{
    return WrapPDU({0x03, address >> 8, address & 0xFF, 0x00, count});
}

// The response header is length or register address
void TGtdIotDeviceTest::ExpectRead(uint16_t address, uint16_t header, const std::vector<uint16_t>& values)
{
    std::vector<int> responsePdu{0x03, header >> 8, header & 0xFF};
    for (auto value: values) {
        responsePdu.push_back(value >> 8);
        responsePdu.push_back(value & 0xFF);
    }
    SerialPort->Expect(MakeReadRequest(address, values.size()), WrapPDU(responsePdu));
}

void TGtdIotDeviceTest::ExpectReadWithoutAnswer(uint16_t address, uint16_t count)
{
    SerialPort->Expect(MakeReadRequest(address, count), {});
}

// Key registers are read together: key code and then statuses of all configured keys
void TGtdIotDeviceTest::ExpectKeys(uint16_t keyCode, std::vector<uint16_t> statuses)
{
    statuses.resize(KeyCount);
    ExpectRead(0x100B, 2, {keyCode});
    ExpectRead(0x1310, KeyCount, statuses);
}

void TGtdIotDeviceTest::CheckKey(const TKeyRegisters& key,
                                 uint16_t status,
                                 uint16_t singlePresses,
                                 uint16_t longPresses)
{
    EXPECT_EQ(TRegisterValue{status}, key.Status->GetValue());
    EXPECT_EQ(TRegisterValue{singlePresses}, key.SinglePresses->GetValue());
    EXPECT_EQ(TRegisterValue{longPresses}, key.LongPresses->GetValue());
}

TEST_F(TGtdIotDeviceTest, Read)
{
    auto led1 = AddRegister(0x1008, 0, 1);
    auto led3 = AddRegister(0x1008, 2, 1);
    auto backlight = AddRegister(0x1008, 8, 1);

    // The register is read once for all its channels.
    // The response has register address instead of Modbus byte count
    SerialPort->Expect({0x01, 0x03, 0x10, 0x08, 0x00, 0x01, 0x01, 0x08},
                       {0x01, 0x03, 0x10, 0x08, 0x01, 0x04, 0xC0, 0x9B});
    ReadRange({led1, led3, backlight});
    EXPECT_EQ(TRegisterValue{0}, led1->GetValue());
    EXPECT_EQ(TRegisterValue{1}, led3->GetValue());
    EXPECT_EQ(TRegisterValue{1}, backlight->GetValue());

    // Failed request is not repeated for other channels of the range
    SerialPort->Expect({0x01, 0x03, 0x10, 0x08, 0x00, 0x01, 0x01, 0x08}, {});
    ReadRange({led1, led3});
    EXPECT_TRUE(led1->GetErrorState().test(TRegister::ReadError));
    EXPECT_TRUE(led3->GetErrorState().test(TRegister::ReadError));
}

TEST_F(TGtdIotDeviceTest, Keys)
{
    auto key1 = AddKey(0x1310);
    auto key2 = AddKey(0x1311);
    std::vector<PRegister> keys{key1.Status,
                                key1.SinglePresses,
                                key1.LongPresses,
                                key2.Status,
                                key2.SinglePresses,
                                key2.LongPresses};

    ExpectKeys(0, {});
    ReadRange(keys);
    CheckKey(key1, 0, 0, 0);
    CheckKey(key2, 0, 0, 0);

    // K2 is pressed and released between polls: the press is shown once
    ExpectKeys(0x0202, {});
    ReadRange(keys);
    CheckKey(key2, 1, 1, 0);
    ExpectKeys(0, {});
    ReadRange(keys);
    CheckKey(key2, 0, 1, 0);

    // K1 is pressed between the requests: key code is reset, but the key is still pressed
    ExpectKeys(0, {1});
    ReadRange(keys);
    CheckKey(key1, 1, 0, 0);
    ExpectKeys(0, {2});
    ReadRange(keys);
    CheckKey(key1, 2, 0, 1);
    ExpectKeys(0, {0});
    ReadRange(keys);
    CheckKey(key1, 0, 0, 1);

    // K1 is released and pressed again between polls
    ExpectKeys(0x0101, {1});
    ReadRange(keys);
    CheckKey(key1, 1, 0, 1);
    ExpectKeys(0x0101, {1});
    ReadRange(keys);
    CheckKey(key1, 1, 1, 1);
    ExpectKeys(0, {0});
    ReadRange(keys);
    CheckKey(key1, 0, 2, 1);
    CheckKey(key2, 0, 1, 0);

    // K3 is not configured, its bit of the key code is dropped
    ExpectKeys(0x0404, {});
    ReadRange(keys);
    CheckKey(key1, 0, 2, 1);
    CheckKey(key2, 0, 1, 0);

    // The number of read statuses is taken from the device, so a range with a part of key registers
    // and a key channel made write only by its type don't change the request
    ExpectKeys(0, {});
    ReadRange({key1.Status});
    AddRegister("status", 0x1317)->GetConfig()->AccessType = TRegisterConfig::EAccessType::WRITE_ONLY;
    ExpectKeys(0, {});
    ReadRange(keys);
}

TEST_F(TGtdIotDeviceTest, KeysOfNotAddedRegister)
{
    // Registers made for RPC requests are not added to the device, statuses of all keys are read for them
    TRegisterDesc desc{std::make_shared<TUint32RegisterAddress>(0x1310)};
    auto type = DeviceFactory.GetProtocol("gtd_iot")->GetRegTypes()->Find("status").Index;
    auto key = std::make_shared<TRegister>(Dev, TRegisterConfig::Create(type, desc));
    KeyCount = TGtdIotDevice::MAX_KEYS;

    ExpectKeys(0x0101, {});
    ReadRange({key});
    EXPECT_EQ(TRegisterValue{1}, key->GetValue());
}

TEST_F(TGtdIotDeviceTest, KeysReadError)
{
    // The device is not marked as disconnected after a failed request. The timeout is counted from the
    // last successful cycle, so the device must be polled successfully first, otherwise it is counted
    // from the start of the steady clock and the result depends on the uptime of the machine
    Dev->DeviceConfig()->DeviceTimeout = std::chrono::hours(1);
    auto key1 = AddKey(0x1310);
    std::vector<PRegister> keys{key1.Status, key1.SinglePresses, key1.LongPresses};

    ExpectKeys(0, {});
    ReadRange(keys);
    CheckKey(key1, 0, 0, 0);

    // Key code is read, but reading of statuses fails: the request is not repeated for other channels
    // and the key press is not lost
    ExpectRead(0x100B, 2, {0x0101});
    ExpectReadWithoutAnswer(0x1310, KeyCount);
    ReadRange(keys);
    EXPECT_TRUE(key1.Status->GetErrorState().test(TRegister::ReadError));
    EXPECT_TRUE(key1.SinglePresses->GetErrorState().test(TRegister::ReadError));
    ExpectKeys(0, {0});
    ReadRange(keys);
    CheckKey(key1, 1, 1, 0);
}

TEST_F(TGtdIotDeviceTest, KeysAfterDisconnect)
{
    // The device must be marked as disconnected after the first failed request regardless of the uptime
    // of the machine, so the timeout counted from the start of the steady clock is disabled
    Dev->DeviceConfig()->DeviceTimeout = std::chrono::milliseconds::zero();
    auto key1 = AddKey(0x1310);
    std::vector<PRegister> keys{key1.Status, key1.SinglePresses, key1.LongPresses};

    // The key was pressed while the panel was not polled, the press is dropped with the rest of the state
    ExpectRead(0x100B, 2, {0x0101});
    ExpectReadWithoutAnswer(0x1310, KeyCount);
    ReadRange(keys);
    EXPECT_EQ(TDeviceConnectionState::DISCONNECTED, Dev->GetConnectionState());
    ExpectKeys(0, {0});
    ReadRange(keys);
    CheckKey(key1, 0, 0, 0);
}

TEST_F(TGtdIotDeviceTest, PollLimit)
{
    // At 9600 baud one byte takes 11 bits: 16 bytes of a register request are 19 ms.
    // Keys need one more request of 14 + 2 bytes per key, so both requests of one key are 32 bytes, 37 ms
    auto key = AddRegister("status", 0x1310);
    auto led = AddRegister(0x1008, 0, 1);
    EXPECT_TRUE(Dev->CreateRegisterRange()->Add(*SerialPort, led, std::chrono::milliseconds(19)));
    EXPECT_FALSE(Dev->CreateRegisterRange()->Add(*SerialPort, led, std::chrono::milliseconds(18)));
    EXPECT_TRUE(Dev->CreateRegisterRange()->Add(*SerialPort, key, std::chrono::milliseconds(37)));
    EXPECT_FALSE(Dev->CreateRegisterRange()->Add(*SerialPort, key, std::chrono::milliseconds(36)));

    // Request delay is added for every request: one for a register and two for keys
    Dev->DeviceConfig()->RequestDelay = std::chrono::milliseconds(10);
    EXPECT_TRUE(Dev->CreateRegisterRange()->Add(*SerialPort, led, std::chrono::milliseconds(29)));
    EXPECT_FALSE(Dev->CreateRegisterRange()->Add(*SerialPort, led, std::chrono::milliseconds(28)));
    EXPECT_TRUE(Dev->CreateRegisterRange()->Add(*SerialPort, key, std::chrono::milliseconds(57)));
    EXPECT_FALSE(Dev->CreateRegisterRange()->Add(*SerialPort, key, std::chrono::milliseconds(56)));
}

TEST_F(TGtdIotDeviceTest, Write)
{
    // Bit field is written with read-modify-write, the panel answers with the echo of the request
    SerialPort->Expect({0x01, 0x03, 0x10, 0x08, 0x00, 0x01, 0x01, 0x08},
                       {0x01, 0x03, 0x10, 0x08, 0x01, 0x00, 0xC1, 0x58});
    SerialPort->Expect({0x01, 0x06, 0x10, 0x08, 0x01, 0x02, 0x8C, 0x99},
                       {0x01, 0x06, 0x10, 0x08, 0x01, 0x02, 0x8C, 0x99});
    Dev->WriteRegister(*SerialPort, AddRegister(0x1008, 1, 1), 1);

    // The panel doesn't answer if the value is not accepted
    SerialPort->Expect({0x01, 0x06, 0x10, 0x03, 0x00, 0x40, 0x7C, 0xFA}, {});
    EXPECT_THROW(Dev->WriteRegister(*SerialPort, AddRegister(0x1003), 0x40), TSerialDeviceTransientErrorException);

    SerialPort->Expect({0x01, 0x06, 0x10, 0x08, 0x01, 0x02, 0x8C, 0x99},
                       {0x01, 0x06, 0x10, 0x08, 0x00, 0x02, 0x8D, 0x09});
    EXPECT_THROW(Dev->WriteRegister(*SerialPort, AddRegister(0x1008), 0x0102), TSerialDeviceTransientErrorException);
}

TEST(TGtdIotAddressTest, KeyAddressOutOfRange)
{
    TSerialDeviceFactory deviceFactory;
    RegisterProtocols(deviceFactory);
    const auto& factory = deviceFactory.GetProtocolParams("gtd_iot").factory->GetRegisterAddressFactory();
    auto loadAddress = [&factory](const std::string& regType, const std::string& property, const std::string& value) {
        Json::Value regCfg;
        regCfg["reg_type"] = regType;
        regCfg[property] = value;
        factory.LoadRegisterAddress(regCfg, factory.GetBaseRegisterAddress(), 0, 2);
    };

    // Keys are K1...K8, their status registers are 0x1310...0x1317
    EXPECT_THROW(loadAddress("status", "address", "0x1318"), TConfigParserException);
    EXPECT_NO_THROW(loadAddress("status", "address", "0x1317"));
    EXPECT_NO_THROW(loadAddress("holding", "address", "0x1318"));
    // A channel with only "write_address" has no address to check
    EXPECT_NO_THROW(loadAddress("status", "write_address", "0x1318"));
}

class TGtdIotIntegrationTest: public TSerialDeviceIntegrationTest, public TModbusExpectationsBase
{
protected:
    const char* ConfigPath() const override
    {
        return "configs/config-gtd-iot-test.json";
    }

    //! The template is built from a jinja file, so the generated one is used
    std::string GetTemplatePath() const override
    {
        return "../build/templates/";
    }

    void TearDown() override
    {
        SerialPort->Close();
        TSerialDeviceIntegrationTest::TearDown();
    }

    void ExpectRequest(const std::vector<int>& requestPdu, const std::vector<int>& responsePdu)
    {
        Expector()->Expect(WrapPDU(requestPdu), WrapPDU(responsePdu));
    }
};

TEST_F(TGtdIotIntegrationTest, Poll)
{
    // One register range is read per cycle. Polling mode is written by a setup item,
    // the panel answers with the echo of the request. LEDs and backlight are bits of one register
    ExpectRequest({0x06, 0x10, 0x03, 0x00, 0x00}, {0x06, 0x10, 0x03, 0x00, 0x00});
    ExpectRequest({0x03, 0x10, 0x08, 0x00, 0x01}, {0x03, 0x10, 0x08, 0x00, 0x00});
    Note() << "LoopOnce()";
    SerialDriver->LoopOnce();

    // The panel has one key, so the key code is followed by the status of one key
    ExpectRequest({0x03, 0x10, 0x0B, 0x00, 0x01}, {0x03, 0x00, 0x02, 0x00, 0x00});
    ExpectRequest({0x03, 0x13, 0x10, 0x00, 0x01}, {0x03, 0x00, 0x01, 0x00, 0x00});
    Note() << "LoopOnce()";
    SerialDriver->LoopOnce();

    ExpectRequest({0x03, 0x10, 0x08, 0x00, 0x01}, {0x03, 0x10, 0x08, 0x00, 0x00});
    Note() << "LoopOnce()";
    SerialDriver->LoopOnce();

    // K1 is pressed and released between polls: the state shows the press and the counter is incremented
    ExpectRequest({0x03, 0x10, 0x0B, 0x00, 0x01}, {0x03, 0x00, 0x02, 0x01, 0x01});
    ExpectRequest({0x03, 0x13, 0x10, 0x00, 0x01}, {0x03, 0x00, 0x01, 0x00, 0x00});
    Note() << "LoopOnce()";
    SerialDriver->LoopOnce();
}
