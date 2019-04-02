#include "fake_serial_port.h"
#include "modbus_expectations.h"
#include "modbus_device.h"
#include "modbus_common.h"

using namespace std;


class TModbusTest: public TSerialDeviceTest, public TModbusExpectations
{
    typedef shared_ptr<TModbusDevice> PModbusDevice;
protected:
    void SetUp();
    set<int> VerifyQuery(list<PRegister> registerList = list<PRegister>());

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
};

PDeviceConfig TModbusTest::GetDeviceConfig()
{
    return std::make_shared<TDeviceConfig>("modbus", std::to_string(0x01), "modbus");
}

void TModbusTest::SetUp()
{
    SelectModbusType(MODBUS_RTU);
    TSerialDeviceTest::SetUp();

    ModbusDev = std::make_shared<TModbusDevice>(GetDeviceConfig(), SerialPort,
                                TSerialDeviceFactory::GetProtocol("modbus"));
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

    SerialPort->Open();
}

set<int> TModbusTest::VerifyQuery(list<PRegister> registerList)
{
    std::list<PRegisterRange> ranges;

    if (registerList.empty()) {
        registerList = {
            ModbusCoil0, ModbusCoil1, ModbusDiscrete, ModbusHolding, ModbusInput, ModbusHoldingS64
        };

        ranges = ModbusDev->SplitRegisterList(registerList);

        if (ranges.size() != 5) {
            throw runtime_error("wrong range count: " + to_string(ranges.size()));
        }
    } else {
        ranges = ModbusDev->SplitRegisterList(registerList);
    }
    set<int> readAddresses;
    set<int> errorRegisters;
    map<int, uint64_t> registerValues;

    for (auto range: ranges) {
        ModbusDev->ReadRegisterRange(range);
        range->MapRange([&](PRegister reg, uint64_t value){
            registerValues[reg->Address] = value;
            readAddresses.insert(reg->Address);
        }, [&](PRegister reg){
            readAddresses.insert(reg->Address);
            errorRegisters.insert(reg->Address);
        });
    }

    EXPECT_EQ(to_string(registerList.size()), to_string(readAddresses.size()));

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

    set<int> expectedAddresses {0, 1, 20}; // errors in 2 coils and 1 input
    auto errorAddresses = VerifyQuery();

    ASSERT_EQ(expectedAddresses, errorAddresses);
    SerialPort->Close();
}

TEST_F(TModbusTest, CRCError)
{
    EnqueueInvalidCRCCoilReadResponse();

    ModbusDev->ReadRegisterRange(ModbusDev->SplitRegisterList({ ModbusCoil0 }).front());

    SerialPort->Close();
}

TEST_F(TModbusTest, WrongResponseDataSize)
{
    EnqueueWrongDataSizeReadResponse();

    ModbusDev->ReadRegisterRange(ModbusDev->SplitRegisterList({ ModbusCoil0 }).front());

    SerialPort->Close();
}

TEST_F(TModbusTest, WrongSlaveId)
{
    EnqueueWrongSlaveIdCoilReadResponse();

    EXPECT_EQ(1, VerifyQuery({ ModbusCoil0 }).size());

    SerialPort->Close();
}

TEST_F(TModbusTest, WrongFunctionCode)
{
    EnqueueWrongFunctionCodeCoilReadResponse();

    EXPECT_EQ(1, VerifyQuery({ ModbusCoil0 }).size());

    SerialPort->Close();
}

TEST_F(TModbusTest, WrongFunctionCodeWithException)
{
    EnqueueWrongFunctionCodeCoilReadResponse(0x2);

    EXPECT_EQ(1, VerifyQuery({ ModbusCoil0 }).size());

    SerialPort->Close();
}

TEST_F(TModbusTest, WrongSlaveIdWrite)
{
    EnqueueWrongSlaveIdCoilWriteResponse();

    try {
        ModbusDev->WriteRegister(ModbusCoil0, 0xFF);
        EXPECT_FALSE(true);
    } catch (const TSerialDeviceTransientErrorException & e) {
        EXPECT_EQ(string("Serial protocol error: failed to write (type 2) @ 0: Serial protocol error: request and response slave id mismatch"), e.what());
    }

    SerialPort->Close();
}

