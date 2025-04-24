#include "devices/modbus_device.h"
#include "fake_serial_port.h"
#include "modbus_common.h"
#include "modbus_expectations.h"

#include <wblib/control.h>

using namespace std;

class TModbusTest: public TSerialDeviceTest, public TModbusExpectations
{
    typedef shared_ptr<TModbusDevice> PModbusDevice;

protected:
    void SetUp();
    set<int> VerifyQuery(PRegister reg = PRegister());

    virtual TModbusDeviceConfig GetDeviceConfig() const;

    PModbusDevice ModbusDev;

    PRegister ModbusCoil0;
    PRegister ModbusCoil1;
    PRegister ModbusDiscrete;
    PRegister ModbusHolding;
    PRegister ModbusInput;

    PRegister ModbusHoldingS64;

    PRegister ModbusHoldingU64Single;
    PRegister ModbusHoldingU16Single;
    PRegister ModbusHoldingU64Multi;
    PRegister ModbusHoldingU16Multi;

    PRegister ModbusHoldingU16WithAddressWrite;
    PRegister ModbusHoldingU16WithWriteBitOffset;

    PRegister ModbusHoldingStringRead;
    PRegister ModbusHoldingStringWrite;

    PRegister ModbusHoldingString8Read;
    PRegister ModbusHoldingString8Write;
};

TModbusDeviceConfig TModbusTest::GetDeviceConfig() const
{
    TModbusDeviceConfig config;
    config.CommonConfig = std::make_shared<TDeviceConfig>("modbus", std::to_string(0x01), "modbus");
    config.CommonConfig->MaxReadRegisters = 10;
    return config;
}

void TModbusTest::SetUp()
{
    SelectModbusType(MODBUS_RTU);
    TSerialDeviceTest::SetUp();

    auto modbusRtuTraits = std::make_unique<Modbus::TModbusRTUTraits>();

    ModbusDev = std::make_shared<TModbusDevice>(std::move(modbusRtuTraits),
                                                GetDeviceConfig(),
                                                SerialPort,
                                                DeviceFactory.GetProtocol("modbus"));
    ModbusCoil0 = ModbusDev->AddRegister(TRegisterConfig::Create(Modbus::REG_COIL, 0, U8));
    ModbusCoil1 = ModbusDev->AddRegister(TRegisterConfig::Create(Modbus::REG_COIL, 1, U8));
    ModbusDiscrete = ModbusDev->AddRegister(TRegisterConfig::Create(Modbus::REG_DISCRETE, 20, U8));
    ModbusHolding = ModbusDev->AddRegister(TRegisterConfig::Create(Modbus::REG_HOLDING, 70, U16));
    ModbusInput = ModbusDev->AddRegister(TRegisterConfig::Create(Modbus::REG_INPUT, 40, U16));
    ModbusHoldingS64 = ModbusDev->AddRegister(TRegisterConfig::Create(Modbus::REG_HOLDING, 30, S64));

    ModbusHoldingU64Single = ModbusDev->AddRegister(TRegisterConfig::Create(Modbus::REG_HOLDING_SINGLE, 90, U64));
    ModbusHoldingU16Single = ModbusDev->AddRegister(TRegisterConfig::Create(Modbus::REG_HOLDING_SINGLE, 94, U16));
    ModbusHoldingU64Multi = ModbusDev->AddRegister(TRegisterConfig::Create(Modbus::REG_HOLDING_MULTI, 95, U64));
    ModbusHoldingU16Multi = ModbusDev->AddRegister(TRegisterConfig::Create(Modbus::REG_HOLDING_MULTI, 99, U16));

    TRegisterDesc regAddrDesc;
    regAddrDesc.Address = std::make_shared<TUint32RegisterAddress>(110);
    regAddrDesc.WriteAddress = std::make_shared<TUint32RegisterAddress>(115);

    ModbusHoldingU16WithAddressWrite =
        ModbusDev->AddRegister(TRegisterConfig::Create(Modbus::REG_HOLDING, regAddrDesc, RegisterFormat::U16));

    regAddrDesc.Address = std::make_shared<TUint32RegisterAddress>(111);
    regAddrDesc.WriteAddress = std::make_shared<TUint32RegisterAddress>(116);
    regAddrDesc.DataWidth = 3;
    regAddrDesc.DataOffset = 2;

    ModbusHoldingU16WithWriteBitOffset =
        ModbusDev->AddRegister(TRegisterConfig::Create(Modbus::REG_HOLDING, regAddrDesc, RegisterFormat::U16));

    TRegisterDesc regStringDesc;
    regStringDesc.Address = std::make_shared<TUint32RegisterAddress>(120);
    regStringDesc.DataWidth = 16 * sizeof(char) * 8;
    ModbusHoldingStringRead =
        ModbusDev->AddRegister(TRegisterConfig::Create(Modbus::REG_HOLDING, regStringDesc, String));
    ModbusHoldingStringWrite =
        ModbusDev->AddRegister(TRegisterConfig::Create(Modbus::REG_HOLDING_MULTI, regStringDesc, String));

    TRegisterDesc regString8Desc;
    regString8Desc.Address = std::make_shared<TUint32RegisterAddress>(142);
    regString8Desc.DataWidth = 8 * sizeof(char) * 8;
    ModbusHoldingString8Read =
        ModbusDev->AddRegister(TRegisterConfig::Create(Modbus::REG_HOLDING, regString8Desc, String8));
    ModbusHoldingString8Write =
        ModbusDev->AddRegister(TRegisterConfig::Create(Modbus::REG_HOLDING_MULTI, regString8Desc, String8));

    SerialPort->Open();
}

