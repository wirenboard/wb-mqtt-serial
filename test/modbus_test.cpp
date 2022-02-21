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

    virtual PDeviceConfig GetDeviceConfig();

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
};

PDeviceConfig TModbusTest::GetDeviceConfig()
{
    PDeviceConfig dev = std::make_shared<TDeviceConfig>("modbus", std::to_string(0x01), "modbus");
    dev->MaxReadRegisters = 10;
    return dev;
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
    ModbusCoil0 = TRegister::Intern(ModbusDev, TRegisterConfig::Create(Modbus::REG_COIL, 0, U8));
    ModbusCoil1 = TRegister::Intern(ModbusDev, TRegisterConfig::Create(Modbus::REG_COIL, 1, U8));
    ModbusDiscrete = TRegister::Intern(ModbusDev, TRegisterConfig::Create(Modbus::REG_DISCRETE, 20, U8));
    ModbusHolding = TRegister::Intern(ModbusDev, TRegisterConfig::Create(Modbus::REG_HOLDING, 70, U16));
    ModbusInput = TRegister::Intern(ModbusDev, TRegisterConfig::Create(Modbus::REG_INPUT, 40, U16));
    ModbusHoldingS64 = TRegister::Intern(ModbusDev, TRegisterConfig::Create(Modbus::REG_HOLDING, 30, S64));

    ModbusHoldingU64Single = TRegister::Intern(ModbusDev, TRegisterConfig::Create(Modbus::REG_HOLDING_SINGLE, 90, U64));
    ModbusHoldingU16Single = TRegister::Intern(ModbusDev, TRegisterConfig::Create(Modbus::REG_HOLDING_SINGLE, 94, U16));
    ModbusHoldingU64Multi = TRegister::Intern(ModbusDev, TRegisterConfig::Create(Modbus::REG_HOLDING_MULTI, 95, U64));
    ModbusHoldingU16Multi = TRegister::Intern(ModbusDev, TRegisterConfig::Create(Modbus::REG_HOLDING_MULTI, 99, U16));

    TRegisterDesc regAddrDesc;
    regAddrDesc.Address = std::make_shared<TUint32RegisterAddress>(110);
    regAddrDesc.WriteAddress = std::make_shared<TUint32RegisterAddress>(115);

    ModbusHoldingU16WithAddressWrite =
        TRegister::Intern(ModbusDev, TRegisterConfig::Create(Modbus::REG_HOLDING, regAddrDesc, RegisterFormat::U16));

    regAddrDesc.Address = std::make_shared<TUint32RegisterAddress>(111);
    regAddrDesc.WriteAddress = std::make_shared<TUint32RegisterAddress>(116);
    regAddrDesc.BitWidth = 3;
    regAddrDesc.BitOffset = 2;

    ModbusHoldingU16WithWriteBitOffset =
        TRegister::Intern(ModbusDev, TRegisterConfig::Create(Modbus::REG_HOLDING, regAddrDesc, RegisterFormat::U16));

    SerialPort->Open();
}

set<int> TModbusTest::VerifyQuery(PRegister reg)
{
    std::list<PRegisterRange> ranges;

    if (!reg) {
        auto r = ModbusDev->CreateRegisterRange(ModbusCoil0);
        r->Add(ModbusCoil1, std::chrono::milliseconds::max());
        ranges.push_back(r);
        ranges.push_back(ModbusDev->CreateRegisterRange(ModbusDiscrete));
        ranges.push_back(ModbusDev->CreateRegisterRange(ModbusHolding));
        ranges.push_back(ModbusDev->CreateRegisterRange(ModbusInput));
        ranges.push_back(ModbusDev->CreateRegisterRange(ModbusHoldingS64));
    } else {
        ranges.push_back(ModbusDev->CreateRegisterRange(reg));
    }
    set<int> readAddresses;
    set<int> errorRegisters;
    map<int, uint64_t> registerValues;

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
        auto value = to_string(registerValue.second);

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

    auto range = ModbusDev->CreateRegisterRange(ModbusHoldingU16WithAddressWrite);
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

    auto range = ModbusDev->CreateRegisterRange(ModbusHoldingU16WithWriteBitOffset);
    ModbusDev->ReadRegisterRange(range);
    auto registerList = range->RegisterList();
    EXPECT_EQ(registerList.size(), 1);
    auto reg = registerList.front();
    EXPECT_EQ(GetUint32RegisterAddress(reg->GetAddress()), 111);
    EXPECT_FALSE(reg->GetErrorState().test(TRegister::TError::ReadError));
    EXPECT_EQ(reg->GetValue(), 5);
}

TEST_F(TModbusTest, WriteHoldingRegiterWithOffsetWriteOptions)
{
    EnqueueHoldingWriteU16ResponseWithOffsetWriteOptions();

    EXPECT_NO_THROW(ModbusDev->WriteRegister(ModbusHoldingU16WithWriteBitOffset, 0x119D));
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

    ModbusDev->ReadRegisterRange(ModbusDev->CreateRegisterRange(ModbusCoil0));

    SerialPort->Close();
}