TEST_F(TModbusTest, WrongFunctionCodeWrite)
{
    EnqueueWrongFunctionCodeCoilWriteResponse();

    try {
        ModbusDev->WriteRegister(ModbusCoil0, 0xFF);
        EXPECT_FALSE(true);
    } catch (const TSerialDeviceTransientErrorException & e) {
        EXPECT_EQ(string("Serial protocol error: failed to write (type 2) @ 0: Serial protocol error: request and response function code mismatch"), e.what());
    }

    SerialPort->Close();
}

TEST_F(TModbusTest, WrongFunctionCodeWithExceptionWrite)
{
    EnqueueWrongFunctionCodeCoilWriteResponse(0x2);

    try {
        ModbusDev->WriteRegister(ModbusCoil0, 0xFF);
        EXPECT_FALSE(true);
    } catch (const TSerialDeviceTransientErrorException & e) {
        EXPECT_EQ(string("Serial protocol error: failed to write (type 2) @ 0: Serial protocol error: request and response function code mismatch"), e.what());
    }

    SerialPort->Close();
}


class TModbusIntegrationTest: public TSerialDeviceIntegrationTest, public TModbusExpectations
{
protected:
    enum TestMode {TEST_DEFAULT, TEST_HOLES, TEST_MAX_READ_REGISTERS};

    void SetUp();
    void TearDown();
    const char* ConfigPath() const override { return "configs/config-modbus-test.json"; }
    void ExpectPollQueries(TestMode mode = TEST_DEFAULT);
    void InvalidateConfigPoll(TestMode mode = TEST_DEFAULT);
};

void TModbusIntegrationTest::SetUp()
{
    SelectModbusType(MODBUS_RTU);
    TSerialDeviceIntegrationTest::SetUp();
    Observer->SetUp();
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
    case TEST_DEFAULT:
    default:
        EnqueueHoldingPackReadResponse();
        break;
    }
    // test different lengths and register types
    EnqueueHoldingReadS64Response();
    EnqueueHoldingReadF32Response();
    EnqueueHoldingReadU16Response();
    EnqueueInputReadU16Response();
    EnqueueCoilReadResponse();

    if (mode == TEST_MAX_READ_REGISTERS) {
        Enqueue10CoilsMax3ReadResponse();
    } else {
        Enqueue10CoilsReadResponse();
    }

    EnqueueDiscreteReadResponse();

    if (mode == TEST_MAX_READ_REGISTERS) {
        EnqueueHoldingSingleMax3ReadResponse();
        EnqueueHoldingMultiMax3ReadResponse();
    } else {
        EnqueueHoldingSingleReadResponse();
        EnqueueHoldingMultiReadResponse();
    }
}

void TModbusIntegrationTest::InvalidateConfigPoll(TestMode mode)
{
    TSerialDeviceFactory::RemoveDevice(TSerialDeviceFactory::GetDevice("1", "modbus", SerialPort));
    Observer = make_shared<TMQTTSerialObserver>(MQTTClient, Config, SerialPort);

    Observer->SetUp();

    ExpectPollQueries(mode);
    Note() << "LoopOnce()";
    Observer->LoopOnce();
}


TEST_F(TModbusIntegrationTest, Poll)
{
    ExpectPollQueries();
    Note() << "LoopOnce()";
    Observer->LoopOnce();
}

TEST_F(TModbusIntegrationTest, Write)
{
    MQTTClient->Publish(nullptr, "/devices/modbus-sample/controls/Coil 0/on", "1");
    MQTTClient->Publish(nullptr, "/devices/modbus-sample/controls/RGB/on", "10;20;30");
    MQTTClient->Publish(nullptr, "/devices/modbus-sample/controls/Holding S64/on", "81985529216486895");
    MQTTClient->Publish(nullptr, "/devices/modbus-sample/controls/Holding U16/on", "3905");
    MQTTClient->Publish(nullptr, "/devices/modbus-sample/controls/Holding U64 Single/on", "283686952306183");
    MQTTClient->Publish(nullptr, "/devices/modbus-sample/controls/Holding U64 Multi/on", "81985529216486895");

    EnqueueCoilWriteResponse();
    EnqueueRGBWriteResponse();
    EnqueueHoldingWriteS64Response();
    EnqueueHoldingWriteU16Response();
    EnqueueHoldingSingleWriteU64Response();
    EnqueueHoldingMultiWriteU64Response();

    ExpectPollQueries();
    Note() << "LoopOnce()";
    Observer->LoopOnce();
}