set<int> TModbusTest::VerifyQuery(PRegister reg)
{
    std::list<PRegisterRange> ranges;

    if (!reg) {
        {
            auto r = ModbusDev->CreateRegisterRange();
            r->Add(ModbusCoil0, std::chrono::milliseconds::max());
            r->Add(ModbusCoil1, std::chrono::milliseconds::max());
            ranges.push_back(r);
        }
        {
            auto r = ModbusDev->CreateRegisterRange();
            r->Add(ModbusDiscrete, std::chrono::milliseconds::max());
            ranges.push_back(r);
        }
        {
            auto r = ModbusDev->CreateRegisterRange();
            r->Add(ModbusHolding, std::chrono::milliseconds::max());
            ranges.push_back(r);
        }
        {
            auto r = ModbusDev->CreateRegisterRange();
            r->Add(ModbusInput, std::chrono::milliseconds::max());
            ranges.push_back(r);
        }
        {
            auto r = ModbusDev->CreateRegisterRange();
            r->Add(ModbusHoldingS64, std::chrono::milliseconds::max());
            ranges.push_back(r);
        }
    } else {
        auto r = ModbusDev->CreateRegisterRange();
        r->Add(reg, std::chrono::milliseconds::max());
        ranges.push_back(r);
    }
    set<int> readAddresses;
    set<int> errorRegisters;
    map<int, TRegisterValue> registerValues;

    for (auto range: ranges) {
        ModbusDev->ReadRegisterRange(range);
        for (auto& reg: range->RegisterList()) {
            auto addr = GetUint32RegisterAddress(reg->GetAddress());
            readAddresses.insert(addr);
            if (reg->GetErrorState().test(TRegister::TError::ReadError)) {
                errorRegisters.insert(addr);
            } else {
                registerValues[addr] = reg->GetValue();
            }
        }
    }

    for (auto registerValue: registerValues) {
        auto address = registerValue.first;
        auto value = to_string(registerValue.second.Get<uint64_t>());

        switch (address) {
            case 0:
                EXPECT_EQ(to_string(0x0), value);
                break;
            case 1:
                EXPECT_EQ(to_string(0x1), value);
                break;
            case 20:
                EXPECT_EQ(to_string(0x1), value);
                break;
            case 30:
                EXPECT_EQ(to_string(0x0102030405060708), value);
                break;
            case 40:
                EXPECT_EQ(to_string(0x66), value);
                break;
            case 70:
                EXPECT_EQ(to_string(0x15), value);
                break;
            default:
                throw runtime_error("register with wrong address " + to_string(address) + " in range");
        }
    }

    return errorRegisters;
}

TEST_F(TModbusTest, ReadHoldingRegiterWithWriteAddress)
{
    EnqueueHoldingReadU16ResponseWithWriteAddress();

    auto range = ModbusDev->CreateRegisterRange();
    range->Add(ModbusHoldingU16WithAddressWrite, std::chrono::milliseconds::max());
    ModbusDev->ReadRegisterRange(range);
    auto registerList = range->RegisterList();
    EXPECT_EQ(registerList.size(), 1);
    auto reg = registerList.front();
    EXPECT_EQ(GetUint32RegisterAddress(reg->GetAddress()), 110);
    EXPECT_FALSE(reg->GetErrorState().test(TRegister::TError::ReadError));
    EXPECT_EQ(reg->GetValue(), 0x15);
}

TEST_F(TModbusTest, WriteHoldingRegiterWithWriteAddress)
{
    EnqueueHoldingWriteU16ResponseWithWriteAddress();
    EXPECT_EQ(GetUint32RegisterAddress(ModbusHoldingU16WithAddressWrite->GetAddress()), 110);
    EXPECT_EQ(GetUint32RegisterAddress(ModbusHoldingU16WithAddressWrite->GetWriteAddress()), 115);

    EXPECT_NO_THROW(ModbusDev->WriteRegister(ModbusHoldingU16WithAddressWrite, 0x119C));
}

TEST_F(TModbusTest, ReadHoldingRegiterWithOffsetWriteOptions)
{
    EnqueueHoldingReadU16ResponseWithOffsetWriteOptions();

    auto range = ModbusDev->CreateRegisterRange();
    range->Add(ModbusHoldingU16WithWriteBitOffset, std::chrono::milliseconds::max());
    ModbusDev->ReadRegisterRange(range);
    auto registerList = range->RegisterList();
    EXPECT_EQ(registerList.size(), 1);
    auto reg = registerList.front();
    EXPECT_EQ(GetUint32RegisterAddress(reg->GetAddress()), 111);
    EXPECT_FALSE(reg->GetErrorState().test(TRegister::TError::ReadError));
    EXPECT_EQ(reg->GetValue(), 5);
}

