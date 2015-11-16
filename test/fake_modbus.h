#pragma once
#include <memory>
#include <gtest/gtest.h>

#include "testlog.h"
#include "../wb-homa-modbus/modbus_client.h"
#include <modbus/modbus.h> // for constants such as MODBUS_MAX_READ_BITS, etc.

struct TRegisterRange {
    TRegisterRange(int start = 0, int end = 0)
        : Start(start), End(end)
    {
        assert(End >= Start);
        assert(End - Start <= 65536);
    }

    int ValidateIndex(const std::string& name, int index) const
    {
        if (index < Start || index > End) {
            throw TModbusException(name + " index is out of range: " +
                                   std::to_string(index) +
                                   " (must be " + std::to_string(Start) +
                                   " <= addr < " + std::to_string(End) + ")");
            return Start;
        }
        return index;
    }

    int Size() const
    {
        return End - Start;
    }

    int Start, End;
};

template<typename T>
class TRegisterSet {
public:
    TRegisterSet(const std::string& name, const TRegisterRange& range)
        : Name(name), Range(range)
    {
        // Allocate zero-filled array of values.
        // It always should contain at least one value
        // so that operator[] can always return some
        // reference, even in the case of erroneous access.
        values = new T[std::max(1, Range.Size())]();
    }

    ~TRegisterSet() {
        delete[] values;
    }

    T& operator[] (int index) {
        return values[Range.ValidateIndex(Name, index) - Range.Start];
    }

    const T& operator[] (int index) const {
        return values[Range.ValidateIndex(Name, index) - Range.Start];
    }

    void ReadRegs(TLoggedFixture& fixture, int addr, int nb, T* dest, RegisterFormat fmt) {
        ASSERT_GT(nb, 0);
        std::stringstream s;
        std::string fmtStr = fmt == AUTO ? "" : std::string(", ") + RegisterFormatName(fmt);
        s << "read " << nb << " " << Name << "(s)" << fmtStr << " @ " << addr;
        if (nb) {
            s << ":";
            while (nb--) {
                int v = (*this)[addr++];
                s << " 0x" << std::hex << std::setw(StrWidth()) << std::setfill('0') << (int)v;
                *dest++ = v;
            }
        }
        fixture.Emit() << s.str();
    }

    void WriteRegs(TLoggedFixture& fixture, int addr, int nb, const T* src, RegisterFormat fmt) {
        ASSERT_GT(nb, 0);
        std::stringstream s;
        std::string fmtStr = fmt == AUTO ? "" : std::string(", ") + RegisterFormatName(fmt);
        s << "write " << nb << " " << Name << "(s)" << fmtStr << " @ " << addr;
        if (nb) {
            s << ": ";
            while (nb--) {
                int v = *src++;
                s << " 0x" << std::hex << std::setw(StrWidth()) << std::setfill('0') << (int)v;
                (*this)[addr++] = v;
            }
        }
        fixture.Emit() << s.str();
    }
private:
    int StrWidth() const { return sizeof(T) * 2; }
    std::string Name;
    TRegisterRange Range;
    T* values;
};

struct TFakeSlave
{
    TFakeSlave(const TRegisterRange& coil_range = TRegisterRange(),
               const TRegisterRange& discrete_range = TRegisterRange(),
               const TRegisterRange& holding_range = TRegisterRange(),
               const TRegisterRange& input_range = TRegisterRange(),
               const TRegisterRange& direct_range = TRegisterRange())
        : Coils("coil", coil_range),
          Discrete("discrete input", discrete_range),
          Holding("holding register", holding_range),
          Input("input register", input_range),
          Direct("direct register", direct_range) {}
    TRegisterSet<uint8_t> Coils;
    TRegisterSet<uint8_t> Discrete;
    TRegisterSet<uint16_t> Holding;
    TRegisterSet<uint16_t> Input;
    TRegisterSet<uint64_t> Direct;
};

typedef std::shared_ptr<TFakeSlave> PFakeSlave;

class TFakeModbusContext: public TModbusContext
{
public:
    void Connect();
    void Disconnect();
    void AddDevice(int slave, const std::string& protocol);
    void SetDebug(bool debug);
    void SetSlave(int slave_addr);
    void ReadCoils(int addr, int nb, uint8_t *dest);
    void WriteCoil(int addr, int value);
    void ReadDisceteInputs(int addr, int nb, uint8_t *dest);
    void ReadHoldingRegisters(int addr, int nb, uint16_t *dest);
    void WriteHoldingRegisters(int addr, int nb, const uint16_t *data);
    void WriteHoldingRegister(int addr, uint16_t value);
    void ReadInputRegisters(int addr, int nb, uint16_t *dest);
    void ReadDirectRegister(int addr, uint64_t* dest, RegisterFormat format, size_t width);
    void WriteDirectRegister(int addr, uint64_t value, RegisterFormat format);
    void EndPollCycle(int usecDelay);

    void ExpectDebug(bool debug)
    {
        ASSERT_EQ(Debug, debug);
    }

    PFakeSlave GetSlave(int slave_addr);
    PFakeSlave AddSlave(int slave_addr, PFakeSlave slave);

private:
    friend class TFakeModbusConnector;
    TFakeModbusContext(TLoggedFixture& fixture): Fixture(fixture) {}

    TLoggedFixture& Fixture;
    bool Connected = false;
    bool Debug = false;
    std::map<int, PFakeSlave> Slaves;
    PFakeSlave CurrentSlave;
};

typedef std::shared_ptr<TFakeModbusContext> PFakeModbusContext;

class TFakeModbusConnector: public TModbusConnector
{
public:
    TFakeModbusConnector(TLoggedFixture& fixture)
        : Fixture(fixture) {}
    PModbusContext CreateContext(const TSerialPortSettings& settings);
    PFakeModbusContext GetContext(const std::string& device);
    PFakeSlave GetSlave(const std::string& device, int slave_addr);
    PFakeSlave AddSlave(const std::string& device, int slave_addr, PFakeSlave slave);
    PFakeSlave AddSlave(const std::string& device, int slave_addr,
                        const TRegisterRange& coil_range = TRegisterRange(),
                        const TRegisterRange& discrete_range = TRegisterRange(),
                        const TRegisterRange& holding_range = TRegisterRange(),
                        const TRegisterRange& input_range = TRegisterRange());

    static const char* PORT0;
    static const char* PORT1;
private:
    TLoggedFixture& Fixture;
    std::map<std::string, PFakeModbusContext> Contexts;
};

typedef std::shared_ptr<TFakeModbusConnector> PFakeModbusConnector;
