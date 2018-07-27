#include "fake_serial_port.h"
#include "modbus_expectations.h"
#include "modbus_device.h"
#include "modbus_common.h"
#include "ir_device_query_factory.h"
#include "ir_device_query.h"

using namespace std;


class TModbusTest: public TSerialDeviceTest, public TModbusExpectations
{
    typedef shared_ptr<TModbusDevice> PModbusDevice;
protected:
    void SetUp();
    set<int> VerifyQuery(vector<PVirtualRegister> registerList = {});
    PIRDeviceQuery GenerateReadQueryForVirtualRegister(const PVirtualRegister & vreg);

    virtual PDeviceConfig GetDeviceConfig();

    PModbusDevice ModbusDev;

    PVirtualRegister ModbusCoil0;
    PVirtualRegister ModbusCoil1;
    PVirtualRegister ModbusDiscrete;
    PVirtualRegister ModbusHolding;
    PVirtualRegister ModbusInput;
    PVirtualRegister ModbusHoldingS64;
    PVirtualRegister ModbusHoldingU64Single;
    PVirtualRegister ModbusHoldingU16Single;
    PVirtualRegister ModbusHoldingU64Multi;
    PVirtualRegister ModbusHoldingU16Multi;
};

PDeviceConfig TModbusTest::GetDeviceConfig()
{
    auto config = make_shared<TDeviceConfig>("modbus", to_string(0x01), "modbus");

    config->MaxReadRegisters = 4;

    return config;
}

void TModbusTest::SetUp()
{
    SelectModbusType(MODBUS_RTU);
    TSerialDeviceTest::SetUp();

    ModbusDev = make_shared<TModbusDevice>(GetDeviceConfig(), SerialPort,
                                TSerialDeviceFactory::GetProtocol("modbus"));

    ModbusCoil0 = Reg(ModbusDev, 0, Modbus::REG_COIL, U8);
    ModbusCoil1 = Reg(ModbusDev, 1, Modbus::REG_COIL, U8);
    ModbusDiscrete = Reg(ModbusDev, 20, Modbus::REG_DISCRETE, U8);
    ModbusHolding = Reg(ModbusDev, 70, Modbus::REG_HOLDING, U16);
    ModbusInput = Reg(ModbusDev, 40, Modbus::REG_INPUT, U16);
    ModbusHoldingS64 = Reg(ModbusDev, 30, Modbus::REG_HOLDING, S64);

    ModbusHoldingU64Single = Reg(ModbusDev, 90, Modbus::REG_HOLDING_SINGLE, U64);
    ModbusHoldingU16Single = Reg(ModbusDev, 94, Modbus::REG_HOLDING_SINGLE, U16);
    ModbusHoldingU64Multi = Reg(ModbusDev, 95, Modbus::REG_HOLDING_MULTI, U64);
    ModbusHoldingU16Multi = Reg(ModbusDev, 99, Modbus::REG_HOLDING_MULTI, U16);

    SerialPort->Open();
}