TEST_F(TModbusTest, ReadString)
{
    const std::vector<std::string> responses = {"2.4.2-rc1", "2.4.2-rc1", "2.4.2-rc1", "2.4.2-rc12345678"};
    EnqueueStringReadResponse(TModbusExpectations::TRAILING_ZEROS);
    EnqueueStringReadResponse(TModbusExpectations::ZERO_AND_TRASH);
    EnqueueStringReadResponse(TModbusExpectations::TRAILING_FF);
    EnqueueStringReadResponse(TModbusExpectations::FULL_OF_CHARS);

    for (int i = 0; i < 4; ++i) {
        auto range = ModbusDev->CreateRegisterRange();
        range->Add(ModbusHoldingStringRead, std::chrono::milliseconds::max());
        ModbusDev->ReadRegisterRange(range);
        auto registerList = range->RegisterList();
        EXPECT_EQ(registerList.size(), 1);
        auto reg = registerList.front();
        EXPECT_EQ(GetUint32RegisterAddress(reg->GetAddress()), 120);
        EXPECT_FALSE(reg->GetErrorState().test(TRegister::TError::ReadError));
        EXPECT_EQ(reg->GetValue().Get<std::string>(), responses[i]);
    }

    SerialPort->Close();
}

TEST_F(TModbusTest, WriteString)
{
    EnqueueStringWriteResponse();
    ModbusDev->WriteRegister(ModbusHoldingStringWrite, TRegisterValue("Lateralus"));
    SerialPort->Close();
}

TEST_F(TModbusTest, ReadString8)
{
    const std::vector<std::string> responses = {"2.4.2-rc1", "2.4.2-rc1", "2.4.2-rc1", "2.4.2-rc12345678"};
    EnqueueString8ReadResponse(TModbusExpectations::TRAILING_ZEROS);
    EnqueueString8ReadResponse(TModbusExpectations::ZERO_AND_TRASH);
    EnqueueString8ReadResponse(TModbusExpectations::TRAILING_FF);
    EnqueueString8ReadResponse(TModbusExpectations::FULL_OF_CHARS);

    for (int i = 0; i < 4; ++i) {
        auto range = ModbusDev->CreateRegisterRange();
        range->Add(ModbusHoldingString8Read, std::chrono::milliseconds::max());
        ModbusDev->ReadRegisterRange(range);
        auto registerList = range->RegisterList();
        EXPECT_EQ(registerList.size(), 1);
        auto reg = registerList.front();
        EXPECT_EQ(GetUint32RegisterAddress(reg->GetAddress()), 142);
        EXPECT_FALSE(reg->GetErrorState().test(TRegister::TError::ReadError));
        EXPECT_EQ(reg->GetValue().Get<std::string>(), responses[i]);
    }

    SerialPort->Close();
}

TEST_F(TModbusTest, WriteString8)
{
    EnqueueString8WriteResponse();
    ModbusDev->WriteRegister(ModbusHoldingString8Write, TRegisterValue("Lateralus"));
    SerialPort->Close();
}

TEST_F(TModbusTest, WriteHoldingRegiterWithOffsetWriteOptions)
{
    EnqueueHoldingWriteU16ResponseWithOffsetWriteOptions();

    EXPECT_NO_THROW(ModbusDev->WriteRegister(ModbusHoldingU16WithWriteBitOffset, 0x119D));
}

TEST_F(TModbusTest, WriteOnlyHoldingRegiter)
{
    PRegister ModbusHoldingU16WriteOnly;

    TRegisterDesc regAddrDesc;
    regAddrDesc.WriteAddress = std::make_shared<TUint32RegisterAddress>(115);

    ModbusHoldingU16WriteOnly =
        ModbusDev->AddRegister(TRegisterConfig::Create(Modbus::REG_HOLDING, regAddrDesc, RegisterFormat::U16));
    EXPECT_TRUE(ModbusHoldingU16WriteOnly->AccessType == TRegisterConfig::EAccessType::WRITE_ONLY);
}

TEST_F(TModbusTest, WriteOnlyHoldingRegiterNeg)
{
    TRegisterDesc regAddrDesc;

    EXPECT_THROW(TRegisterConfig::Create(Modbus::REG_HOLDING, regAddrDesc, RegisterFormat::U16),
                 TSerialDeviceException);
}

TEST_F(TModbusTest, Query)
{
    EnqueueCoilReadResponse();
    EnqueueDiscreteReadResponse();
    EnqueueHoldingReadU16Response();
    EnqueueInputReadU16Response();
    EnqueueHoldingReadS64Response();

    ASSERT_EQ(0, VerifyQuery().size()); // we don't expect any errors to occur here
    SerialPort->Close();
}

TEST_F(TModbusTest, HoldingSingleMulti)
{
    EnqueueHoldingSingleWriteU16Response();
    EnqueueHoldingSingleWriteU64Response();
    EnqueueHoldingMultiWriteU16Response();
    EnqueueHoldingMultiWriteU64Response();

    ModbusDev->WriteRegister(ModbusHoldingU16Single, 0x0f41);
    ModbusDev->WriteRegister(ModbusHoldingU64Single, 0x01020304050607);
    ModbusDev->WriteRegister(ModbusHoldingU16Multi, 0x0123);
    ModbusDev->WriteRegister(ModbusHoldingU64Multi, 0x0123456789ABCDEF);

    SerialPort->Close();
}