TEST_F(TModbusTest, WrongResponseDataSize)
{
    EnqueueWrongDataSizeReadResponse();

    ModbusDev->ReadRegisterRange(ModbusDev->CreateRegisterRange(ModbusCoil0));

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

    void SetUp();
    void TearDown();
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
    SerialDriver->ClearDevices();
    SerialDriver = make_shared<TMQTTSerialDriver>(Driver, Config);

    ExpectPollQueries(mode);
    Note() << "LoopOnce()";
    SerialDriver->LoopOnce();
}

TEST_F(TModbusIntegrationTest, Poll)
{
    ExpectPollQueries();
    Note() << "LoopOnce()";
    SerialDriver->LoopOnce();
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

    SerialDriver->LoopOnce();
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
    SerialDriver->LoopOnce();
}

TEST_F(TModbusIntegrationTest, Holes)
{
    // we check that driver issue long read requests for holding registers 4-18 and all coils
    Config->PortConfigs[0]->Devices[0]->DeviceConfig()->MaxRegHole = 10;
    Config->PortConfigs[0]->Devices[0]->DeviceConfig()->MaxBitHole = 80;
    // First cycle, read registers one by one to find unavailable registers
    InvalidateConfigPoll();
    // Second cycle with holes enabled
    ExpectPollQueries(TEST_HOLES);
    Note() << "LoopOnce() [holes enabled]";
    SerialDriver->LoopOnce();
}

TEST_F(TModbusIntegrationTest, HolesAutoDisable)
{
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
    SerialDriver->LoopOnce();

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
    SerialDriver->LoopOnce();
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
    SerialDriver->LoopOnce();
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
    SerialDriver->LoopOnce();
}

TEST_F(TModbusBitmasksIntegrationTest, SingleWrite)
{
    ExpectPollQueries();
    Note() << "LoopOnce()";
    SerialDriver->LoopOnce();

    PublishWaitOnValue("/devices/modbus-sample/controls/U8:1/on", "1");

    EnqueueU8Shift1SingleBitHoldingWriteResponse();
    ExpectPollQueries(true);
    Note() << "LoopOnce()";
    SerialDriver->LoopOnce();
}

class TModbusUnavailableRegistersIntegrationTest: public TSerialDeviceIntegrationTest, public TModbusExpectations
{
protected:
    void SetUp()
    {
        SelectModbusType(MODBUS_RTU);
        TSerialDeviceIntegrationTest::SetUp();
        ASSERT_TRUE(!!SerialPort);
    }

    void TearDown()
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
    SerialDriver->ClearDevices();
    SerialDriver = make_shared<TMQTTSerialDriver>(Driver, Config);

    EnqueueHoldingPackUnavailableOnBorderReadResponse();
    Note() << "LoopOnce() [one by one]";
    SerialDriver->LoopOnce();
    Note() << "LoopOnce() [new range]";
    SerialDriver->LoopOnce();
}

TEST_F(TModbusUnavailableRegistersIntegrationTest, UnavailableRegisterInTheMiddle)
{
    // we check that driver detects unavailable register in the middle of the range
    // It must split the range into two parts and exclure unavailable register from reading
    SerialDriver->ClearDevices();
    SerialDriver = make_shared<TMQTTSerialDriver>(Driver, Config);

    EnqueueHoldingPackUnavailableInTheMiddleReadResponse();
    Note() << "LoopOnce() [one by one]";
    SerialDriver->LoopOnce();
    Note() << "LoopOnce() [new range]";
    SerialDriver->LoopOnce();
}

TEST_F(TModbusUnavailableRegistersIntegrationTest, UnsupportedRegisterOnBorder)
{
    // Check that driver detects unsupported registers
    // It must remove unsupported registers from request if they are on borders of a range
    // Unsupported registers in the middle of a range must be polled with the whole range
    SerialDriver->ClearDevices();
    SerialDriver = make_shared<TMQTTSerialDriver>(Driver, Config);

    EnqueueHoldingUnsupportedOnBorderReadResponse();
    Note() << "LoopOnce() [first read]";
    SerialDriver->LoopOnce();
    Note() << "LoopOnce() [new range]";
    SerialDriver->LoopOnce();
}

class TModbusUnavailableRegistersAndHolesIntegrationTest: public TSerialDeviceIntegrationTest,
                                                          public TModbusExpectations
{
protected:
    void SetUp()
    {
        SelectModbusType(MODBUS_RTU);
        TSerialDeviceIntegrationTest::SetUp();
        ASSERT_TRUE(!!SerialPort);
    }

    void TearDown()
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

    SerialDriver->ClearDevices();
    SerialDriver = make_shared<TMQTTSerialDriver>(Driver, Config);

    EnqueueHoldingPackUnavailableAndHolesReadResponse();
    Note() << "LoopOnce() [one by one]";
    SerialDriver->LoopOnce();
    Note() << "LoopOnce() [with holes]";
    SerialDriver->LoopOnce();
    Note() << "LoopOnce() [new ranges]";
    SerialDriver->LoopOnce();
}