TEST_F(TModbusIntegrationTest, Errors)
{
    MQTTClient->Publish(nullptr, "/devices/modbus-sample/controls/Coil 0/on", "1");
    MQTTClient->Publish(nullptr, "/devices/modbus-sample/controls/Holding U16/on", "3905");
    MQTTClient->Publish(nullptr, "/devices/modbus-sample/controls/Holding U64 Single/on", "283686952306183");

    EnqueueCoilWriteResponse(0x1);
    EnqueueHoldingWriteU16Response(0x2);
    EnqueueHoldingSingleWriteU64Response(0x2);

    EnqueueHoldingPackReadResponse(0x3);
    EnqueueHoldingReadS64Response(0x4);
    EnqueueHoldingReadF32Response(0x5);
    EnqueueHoldingReadU16Response(0x6);
    EnqueueInputReadU16Response(0x8);
    EnqueueCoilReadResponse(0xa);
    Enqueue10CoilsReadResponse(0x54);   // invalid exception code
    EnqueueDiscreteReadResponse(0xb);
    EnqueueHoldingSingleReadResponse(0x2);
    EnqueueHoldingMultiReadResponse(0x3);

    Note() << "LoopOnce()";
    Observer->LoopOnce();
}

TEST_F(TModbusIntegrationTest, Holes)
{
    // we check that driver issue long read request, reading registers 4-18 at once
    Config->PortConfigs[0]->DeviceConfigs[0]->MaxRegHole = 10;
    Config->PortConfigs[0]->DeviceConfigs[0]->MaxBitHole = 80;
    InvalidateConfigPoll(TEST_HOLES);
}

TEST_F(TModbusIntegrationTest, HolesAutoDisable)
{
    // we check that driver issue long read request, reading registers 4-18 at once
    Config->PortConfigs[0]->DeviceConfigs[0]->MaxRegHole = 10;
    Config->PortConfigs[0]->DeviceConfigs[0]->MaxBitHole = 80;
    InvalidateConfigPoll(TEST_HOLES);

    auto device = TSerialDeviceFactory::GetDevice("1", "modbus", SerialPort);

    ASSERT_EQ(device->DeviceConfig()->MaxRegHole, 10);
    ASSERT_EQ(device->DeviceConfig()->MaxBitHole, 80);

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

    Note() << "LoopOnce()";
    Observer->LoopOnce();

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

    Note() << "LoopOnce()";
    Observer->LoopOnce();
}

TEST_F(TModbusIntegrationTest, MaxReadRegisters)
{
    // Normally registers 4-9 (6 in total) are read or written in a single request.
    // By limiting the max_read_registers to 3 we force driver to issue two requests
    //    for this register range instead of one

    Config->PortConfigs[0]->DeviceConfigs[0]->MaxReadRegisters = 3;
    InvalidateConfigPoll(TEST_MAX_READ_REGISTERS);
}

TEST_F(TModbusIntegrationTest, GuardInterval)
{
    Config->PortConfigs[0]->DeviceConfigs[0]->GuardInterval = chrono::microseconds(1000);
    InvalidateConfigPoll();
}


class TModbusBitmasksIntegrationTest: public TModbusIntegrationTest
{
protected:
    const char* ConfigPath() const override { return "configs/config-modbus-bitmasks-test.json"; }

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
    Observer->LoopOnce();
}

TEST_F(TModbusBitmasksIntegrationTest, SingleWrite)
{
    ExpectPollQueries();
    Note() << "LoopOnce()";
    Observer->LoopOnce();

    MQTTClient->Publish(nullptr, "/devices/modbus-sample/controls/U8:1/on", "1");

    EnqueueU8Shift1SingleBitHoldingWriteResponse();
    ExpectPollQueries(true);
    Note() << "LoopOnce()";
    Observer->LoopOnce();
}

// TEST_F(TModbusBitmasksIntegrationTest, MultipleWrite)
// {
//     ExpectPollQueries();
//     Note() << "LoopOnce()";
//     Observer->LoopOnce();

//     MQTTClient->Publish(nullptr, "/devices/modbus-sample/controls/U16:8/on", "5555");

//     EnqueueU16Shift8HoldingWriteResponse();
//     ExpectPollQueries(false, true);
//     Note() << "LoopOnce()";
//     Observer->LoopOnce();
// }