TEST_F(TModbusTest, Errors)
{
    EnqueueCoilReadResponse(1);
    EnqueueDiscreteReadResponse(2);
    EnqueueHoldingReadU16Response();
    EnqueueInputReadU16Response();
    EnqueueHoldingReadS64Response();

    set<int> expectedAddresses{0, 1, 20}; // errors in 2 coils and 1 input
    auto errorAddresses = VerifyQuery();

    ASSERT_EQ(expectedAddresses, errorAddresses);
    SerialPort->Close();
}

TEST_F(TModbusTest, CRCError)
{
    EnqueueInvalidCRCCoilReadResponse();

    auto r = ModbusDev->CreateRegisterRange();
    r->Add(ModbusCoil0, std::chrono::milliseconds::max());
    ModbusDev->ReadRegisterRange(r);

    SerialPort->Close();
}

TEST_F(TModbusTest, WrongResponseDataSize)
{
    EnqueueWrongDataSizeReadResponse();

    auto r = ModbusDev->CreateRegisterRange();
    r->Add(ModbusCoil0, std::chrono::milliseconds::max());
    ModbusDev->ReadRegisterRange(r);

    SerialPort->Close();
}

TEST_F(TModbusTest, WrongSlaveId)
{
    EnqueueWrongSlaveIdCoilReadResponse();

    EXPECT_EQ(1, VerifyQuery({ModbusCoil0}).size());

    SerialPort->Close();
}

TEST_F(TModbusTest, WrongFunctionCode)
{
    EnqueueWrongFunctionCodeCoilReadResponse();

    EXPECT_EQ(1, VerifyQuery({ModbusCoil0}).size());

    SerialPort->Close();
}

TEST_F(TModbusTest, WrongFunctionCodeWithException)
{
    EnqueueWrongFunctionCodeCoilReadResponse(0x2);

    EXPECT_EQ(1, VerifyQuery({ModbusCoil0}).size());

    SerialPort->Close();
}

TEST_F(TModbusTest, WrongSlaveIdWrite)
{
    EnqueueWrongSlaveIdCoilWriteResponse();

    try {
        ModbusDev->WriteRegister(ModbusCoil0, 0xFF);
        EXPECT_FALSE(true);
    } catch (const TSerialDeviceTransientErrorException& e) {
        EXPECT_EQ(string("Serial protocol error: request and response slave id mismatch"), e.what());
    }

    SerialPort->Close();
}

TEST_F(TModbusTest, WrongFunctionCodeWrite)
{
    EnqueueWrongFunctionCodeCoilWriteResponse();

    try {
        ModbusDev->WriteRegister(ModbusCoil0, 0xFF);
        EXPECT_FALSE(true);
    } catch (const TSerialDeviceTransientErrorException& e) {
        EXPECT_EQ(string("Serial protocol error: request and response function code mismatch"), e.what());
    }

    SerialPort->Close();
}

TEST_F(TModbusTest, WrongFunctionCodeWithExceptionWrite)
{
    EnqueueWrongFunctionCodeCoilWriteResponse(0x2);

    try {
        ModbusDev->WriteRegister(ModbusCoil0, 0xFF);
        EXPECT_FALSE(true);
    } catch (const TSerialDeviceTransientErrorException& e) {
        EXPECT_EQ(string("Serial protocol error: request and response function code mismatch"), e.what());
    }

    SerialPort->Close();
}

TEST_F(TModbusTest, MinReadRegisters)
{
    EnqueueHoldingReadU16Min2ReadResponse();

    ModbusDev->DeviceConfig()->MinReadRegisters = 2;

    auto range = ModbusDev->CreateRegisterRange();
    range->Add(ModbusHoldingU16WithAddressWrite, std::chrono::milliseconds::max());
    ModbusDev->ReadRegisterRange(range);
    auto registerList = range->RegisterList();
    EXPECT_EQ(registerList.size(), 1);
    auto reg = registerList.front();
    EXPECT_EQ(GetUint32RegisterAddress(reg->GetAddress()), 110);
    EXPECT_FALSE(reg->GetErrorState().test(TRegister::TError::ReadError));
    EXPECT_EQ(reg->GetValue(), 0x15);
}

TEST_F(TModbusTest, SkipNoiseAtPacketEnd)
{
    EnqueueReadResponseWithNoiseAtTheEnd(true);
    EnqueueReadResponseWithNoiseAtTheEnd(false);

    auto modbusRtuTraits = std::make_unique<Modbus::TModbusRTUTraits>(true);
    auto deviceConfig = GetDeviceConfig();
    deviceConfig.CommonConfig->FrameTimeout = 0ms;
    auto dev = std::make_shared<TModbusDevice>(std::move(modbusRtuTraits),
                                               deviceConfig,
                                               SerialPort,
                                               DeviceFactory.GetProtocol("modbus"));

    auto range = dev->CreateRegisterRange();
    auto reg = dev->AddRegister(TRegisterConfig::Create(Modbus::REG_HOLDING, 0x272E, U16));
    range->Add(reg, std::chrono::milliseconds::max());
    // Read with noise
    EXPECT_NO_THROW(dev->ReadRegisterRange(range));
    // Read without noise
    EXPECT_NO_THROW(dev->ReadRegisterRange(range));
}

class TModbusIntegrationTest: public TSerialDeviceIntegrationTest, public TModbusExpectations
{
protected:
    enum TestMode
    {
        TEST_DEFAULT,
        TEST_HOLES,
        TEST_MAX_READ_REGISTERS,
        TEST_MAX_READ_REGISTERS_FIRST_CYCLE,
    };

