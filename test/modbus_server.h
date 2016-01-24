#pragma once
#include <map>
#include <mutex>
#include <string>
#include <memory>
#include <thread>
#include <cassert>
#include <stdexcept>
#include <unordered_set>
#include <modbus/modbus.h>
#include "testlog.h"
#include "portsettings.h"
#include "serial_protocol.h"

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
            throw TSerialProtocolException(
                name + " index is out of range: " +
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

struct TModbusRange {
    TModbusRange(const TRegisterRange& coil_range = TRegisterRange(),
                 const TRegisterRange& discrete_range = TRegisterRange(),
                 const TRegisterRange& holding_range = TRegisterRange(),
                 const TRegisterRange& input_range = TRegisterRange())
        : Coils(coil_range),
          Discrete(discrete_range),
          Holding(holding_range),
          Input(input_range) {}
    TRegisterRange Coils, Discrete, Holding, Input;
};

template<typename T>
class TRegisterSet {
public:
    TRegisterSet(const std::string& name, const TRegisterRange& range, T* values)
        : Name(name), Range(range)
    {
        Values = values;
    }

    T& operator[] (int index) {
        // XXX: later: update for newer modbus_mapping_offset_new() when the new API becomes stable:
        // return Values[Range.ValidateIndex(Name, index) - Range.Start];
        return Values[Range.ValidateIndex(Name, index)];
    }

    const T& operator[] (int index) const {
        // XXX: later: update for newer modbus_mapping_offset_new() when the new API becomes stable:
        // return Values[Range.ValidateIndex(Name, index) - Range.Start];
        return Values[Range.ValidateIndex(Name, index)];
    }

    // FIXME: blacklisting is only supported for holding registers currently
    void BlacklistRead(int addr, bool blacklist)
    {
        if (blacklist)
            ReadBlacklist.insert(addr);
        else
            ReadBlacklist.erase(addr);
    }

    void BlacklistWrite(int addr, bool blacklist)
    {
        if (blacklist)
            WriteBlacklist.insert(addr);
        else
            WriteBlacklist.erase(addr);
    }

    bool IsReadBlacklisted(int addr) const
    {
        return ReadBlacklist.find(addr) != ReadBlacklist.end();
    }

    bool IsWriteBlacklisted(int addr) const
    {
        return WriteBlacklist.find(addr) != WriteBlacklist.end();
    }
private:
    std::unordered_set<int> ReadBlacklist, WriteBlacklist;
    int StrWidth() const { return sizeof(T) * 2; }
    std::string Name;
    TRegisterRange Range;
    T* Values;
};

struct TModbusSlave
{
    TModbusSlave(int address, const TModbusRange& range)
        : Address(address),
          Mapping(modbus_mapping_new(range.Coils.End, range.Discrete.End, range.Holding.End, range.Input.End)),
          // XXX: later: update for newer API when it becomes stable:
          // Mapping(modbus_mapping_offset_new(
          //     range.Coils.Size(), range.Coils.Start,
          //     range.Discrete.Size(), range.Discrete.Start,
          //     range.Holding.Size(), range.Holding.Start,
          //     range.Input.Size(), range.Input.Start))
          Coils("coil", range.Coils, Mapping->tab_bits),
          Discrete("discrete input", range.Discrete, Mapping->tab_input_bits),
          Holding("holding register", range.Holding, Mapping->tab_registers),
          Input("input register", range.Input, Mapping->tab_input_registers) {}
    ~TModbusSlave() { modbus_mapping_free(Mapping); }
    int Address;
    modbus_mapping_t* Mapping;
    TRegisterSet<uint8_t> Coils;
    TRegisterSet<uint8_t> Discrete;
    TRegisterSet<uint16_t> Holding;
    TRegisterSet<uint16_t> Input;
};

typedef std::shared_ptr<TModbusSlave> PModbusSlave;

class TModbusServer {
public:
    TModbusServer(PSerialPort port);
    ~TModbusServer();
    void Start();
    PModbusSlave GetSlave() const;
    PModbusSlave SetSlave(PModbusSlave slave);
    PModbusSlave SetSlave(int slave_addr, const TModbusRange& range);

private:
    void Run();
    bool IsRequestBlacklisted() const;

    bool Stop;
    std::mutex Mutex;
    std::thread ServerThread;
    PSerialPort Port;
    PModbusSlave Slave;
    uint8_t* Query;
};

typedef std::shared_ptr<TModbusServer> PModbusServer;