set<int> TModbusTest::VerifyQuery(vector<PVirtualRegister> memoryBlockSet)
{
    size_t expectedQuerySetCount = 0;
    size_t expectedQueryCount = 0;

    if (memoryBlockSet.empty()) {
        memoryBlockSet = {
            ModbusCoil0, ModbusCoil1, ModbusDiscrete, ModbusHolding, ModbusInput, ModbusHoldingS64
        };

        EnqueueCoilReadResponse();
        EnqueueDiscreteReadResponse();
        EnqueueHoldingReadU16Response();
        EnqueueInputReadU16Response();
        EnqueueHoldingReadS64Response();

        expectedQuerySetCount = 1;
        expectedQueryCount = 5;
    }

    auto querySetsByPollInterval = TIRDeviceQueryFactory::GenerateQuerySets(memoryBlockSet, EQueryOperation::Read);

    if (expectedQuerySetCount > 0) {
        if (querySetsByPollInterval.size() != expectedQuerySetCount) {
            throw runtime_error("wrong query sets count: " + to_string(querySetsByPollInterval.size()) + " expected: " + to_string(expectedQuerySetCount));
        } else if (expectedQueryCount > 0) {
            size_t actualQueryCount = 0;

            for (const auto & pollIntervalQuerySet: querySetsByPollInterval) {
                actualQueryCount += pollIntervalQuerySet.second->Queries.size();
            }

            if (actualQueryCount != expectedQueryCount) {
                throw runtime_error("wrong query count: " + to_string(actualQueryCount) + " expected: " + to_string(expectedQueryCount));
            }
        }
    }

    set<int> readAddresses;
    set<int> errorRegisters;
    map<int, string> registerValues;

    for (const auto & pollIntervalQuerySet: querySetsByPollInterval) {
        const auto & querySet = pollIntervalQuerySet.second;

        for (const auto & query: querySet->Queries) {
            ModbusDev->Execute(query);

            for (const auto & val: query->VirtualValues) {
                const auto & vreg = dynamic_pointer_cast<TVirtualRegister>(val);

                if (query->GetStatus() == EQueryStatus::Ok) {
                    registerValues[vreg->Address] = vreg->GetTextValue();
                } else {
                    errorRegisters.insert(vreg->Address);
                }

                readAddresses.insert(vreg->Address);
            }
        }
    }

    EXPECT_EQ(to_string(memoryBlockSet.size()), to_string(readAddresses.size()));

    for (auto registerValue: registerValues) {
        auto address = registerValue.first;
        auto value = registerValue.second;

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

PIRDeviceQuery TModbusTest::GenerateReadQueryForVirtualRegister(const PVirtualRegister & vreg)
{
    auto querySetsByPollInterval = TIRDeviceQueryFactory::GenerateQuerySets({ vreg }, EQueryOperation::Read);

    EXPECT_EQ(1, querySetsByPollInterval.size());
    EXPECT_EQ(1, querySetsByPollInterval.begin()->second->Queries.size());

    return *querySetsByPollInterval.begin()->second->Queries.begin();
}

TEST_F(TModbusTest, Query)
{
    ASSERT_EQ(0, VerifyQuery().size()); // we don't expect any errors to occur here
    SerialPort->Close();
}

TEST_F(TModbusTest, HoldingSingleMulti)
{
    EnqueueHoldingSingleWriteU16Response();
    EnqueueHoldingSingleWriteU64Response();
    EnqueueHoldingMultiWriteU16Response();
    EnqueueHoldingMultiWriteU64Response();

    ModbusHoldingU16Single->SetTextValue(to_string(0x0f41));
    ModbusHoldingU16Single->Flush();

    ModbusHoldingU64Single->SetTextValue(to_string(0x01020304050607));
    ModbusHoldingU64Single->Flush();

    ModbusHoldingU16Multi->SetTextValue(to_string(0x0123));
    ModbusHoldingU16Multi->Flush();

    ModbusHoldingU64Multi->SetTextValue(to_string(0x0123456789ABCDEF));
    ModbusHoldingU64Multi->Flush();

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
    auto errorAddresses = VerifyQuery({
        ModbusCoil0, ModbusCoil1, ModbusDiscrete, ModbusHolding, ModbusInput, ModbusHoldingS64
    });

    ASSERT_EQ(expectedAddresses, errorAddresses);
    SerialPort->Close();
}

#define READ_COIL_0_EMIT_TRANSIENT_ERROR \
    try {                                                               \
        auto query = GenerateReadQueryForVirtualRegister(ModbusCoil0);  \
        ModbusDev->Read(*query);                                        \
        EXPECT_FALSE(true);                                             \
    } catch (const TSerialDeviceTransientErrorException & e) {          \
        Emit() << e.what();                                             \
    }

#define WRITE_COIL_0_EMIT_TRANSIENT_ERROR \
    try {                                                       \
        ModbusCoil0->SetTextValue(to_string(0xFF));             \
        auto query = ModbusCoil0->GetWriteQuery();              \
        query->ResetStatus();                                   \
        ModbusDev->Write(*query);                               \
        EXPECT_FALSE(true);                                     \
    } catch (const TSerialDeviceTransientErrorException & e) {  \
        Emit() << e.what();                                     \
    }

TEST_F(TModbusTest, CRCError)
{
    EnqueueInvalidCRCCoilReadResponse();

    READ_COIL_0_EMIT_TRANSIENT_ERROR

    SerialPort->Close();
}

TEST_F(TModbusTest, WrongResponseDataSize)
{
    EnqueueWrongDataSizeReadResponse();

    READ_COIL_0_EMIT_TRANSIENT_ERROR

    SerialPort->Close();
}

TEST_F(TModbusTest, WrongSlaveId)
{
    EnqueueWrongSlaveIdCoilReadResponse();

    READ_COIL_0_EMIT_TRANSIENT_ERROR

    SerialPort->Close();
}

TEST_F(TModbusTest, WrongFunctionCode)
{
    EnqueueWrongFunctionCodeCoilReadResponse();

    READ_COIL_0_EMIT_TRANSIENT_ERROR

    SerialPort->Close();
}

TEST_F(TModbusTest, WrongFunctionCodeWithException)
{
    EnqueueWrongFunctionCodeCoilReadResponse(0x2);

    READ_COIL_0_EMIT_TRANSIENT_ERROR

    SerialPort->Close();
}

TEST_F(TModbusTest, WrongSlaveIdWrite)
{
    EnqueueWrongSlaveIdCoilWriteResponse();

    WRITE_COIL_0_EMIT_TRANSIENT_ERROR

    SerialPort->Close();
}

TEST_F(TModbusTest, WrongFunctionCodeWrite)
{
    EnqueueWrongFunctionCodeCoilWriteResponse();

    WRITE_COIL_0_EMIT_TRANSIENT_ERROR

    SerialPort->Close();
}

TEST_F(TModbusTest, WrongFunctionCodeWithExceptionWrite)
{
    EnqueueWrongFunctionCodeCoilWriteResponse(0x2);

    WRITE_COIL_0_EMIT_TRANSIENT_ERROR

    SerialPort->Close();
}

#undef READ_COIL_0_EMIT_TRANSIENT_ERROR
#undef WRITE_COIL_0_EMIT_TRANSIENT_ERROR


class TModbusIntegrationTest: public TSerialDeviceIntegrationTest, public TModbusExpectations
{
protected:
    enum TestMode {TEST_DEFAULT, TEST_HOLES, TEST_MAX_READ_REGISTERS};

    void SetUp();
    void TearDown();
    const char* ConfigPath() const { return "configs/config-modbus-test.json"; }
    void ExpectPollQueries(TestMode mode = TEST_DEFAULT);
    void InvalidateConfig();
    void InvalidateConfigPoll(TestMode mode = TEST_DEFAULT);
    void ChooseConfig(const char * path);
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
        Enqueue82CoilsReadResponse();
        break;
    case TEST_MAX_READ_REGISTERS:
    case TEST_DEFAULT:
    default:
        EnqueueCoilReadResponse();
        break;
    }

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
    EnqueueDiscreteReadResponse();
    EnqueueHoldingReadS64Response();
    EnqueueInputReadU16Response();
    EnqueueHoldingReadF32Response();
    EnqueueHoldingReadU16Response();

    switch (mode) {
    case TEST_HOLES:
        break;
    case TEST_MAX_READ_REGISTERS:
        Enqueue10CoilsMax3ReadResponse();
    case TEST_DEFAULT:
    default:
        Enqueue10CoilsReadResponse();
        break;
    }

    if (mode == TEST_MAX_READ_REGISTERS) {
        EnqueueHoldingSingleMax3ReadResponse();
        EnqueueHoldingMultiMax3ReadResponse();
    } else {
        EnqueueHoldingSingleReadResponse();
        EnqueueHoldingMultiReadResponse();
    }
}

void TModbusIntegrationTest::InvalidateConfig()
{
    TSerialDeviceFactory::RemoveDevice(TSerialDeviceFactory::GetDevice("1", "modbus", SerialPort));
    Observer = make_shared<TMQTTSerialObserver>(MQTTClient, Config, SerialPort);

    Observer->SetUp();
}

void TModbusIntegrationTest::InvalidateConfigPoll(TestMode mode)
{
    InvalidateConfig();

    ExpectPollQueries(mode);
    Note() << "LoopOnce()";
    Observer->LoopOnce();
}

void TModbusIntegrationTest::ChooseConfig(const char * path)
{
    PTemplateMap templateMap = std::make_shared<TTemplateMap>();
    if (GetTemplatePath()) {
        TConfigTemplateParser templateParser(GetDataFilePath(GetTemplatePath()), false);
        templateMap = templateParser.Parse();
    }
    TConfigParser parser(GetDataFilePath(path), false, templateMap);
    Config = parser.Parse();

    InvalidateConfig();
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

    EnqueueCoilReadResponse(0xa);

    EnqueueHoldingPackReadResponse(0x3);
    EnqueueDiscreteReadResponse(0xb);
    EnqueueHoldingReadS64Response(0x4);
    EnqueueInputReadU16Response(0x8);
    EnqueueHoldingReadF32Response(0x5);
    EnqueueHoldingReadU16Response(0x6);
    Enqueue10CoilsReadResponse(0x54);   // invalid exception code
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

    Enqueue82CoilsReadResponse();
    EnqueueHoldingPackHoles10ReadResponse(0x3); // this must result in auto-disabling holes feature
    EnqueueDiscreteReadResponse();
    EnqueueHoldingReadS64Response();
    EnqueueInputReadU16Response();
    EnqueueHoldingReadF32Response();
    EnqueueHoldingReadU16Response();
    EnqueueHoldingSingleReadResponse();
    EnqueueHoldingMultiReadResponse();

    Note() << "LoopOnce()";
    Observer->LoopOnce();

    Enqueue82CoilsReadResponse();
    EnqueueHoldingPackReadResponse();
    EnqueueDiscreteReadResponse();
    EnqueueHoldingReadS64Response();
    EnqueueInputReadU16Response();
    EnqueueHoldingReadF32Response();
    EnqueueHoldingReadU16Response();
    EnqueueHoldingSingleReadResponse();
    EnqueueHoldingMultiReadResponse();

    Note() << "LoopOnce()";
    Observer->LoopOnce();
}

TEST_F(TModbusIntegrationTest, RegisterAutoDisable)
{
    // we check that driver issue long read request, reading registers 4-18 at once
    Config->PortConfigs[0]->DeviceConfigs[0]->MaxRegHole = 10;
    Config->PortConfigs[0]->DeviceConfigs[0]->MaxBitHole = 80;
    InvalidateConfigPoll(TEST_HOLES);

    auto device = TSerialDeviceFactory::GetDevice("1", "modbus", SerialPort);

    ASSERT_EQ(device->DeviceConfig()->MaxRegHole, 10);
    ASSERT_EQ(device->DeviceConfig()->MaxBitHole, 80);

    Enqueue82CoilsReadResponse();
    EnqueueHoldingPackDisableReadResponse(1); // this must result in auto-disabling holes feature
    EnqueueDiscreteReadResponse();
    EnqueueHoldingReadS64Response();
    EnqueueInputReadU16Response();
    EnqueueHoldingReadF32Response();
    EnqueueHoldingReadU16Response();
    EnqueueHoldingSingleReadResponse();
    EnqueueHoldingMultiReadResponse();

    Note() << "LoopOnce()";
    Observer->LoopOnce();

    Enqueue82CoilsReadResponse();
    EnqueueHoldingPackDisableReadResponse(2); // this must result in splitting ranges to individual registers and disabling single register ranges
    EnqueueDiscreteReadResponse();
    EnqueueHoldingReadS64Response();
    EnqueueInputReadU16Response();
    EnqueueHoldingReadF32Response();
    EnqueueHoldingReadU16Response();
    EnqueueHoldingSingleReadResponse();
    EnqueueHoldingMultiReadResponse();

    Note() << "LoopOnce()";
    Observer->LoopOnce();

    Enqueue82CoilsReadResponse();
    EnqueueHoldingPackDisableReadResponse(3); // this must result in disabling registers
    EnqueueDiscreteReadResponse();
    EnqueueHoldingReadS64Response();
    EnqueueInputReadU16Response();
    EnqueueHoldingReadF32Response();
    EnqueueHoldingReadU16Response();
    EnqueueHoldingSingleReadResponse();
    EnqueueHoldingMultiReadResponse();

    Note() << "LoopOnce()";
    Observer->LoopOnce();

    // no holding pack registers here
    Enqueue82CoilsReadResponse();
    EnqueueDiscreteReadResponse();
    EnqueueHoldingReadS64Response();
    EnqueueInputReadU16Response();
    EnqueueHoldingReadF32Response();
    EnqueueHoldingReadU16Response();
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

    ChooseConfig("configs/config-modbus-max-read-regs-test.json");

    EnqueueCoilReadResponse();

    EnqueueHoldingPackMax3ReadResponse();
    EnqueueDiscreteReadResponse();
    EnqueueInputReadU16Response();

    // test different lengths and register types
    EnqueueHoldingReadF32Response();
    EnqueueHoldingReadU16Response();

    Enqueue10CoilsMax3ReadResponse();

    Note() << "LoopOnce()";
    Observer->LoopOnce();
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
    EnqueueU16Shift8Bits8U32Shift8Bits16HoldingReadResponse(afterWriteMultiple);
    EnqueueU8SingleBitsHoldingReadResponse(afterWriteSingle);
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

    MQTTClient->Publish(nullptr, "/devices/modbus-sample/controls/U8:1:1/on", "1");

    EnqueueU8Shift1SingleBitHoldingWriteResponse();
    ExpectPollQueries(true);
    Note() << "LoopOnce()";
    Observer->LoopOnce();
}

TEST_F(TModbusBitmasksIntegrationTest, MultipleWrite)
{
    ExpectPollQueries();
    Note() << "LoopOnce()";
    Observer->LoopOnce();

    MQTTClient->Publish(nullptr, "/devices/modbus-sample/controls/U32:8:16/on", "5555");

    EnqueueU32Shift8HoldingWriteResponse();
    ExpectPollQueries(false, true);
    Note() << "LoopOnce()";
    Observer->LoopOnce();
}