    void SetUp() override;
    void TearDown() override;
    const char* ConfigPath() const override
    {
        return "configs/config-modbus-test.json";
    }
    void ExpectPollQueries(TestMode mode = TEST_DEFAULT);
    void InvalidateConfigPoll(TestMode mode = TEST_DEFAULT);
};

void TModbusIntegrationTest::SetUp()
{
    SelectModbusType(MODBUS_RTU);
    TSerialDeviceIntegrationTest::SetUp();
    ASSERT_TRUE(!!SerialPort);
}

void TModbusIntegrationTest::TearDown()
{
    SerialPort->Close();
    TSerialDeviceIntegrationTest::TearDown();
}

void TModbusIntegrationTest::ExpectPollQueries(TestMode mode)
{
    switch (mode) {
        case TEST_HOLES:
            EnqueueHoldingPackHoles10ReadResponse();
            break;
        case TEST_MAX_READ_REGISTERS:
            EnqueueHoldingPackMax3ReadResponse();
            break;
        case TEST_MAX_READ_REGISTERS_FIRST_CYCLE:
        case TEST_DEFAULT:
        default:
            EnqueueHoldingSeparateReadResponse();
            break;
    }
    // test different lengths and register types
    EnqueueHoldingReadS64Response();
    EnqueueHoldingReadF32Response();
    EnqueueHoldingReadU16Response();
    EnqueueInputReadU16Response();

    switch (mode) {
        case TEST_HOLES:
            EnqueueCoilsPackReadResponse();
            break;
        case TEST_MAX_READ_REGISTERS:
        case TEST_MAX_READ_REGISTERS_FIRST_CYCLE:
            EnqueueCoilReadResponse();
            Enqueue10CoilsMax3ReadResponse();
            break;
        case TEST_DEFAULT:
        default:
            EnqueueCoilReadResponse();
            Enqueue10CoilsReadResponse();
            break;
    }

    EnqueueDiscreteReadResponse();

    switch (mode) {
        case TEST_HOLES:
            EnqueueHoldingSingleReadResponse();
            EnqueueHoldingMultiReadResponse();
            break;
        case TEST_MAX_READ_REGISTERS:
        case TEST_MAX_READ_REGISTERS_FIRST_CYCLE:
            EnqueueHoldingSingleMax3ReadResponse();
            EnqueueHoldingMultiMax3ReadResponse();
            break;
        case TEST_DEFAULT:
        default:
            EnqueueHoldingSingleOneByOneReadResponse();
            EnqueueHoldingMultiOneByOneReadResponse();
            break;
    }
}

void TModbusIntegrationTest::InvalidateConfigPoll(TestMode mode)
{
    ExpectPollQueries(mode);
    Note() << "LoopOnce()";
    size_t n = (TEST_MAX_READ_REGISTERS_FIRST_CYCLE == mode) ? 21 : 18;
    for (size_t i = 0; i < n; ++i) {
        SerialDriver->LoopOnce();
    }
}

TEST_F(TModbusIntegrationTest, Poll)
{
    ExpectPollQueries();
    Note() << "LoopOnce()";
    for (auto i = 0; i < 18; ++i) {
        SerialDriver->LoopOnce();
    }
}

TEST_F(TModbusIntegrationTest, Write)
{
    PublishWaitOnValue("/devices/modbus-sample/controls/Coil 0/on", "1");
    PublishWaitOnValue("/devices/modbus-sample/controls/RGB/on", "10;20;30");
    PublishWaitOnValue("/devices/modbus-sample/controls/Holding S64/on", "81985529216486895");
    PublishWaitOnValue("/devices/modbus-sample/controls/Holding U16/on", "3905");
    PublishWaitOnValue("/devices/modbus-sample/controls/Holding U64 Single/on", "283686952306183");
    PublishWaitOnValue("/devices/modbus-sample/controls/Holding U64 Multi/on", "81985529216486895");

    EnqueueCoilWriteResponse();
    EnqueueRGBWriteResponse();
    EnqueueHoldingWriteS64Response();
    EnqueueHoldingWriteU16Response();
    EnqueueHoldingSingleWriteU64Response();
    EnqueueHoldingMultiWriteU64Response();

    EnqueueHoldingPartialPackReadResponse();
    EnqueueHoldingReadS64Response();
    EnqueueHoldingReadF32Response();
    EnqueueHoldingReadU16Response();
    EnqueueInputReadU16Response();
    EnqueueCoilOneByOneReadResponse();
    Enqueue10CoilsReadResponse();
    EnqueueDiscreteReadResponse();
    EnqueueHoldingSingleOneByOneReadResponse();
    EnqueueHoldingMultiOneByOneReadResponse();

    Note() << "LoopOnce()";
    for (auto i = 0; i < 17; ++i) {
        SerialDriver->LoopOnce();
    }
}

