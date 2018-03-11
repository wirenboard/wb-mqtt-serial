#include "fake_serial_port.h"
#include "modbus_expectations.h"
#include "modbus_device.h"
#include "modbus_common.h"
#include "ir_device_query_factory.h"
#include "ir_device_query.h"
#include "virtual_register.h"

using namespace std;


class TModbusTest: public TSerialDeviceTest, public TModbusExpectations
{
    typedef shared_ptr<TModbusDevice> PModbusDevice;
protected:
    void SetUp();
    set<int> VerifyQuery(TPSet<PVirtualRegister> registerList = TPSet<PVirtualRegister>());
    PIRDeviceQuery GenerateReadQueryForVirtualRegister(const PVirtualRegister & virtualRegister);

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
    return make_shared<TDeviceConfig>("modbus", to_string(0x01), "modbus");
}

void TModbusTest::SetUp()
{
    SelectModbusType(MODBUS_RTU);
    TSerialDeviceTest::SetUp();

    ModbusDev = make_shared<TModbusDevice>(GetDeviceConfig(), SerialPort,
                                TSerialDeviceFactory::GetProtocol("modbus"));

    ModbusCoil0 = TVirtualRegister::Create(TRegisterConfig::Create(Modbus::REG_COIL, 0, U8), ModbusDev);
    ModbusCoil1 = TVirtualRegister::Create(TRegisterConfig::Create(Modbus::REG_COIL, 1, U8), ModbusDev);
    ModbusDiscrete = TVirtualRegister::Create(TRegisterConfig::Create(Modbus::REG_DISCRETE, 20, U8), ModbusDev);
    ModbusHolding = TVirtualRegister::Create(TRegisterConfig::Create(Modbus::REG_HOLDING, 70, U16), ModbusDev);
    ModbusInput = TVirtualRegister::Create(TRegisterConfig::Create(Modbus::REG_INPUT, 40, U16), ModbusDev);
    ModbusHoldingS64 = TVirtualRegister::Create(TRegisterConfig::Create(Modbus::REG_HOLDING, 30, S64), ModbusDev);

    ModbusHoldingU64Single = TVirtualRegister::Create(TRegisterConfig::Create(Modbus::REG_HOLDING_SINGLE, 90, U64), ModbusDev);
    ModbusHoldingU16Single = TVirtualRegister::Create(TRegisterConfig::Create(Modbus::REG_HOLDING_SINGLE, 94, U16), ModbusDev);
    ModbusHoldingU64Multi = TVirtualRegister::Create(TRegisterConfig::Create(Modbus::REG_HOLDING_MULTI, 95, U64), ModbusDev);
    ModbusHoldingU16Multi = TVirtualRegister::Create(TRegisterConfig::Create(Modbus::REG_HOLDING_MULTI, 99, U16), ModbusDev);

    SerialPort->Open();
}

set<int> TModbusTest::VerifyQuery(TPSet<PVirtualRegister> registerSet)
{
    size_t expectedQuerySetCount = 0;

    if (registerSet.empty()) {
        registerSet = {
            ModbusCoil0, ModbusCoil1, ModbusDiscrete, ModbusHolding, ModbusInput, ModbusHoldingS64
        };

        expectedQuerySetCount = 4;
    }

    auto querySetsByPollInterval = TIRDeviceQueryFactory::GenerateQuerySets(registerSet, EQueryOperation::Read);

    if (expectedQuerySetCount > 0) {
        if (querySetsByPollInterval.size() != expectedQuerySetCount) {
            throw runtime_error("wrong query sets count: " + to_string(querySetsByPollInterval.size()) + " expected: " + to_string(expectedQuerySetCount));
        }
    }

    set<int> readAddresses;
    set<int> errorRegisters;
    map<int, uint64_t> registerValues;

    for (const auto & pollIntervalQuerySets: querySetsByPollInterval) {
        const auto & querySets = pollIntervalQuerySets.second;

        for (const auto & querySet: querySets) {
            for (const auto & query: querySet->Queries) {
                ModbusDev->Execute(query);

                for (const auto & virtualRegister: query->VirtualRegisters) {
                    if (query->GetStatus() == EQueryStatus::Ok) {
                        registerValues[virtualRegister->Address] = virtualRegister->GetValue();
                    } else {
                        errorRegisters.insert(virtualRegister->Address);
                    }

                    readAddresses.insert(virtualRegister->Address);
                }
            }
        }
    }

    EXPECT_EQ(to_string(registerSet.size()), to_string(readAddresses.size()));

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

PIRDeviceQuery TModbusTest::GenerateReadQueryForVirtualRegister(const PVirtualRegister & virtualRegister)
{
    auto querySetsByPollInterval = TIRDeviceQueryFactory::GenerateQuerySets({ virtualRegister }, EQueryOperation::Read);

    EXPECT_EQ(1, querySetsByPollInterval.size());
    EXPECT_EQ(1, querySetsByPollInterval.begin()->second.size());
    EXPECT_EQ(1, querySetsByPollInterval.begin()->second.front()->Queries.size());

    return *querySetsByPollInterval.begin()->second.front()->Queries.begin();
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

    ModbusHoldingU16Single->SetValue(0x0f41);
    ModbusHoldingU16Single->Flush();

    ModbusHoldingU64Single->SetValue(0x01020304050607);
    ModbusHoldingU64Single->Flush();

    ModbusHoldingU16Multi->SetValue(0x0123);
    ModbusHoldingU16Multi->Flush();

    ModbusHoldingU64Multi->SetValue(0x0123456789ABCDEF);
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
    auto errorAddresses = VerifyQuery();

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
        ModbusCoil0->SetValue(0xFF);                            \
        ModbusCoil0->Flush();                                   \
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