TEST_F(TModbusIntegrationTest, Errors)
{
    PublishWaitOnValue("/devices/modbus-sample/controls/Coil 0/on", "1");
    PublishWaitOnValue("/devices/modbus-sample/controls/Holding U16/on", "3905");
    PublishWaitOnValue("/devices/modbus-sample/controls/Holding U64 Single/on", "283686952306183");

    EnqueueCoilWriteResponse(0x1);
    EnqueueHoldingWriteU16Response(0x2);
    EnqueueHoldingSingleWriteU64Response(0x2);

    EnqueueHoldingSeparateReadResponse(0x3);
    EnqueueHoldingReadS64Response(0x4);
    EnqueueHoldingReadF32Response(0x5);
    EnqueueHoldingReadU16Response(0x6);
    EnqueueInputReadU16Response(0x8);
    EnqueueCoilReadResponse(0xa);
    Enqueue10CoilsReadResponse(0x54); // invalid exception code
    EnqueueDiscreteReadResponse(0xb);
    EnqueueHoldingSingleOneByOneReadResponse(0x2);
    EnqueueHoldingMultiOneByOneReadResponse(0x3);

    Note() << "LoopOnce()";
    for (auto i = 0; i < 18; ++i) {
        SerialDriver->LoopOnce();
    }
}

TEST_F(TModbusIntegrationTest, Holes)
{
    SerialPort->SetBaudRate(115200);
    // we check that driver issue long read requests for holding registers 4-18 and all coils
    Config->PortConfigs[0]->Devices[0]->DeviceConfig()->MaxRegHole = 10;
    Config->PortConfigs[0]->Devices[0]->DeviceConfig()->MaxBitHole = 80;
    // First cycle, read registers one by one to find unavailable registers
    ExpectPollQueries();
    Note() << "LoopOnce()";
    for (size_t i = 0; i < 18; ++i) {
        SerialDriver->LoopOnce();
    }
    // Second cycle with holes enabled
    ExpectPollQueries(TEST_HOLES);
    Note() << "LoopOnce() [holes enabled]";
    for (auto i = 0; i < 9; ++i) {
        SerialDriver->LoopOnce();
    }
}

TEST_F(TModbusIntegrationTest, HolesAutoDisable)
{
    SerialPort->SetBaudRate(115200);

    Config->PortConfigs[0]->Devices[0]->DeviceConfig()->MaxRegHole = 10;
    InvalidateConfigPoll();

    EnqueueHoldingPackHoles10ReadResponse(0x3); // this must result in auto-disabling holes feature
    EnqueueHoldingReadS64Response();
    EnqueueHoldingReadF32Response();
    EnqueueHoldingReadU16Response();
    EnqueueInputReadU16Response();
    EnqueueCoilReadResponse();
    Enqueue10CoilsReadResponse();
    EnqueueDiscreteReadResponse();
    EnqueueHoldingSingleReadResponse();
    EnqueueHoldingMultiReadResponse();

    Note() << "LoopOnce() [read with holes, error]";
    for (auto i = 0; i < 10; ++i) {
        SerialDriver->LoopOnce();
    }

    EnqueueHoldingPackReadResponse();
    EnqueueHoldingReadS64Response();
    EnqueueHoldingReadF32Response();
    EnqueueHoldingReadU16Response();
    EnqueueInputReadU16Response();
    EnqueueCoilReadResponse();
    Enqueue10CoilsReadResponse();
    EnqueueDiscreteReadResponse();
    EnqueueHoldingSingleReadResponse();
    EnqueueHoldingMultiReadResponse();

    Note() << "LoopOnce() [read without holes]";
    for (auto i = 0; i < 11; ++i) {
        SerialDriver->LoopOnce();
    }
}

TEST_F(TModbusIntegrationTest, MaxReadRegisters)
{
    // Normally registers 4-9 (6 in total) are read or written in a single request.
    // By limiting the max_read_registers to 3 we force driver to issue two requests
    //    for this register range instead of one

    Config->PortConfigs[0]->Devices[0]->DeviceConfig()->MaxReadRegisters = 3;
    InvalidateConfigPoll(TEST_MAX_READ_REGISTERS_FIRST_CYCLE);
    ExpectPollQueries(TEST_MAX_READ_REGISTERS);
    Note() << "LoopOnce()";
    for (auto i = 0; i < 17; ++i) {
        SerialDriver->LoopOnce();
    }
}

TEST_F(TModbusIntegrationTest, GuardInterval)
{
    Config->PortConfigs[0]->Devices[0]->DeviceConfig()->RequestDelay = chrono::microseconds(1000);
    InvalidateConfigPoll();
}

class TModbusReconnectTest: public TModbusIntegrationTest
{
public:
    const char* ConfigPath() const override
    {
        return "configs/config-modbus-reconnect.json";
    }
};

TEST_F(TModbusReconnectTest, Disconnect)
{
    EnqueueHoldingSingleWriteU16Response();
    EnqueueHoldingReadU16Response();
    Note() << "LoopOnce() [connected]";
    SerialDriver->LoopOnce();
    EnqueueHoldingReadU16Response();
    Note() << "LoopOnce() [on-line]";
    SerialDriver->LoopOnce();
    this_thread::sleep_for(std::chrono::milliseconds(10));
    EnqueueHoldingReadU16Response(0, true);
    Note() << "LoopOnce() [disconnected]";
    SerialDriver->LoopOnce();
    EnqueueHoldingSingleWriteU16Response();
    EnqueueHoldingReadU16Response();
    Note() << "LoopOnce() [connected2]";
    SerialDriver->LoopOnce();
}

class TModbusBitmasksIntegrationTest: public TModbusIntegrationTest
{
protected:
    const char* ConfigPath() const override
    {
        return "configs/config-modbus-bitmasks-test.json";
    }

    void ExpectPollQueries(bool afterWrite = false, bool afterWriteMultiple = false);
};

void TModbusBitmasksIntegrationTest::ExpectPollQueries(bool afterWriteSingle, bool afterWriteMultiple)
{
    EnqueueU8Shift0Bits8HoldingReadResponse();
    EnqueueU16Shift8HoldingReadResponse(afterWriteMultiple);
    EnqueueU8Shift0SingleBitHoldingReadResponse(afterWriteSingle);
    EnqueueU8Shift1SingleBitHoldingReadResponse(afterWriteSingle);
    EnqueueU8Shift2SingleBitHoldingReadResponse(afterWriteSingle);
}

TEST_F(TModbusBitmasksIntegrationTest, Poll)
{
    ExpectPollQueries();
    Note() << "LoopOnce()";
    for (auto i = 0; i < 5; ++i) {
        SerialDriver->LoopOnce();
    }
}

TEST_F(TModbusBitmasksIntegrationTest, SingleWrite)
{
    ExpectPollQueries();
    Note() << "LoopOnce()";
    for (auto i = 0; i < 5; ++i) {
        SerialDriver->LoopOnce();
    }

    PublishWaitOnValue("/devices/modbus-sample/controls/U8:1/on", "1");

    EnqueueU8Shift1SingleBitHoldingWriteResponse();
    EnqueueU8Shift0Bits8HoldingReadResponse();
    EnqueueU16Shift8HoldingReadResponse(false);
    // Read all single bit registers at once as they are located in the same modbus register
    EnqueueU8Shift0SingleBitHoldingReadResponse(true);
    Note() << "LoopOnce()";
    for (auto i = 0; i < 3; ++i) {
        SerialDriver->LoopOnce();
    }
}

class TModbusSeveralBitmasksIntegrationTest: public TModbusIntegrationTest
{
protected:
    const char* ConfigPath() const override
    {
        return "configs/config-modbus-several-bitmasks-test.json";
    }
};

TEST_F(TModbusSeveralBitmasksIntegrationTest, Poll)
{
    EnqueueInputReadResponse(16, {0xA1, 0xA2});
    EnqueueInputReadResponse(16, {0xA1, 0xA2});
    EnqueueInputReadResponse(17, {0xB3, 0xB4});
    EnqueueInputReadResponse(17, {0xB3, 0xB4});
    EnqueueInputReadResponse(18, {0xC5, 0xC6});
    EnqueueInputReadResponse(18, {0xC5, 0xC6});
    EnqueueInputReadResponse(16, {0x00, 0x02, 0x02, 0x04, 0xff, 0x04});

    Note() << "LoopOnce()";
    for (auto i = 0; i < 7; ++i) {
        SerialDriver->LoopOnce();
    }
}

class TModbusBitmasksU32IntegrationTest: public TModbusIntegrationTest
{
protected:
    const char* ConfigPath() const override
    {
        return "configs/config-modbus-bitmasks-u32-test.json";
    }

    void ExpectPollQueries(bool afterWrite = false);
};

TEST_F(TModbusBitmasksU32IntegrationTest, Poll)
{
    EnqueueU32BitsHoldingReadResponse(false);
    Note() << "LoopOnce()";
    for (auto i = 0; i < 4; ++i) {
        SerialDriver->LoopOnce();
    }
}

TEST_F(TModbusBitmasksU32IntegrationTest, Write)
{
    EnqueueU32BitsHoldingReadResponse(false);
    Note() << "LoopOnce()";
    for (auto i = 0; i < 4; ++i) {
        SerialDriver->LoopOnce();
    }

    PublishWaitOnValue("/devices/modbus-sample/controls/U32:1/on", "1");
    PublishWaitOnValue("/devices/modbus-sample/controls/U32:20/on", "4");
    PublishWaitOnValue("/devices/modbus-sample/controls/U32:1 le/on", "1");
    PublishWaitOnValue("/devices/modbus-sample/controls/U32:20 le/on", "4");

    EnqueueU32BitsHoldingWriteResponse();
    EnqueueU32BitsHoldingReadResponse(true);
    Note() << "LoopOnce()";
    for (auto i = 0; i < 4; ++i) {
        SerialDriver->LoopOnce();
    }
}

class TModbusUnavailableRegistersIntegrationTest: public TSerialDeviceIntegrationTest, public TModbusExpectations
{
protected:
    void SetUp() override
    {
        SelectModbusType(MODBUS_RTU);
        TSerialDeviceIntegrationTest::SetUp();
        ASSERT_TRUE(!!SerialPort);
    }

    void TearDown() override
    {
        SerialPort->Close();
        TSerialDeviceIntegrationTest::TearDown();
    }

    const char* ConfigPath() const override
    {
        return "configs/config-modbus-unavailable-registers-test.json";
    }
};

TEST_F(TModbusUnavailableRegistersIntegrationTest, UnavailableRegisterOnBorder)
{
    // we check that driver detects unavailable register on the ranges border and stops to read it
    EnqueueHoldingPackUnavailableOnBorderReadResponse();
    Note() << "LoopOnce() [one by one]";
    for (auto i = 0; i < 6; ++i) {
        SerialDriver->LoopOnce();
    }
    Note() << "LoopOnce() [new range]";
    SerialDriver->LoopOnce();
}

TEST_F(TModbusUnavailableRegistersIntegrationTest, UnavailableRegisterInTheMiddle)
{
    // we check that driver detects unavailable register in the middle of the range
    // It must split the range into two parts and exclude unavailable register from reading
    EnqueueHoldingPackUnavailableInTheMiddleReadResponse();
    Note() << "LoopOnce() [one by one]";
    for (auto i = 0; i < 6; ++i) {
        SerialDriver->LoopOnce();
    }
    Note() << "LoopOnce() [new range]";
    for (auto i = 0; i < 2; ++i) {
        SerialDriver->LoopOnce();
    }
}

TEST_F(TModbusUnavailableRegistersIntegrationTest, UnsupportedRegisterOnBorder)
{
    // Check that driver detects unsupported registers
    // It must remove unsupported registers from request if they are on borders of a range
    // Unsupported registers in the middle of a range must be polled with the whole range
    EnqueueHoldingUnsupportedOnBorderReadResponse();
    Note() << "LoopOnce() [first read]";
    for (auto i = 0; i < 6; ++i) {
        SerialDriver->LoopOnce();
    }
    Note() << "LoopOnce() [new range]";
    for (auto i = 0; i < 2; ++i) {
        SerialDriver->LoopOnce();
    }
}

class TModbusUnavailableRegistersAndHolesIntegrationTest: public TSerialDeviceIntegrationTest,
                                                          public TModbusExpectations
{
protected:
    void SetUp() override
    {
        SelectModbusType(MODBUS_RTU);
        TSerialDeviceIntegrationTest::SetUp();
        ASSERT_TRUE(!!SerialPort);
    }

    void TearDown() override
    {
        SerialPort->Close();
        TSerialDeviceIntegrationTest::TearDown();
    }

    const char* ConfigPath() const override
    {
        return "configs/config-modbus-unavailable-registers-and-holes-test.json";
    }
};

TEST_F(TModbusUnavailableRegistersAndHolesIntegrationTest, HolesAndUnavailable)
{
    // we check that driver disables holes feature and after that detects and excludes unavailable register
    Config->PortConfigs[0]->Devices[0]->DeviceConfig()->MaxRegHole = 10;

    EnqueueHoldingPackUnavailableAndHolesReadResponse();
    Note() << "LoopOnce() [one by one]";
    for (auto i = 0; i < 5; ++i) {
        SerialDriver->LoopOnce();
    }
    Note() << "LoopOnce() [with holes]";
    SerialDriver->LoopOnce();
    Note() << "LoopOnce() [new ranges]";
    for (auto i = 0; i < 2; ++i) {
        SerialDriver->LoopOnce();
    }
}

class TModbusContinuousRegisterReadTest: public TSerialDeviceIntegrationTest, public TModbusExpectations
{
protected:
    const char* ConfigPath() const override
    {
        return "configs/config-modbus-continuous-read-test.json";
    }
};

TEST_F(TModbusContinuousRegisterReadTest, Supported)
{
    EnqueueContinuousReadEnableResponse();
    EnqueueContinuousReadResponse();
    Note() << "LoopOnce() [one by one]";
    for (auto i = 0; i < 6; ++i) {
        SerialDriver->LoopOnce();
    }
    EnqueueContinuousReadResponse(false);
    Note() << "LoopOnce() [continuous]";
    for (auto i = 0; i < 4; ++i) {
        SerialDriver->LoopOnce();
    }
}

TEST_F(TModbusContinuousRegisterReadTest, NotSupported)
{
    EnqueueContinuousReadEnableResponse(false);
    EnqueueContinuousReadResponse();
    Note() << "LoopOnce() [one by one]";
    for (auto i = 0; i < 6; ++i) {
        SerialDriver->LoopOnce();
    }
    EnqueueContinuousReadResponse();
    Note() << "LoopOnce() [separated]";
    for (auto i = 0; i < 6; ++i) {
        SerialDriver->LoopOnce();
    }
}

class TModbusLittleEndianRegisterTest: public TSerialDeviceIntegrationTest, public TModbusExpectations
{
protected:
    const char* ConfigPath() const override
    {
        return "configs/config-modbus-little-endian-test.json";
    }
};

TEST_F(TModbusLittleEndianRegisterTest, Read)
{
    EnqueueLittleEndianReadResponses();
    for (auto i = 0; i < 5; ++i) {
        SerialDriver->LoopOnce();
    }
}

TEST_F(TModbusLittleEndianRegisterTest, Write)
{
    EnqueueLittleEndianReadResponses();
    for (auto i = 0; i < 5; ++i) {
        SerialDriver->LoopOnce();
    }

    PublishWaitOnValue("/devices/modbus-sample/controls/U8/on", "1");
    PublishWaitOnValue("/devices/modbus-sample/controls/U16/on", std::to_string(0x0304));
    PublishWaitOnValue("/devices/modbus-sample/controls/U24/on", std::to_string(0x050607));
    PublishWaitOnValue("/devices/modbus-sample/controls/U32/on", std::to_string(0x08090A0B));
    PublishWaitOnValue("/devices/modbus-sample/controls/U64/on", std::to_string(0x0C0D0E0F11121314));

    EnqueueLittleEndianWriteResponses();
    EnqueueLittleEndianReadResponses();
    for (auto i = 0; i < 5; ++i) {
        SerialDriver->LoopOnce();
    }
}
