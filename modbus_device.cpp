#include <iostream>
#include <stdexcept>

#include "modbus_device.h"

namespace {
    const int MAX_REGS_HOLE = 10;
    const int MAX_BITS_HOLE = 80;

    static bool IsSingleBitType(int type) {
        return type == TModbusDevice::REG_COIL || type == TModbusDevice::REG_DISCRETE;
    }

    class TModbusRegisterRange: public TRegisterRange {
    public:
        TModbusRegisterRange(const std::list<PRegister>& regs);
        ~TModbusRegisterRange();
        void MapRange(TValueCallback value_callback, TErrorCallback error_callback);
        int GetStart() const { return Start; }
        int GetCount() const { return Count; }
        uint8_t* GetBits();
        uint16_t* GetWords();
        void SetError(bool error) { Error = error; }
        bool GetError() const { return Error; }

    private:
        bool Error = false;
        int Start, Count;
        uint8_t* Bits = 0;
        uint16_t* Words = 0;
    };
};

TModbusRegisterRange::TModbusRegisterRange(const std::list<PRegister>& regs):
    TRegisterRange(regs)
{
    if (regs.empty()) // shouldn't happen
        throw std::runtime_error("cannot construct empty register range");

    auto it = regs.begin();
    Start = (*it)->Address;
    int end = Start + (*it)->Width();
    while (++it != regs.end()) {
        if ((*it)->Type != Type())
            throw std::runtime_error("registers of different type in the same range");
        int new_end = (*it)->Address + (*it)->Width();
        if (new_end > end)
            end = new_end;
    }
    Count = end - Start;
    if (Count > (IsSingleBitType(Type()) ? MODBUS_MAX_READ_BITS : MODBUS_MAX_READ_REGISTERS))
        throw std::runtime_error("Modbus register range too large");
}

void TModbusRegisterRange::MapRange(TValueCallback value_callback, TErrorCallback error_callback)
{
    if (Error) {
        for (auto reg: RegisterList())
            error_callback(reg);
        return;
    }
    if (IsSingleBitType(Type())) {
        if (!Bits)
            throw std::runtime_error("bits not loaded");
        for (auto reg: RegisterList()) {
            int w = reg->Width();
            if (w != 1)
                throw TSerialDeviceException(
                    "width other than 1 is not currently supported for reg type" +
                    reg->TypeName);
            if (reg->Address - Start >= Count)
                throw std::runtime_error("address out of range");
            value_callback(reg, Bits[reg->Address - Start]);
        }
        return;
    }

    if (!Words)
        throw std::runtime_error("words not loaded");
    for (auto reg: RegisterList()) {
        int w = reg->Width();
        if (reg->Address - Start + w > Count)
            throw std::runtime_error("address out of range");
        uint64_t r = 0;
        uint16_t* data = Words + (reg->Address - Start);
        while (w--)
            r = (r << 16) + *data++;
        value_callback(reg, r);
    }
}

TModbusRegisterRange::~TModbusRegisterRange() {
    if (Bits)
        delete[] Bits;
    if (Words)
        delete[] Words;
}

uint8_t* TModbusRegisterRange::GetBits() {
    if (!IsSingleBitType(Type()))
        throw std::runtime_error("GetBits() for non-bit register");
    if (!Bits)
        Bits = new uint8_t[Count];
    return Bits;
}

uint16_t* TModbusRegisterRange::GetWords() {
    if (IsSingleBitType(Type()))
        throw std::runtime_error("GetWords() for non-word register");
    if (!Words)
        Words = new uint16_t[Count];
    return Words;
}

REGISTER_PROTOCOL("modbus", TModbusDevice, TRegisterTypes({
            { TModbusDevice::REG_COIL, "coil", "switch", U8 },
            { TModbusDevice::REG_DISCRETE, "discrete", "switch", U8, true },
            { TModbusDevice::REG_HOLDING, "holding", "text", U16 },
            { TModbusDevice::REG_INPUT, "input", "text", U16, true }
        }));

TModbusDevice::TModbusDevice(PDeviceConfig config, PAbstractSerialPort port)
    : TSerialDevice(config, port), Context(port->LibModbusContext()) {}

std::list<PRegisterRange> TModbusDevice::SplitRegisterList(const std::list<PRegister> reg_list) const
{
    std::list<PRegisterRange> r;
    if (reg_list.empty())
        return r;

    std::list<PRegister> l;
    int prev_start = -1, prev_type = -1, prev_end = -1;
    std::chrono::milliseconds prev_interval;
    int max_hole = IsSingleBitType(reg_list.front()->Type) ? MAX_BITS_HOLE : MAX_REGS_HOLE,
        max_regs = IsSingleBitType(reg_list.front()->Type) ? MODBUS_MAX_READ_BITS : MODBUS_MAX_READ_REGISTERS;
    for (auto reg: reg_list) {
        int new_end = reg->Address + reg->Width();
        if (!(prev_end >= 0 &&
              reg->Type == prev_type &&
              reg->Address >= prev_end &&
              reg->Address <= prev_end + max_hole &&
              reg->PollInterval == prev_interval &&
              new_end - prev_start <= max_regs)) {
            if (!l.empty()) {
                auto range = std::make_shared<TModbusRegisterRange>(l);
                if (Port()->Debug())
                    std::cerr << "Adding range: " << range->GetCount() << " " <<
                        range->TypeName() << "(s) @ " << range->GetStart() <<
                        " of slave " << range->Slave() << std::endl;
                r.push_back(range);
                l.clear();
            }
            prev_start = reg->Address;
            prev_type = reg->Type;
            prev_interval = reg->PollInterval;
        }
        l.push_back(reg);
        prev_end = new_end;
    }
    if (!l.empty()) {
        auto range = std::make_shared<TModbusRegisterRange>(l);
        if (Port()->Debug())
            std::cerr << "Adding range: " << range->GetCount() << " " <<
                range->TypeName() << "(s) @ " << range->GetStart() <<
                " of slave " << range->Slave() << std::endl;
        r.push_back(range);
    }
    return r;
}

uint64_t TModbusDevice::ReadRegister(PRegister reg)
{
    int w = reg->Width();

    if (Port()->Debug())
        std::cerr << "modbus: read " << w << " " << reg->TypeName << "(s) @ " << reg->Address <<
            " of slave " << reg->Slave << std::endl;

    modbus_set_slave(Context->Inner, reg->Slave->Id);
    if (IsSingleBitType(reg->Type)) {
        uint8_t b;
        if (w != 1)
            throw TSerialDeviceException(
                "width other than 1 is not currently supported for reg type" +
                reg->TypeName);

        int rc = reg->Type == REG_COIL ?
            modbus_read_bits(Context->Inner, reg->Address, 1, &b) :
            modbus_read_input_bits(Context->Inner, reg->Address, 1, &b);

        if (rc > 0)
            return b;
    } else {
        if (w > 4)
            throw TSerialDeviceException(
                "can't pack more than 4 " + reg->TypeName +
                "s into a single value");

        uint16_t v[4];
        if (reg->Type != REG_HOLDING && reg->Type != REG_INPUT)
            throw TSerialDeviceException("invalid register type");

        int rc = reg->Type == REG_HOLDING ?
            modbus_read_registers(Context->Inner, reg->Address, w, v) :
            modbus_read_input_registers(Context->Inner, reg->Address, w, v);

        if (rc > 0) {
            uint64_t r = 0;
            for (size_t i = 0; i < size_t(w); ++i)
                r = (r << 16) + v[i];
            return r;
        }
    }

    throw TSerialDeviceTransientErrorException(
        "failed to read " + reg->TypeName +
        " @ " + std::to_string(reg->Address));
}

void TModbusDevice::WriteRegister(PRegister reg, uint64_t value)
{
    int w = reg->Width();

    if (Port()->Debug())
        std::cerr << "modbus: write " << w << " " << reg->TypeName << "(s) @ " << reg->Address <<
            " of slave " << reg->Slave << std::endl;

    modbus_set_slave(Context->Inner, reg->Slave->Id);
    switch (reg->Type) {
    case REG_COIL:
        if (w != 1)
            throw TSerialDeviceException(
                "width other than 1 is not currently supported for reg type" +
                reg->TypeName);
        if (modbus_write_bit(Context->Inner, reg->Address, value ? 1 : 0) >= 0)
            return;
        break;
    case REG_HOLDING:
        if (w == 1) {
            if (modbus_write_register(Context->Inner, reg->Address, value) >= 0)
                return;
        } else if (w > 4)
            throw TSerialDeviceException(
                "can't pack more than 4 " + reg->TypeName +
                "s into a single value");
        else {
            uint16_t v[4];
            for (int p = w - 1; p >= 0; --p) {
                v[p] = value & 0xffff;
                value >>= 16;
            }
            // TBD: actually should use modbus_write_and_read_registers
            // and echo back the value that was read (must be configurable)
            if (modbus_write_registers(Context->Inner, reg->Address, w, v) >= 0)
                return;
        }
        break;
    default:
        throw TSerialDeviceException("can't write to " + reg->TypeName);
    }

    throw TSerialDeviceTransientErrorException(
        "failed to write " + reg->TypeName +
        " @ " + std::to_string(reg->Address));
}

void TModbusDevice::ReadRegisterRange(PRegisterRange range)
{
    auto modbus_range = std::dynamic_pointer_cast<TModbusRegisterRange>(range);
    if (!modbus_range)
        throw std::runtime_error("modbus range expected");
    int type = modbus_range->Type(),
        start = modbus_range->GetStart(),
        count = modbus_range->GetCount();
    if (Port()->Debug())
        std::cerr << "modbus: read " << modbus_range->GetCount() << " " <<
            modbus_range->TypeName() << "(s) @ " << modbus_range->GetStart() <<
            " of slave " << modbus_range->Slave() << std::endl;

    modbus_set_slave(Context->Inner, modbus_range->Slave()->Id);
    int rc;
    if (IsSingleBitType(type))
        rc = type == REG_COIL ?
            modbus_read_bits(Context->Inner, start, count, modbus_range->GetBits()) :
            modbus_read_input_bits(Context->Inner, start, count, modbus_range->GetBits());
    else {
        if (type != REG_HOLDING && type != REG_INPUT)
            throw TSerialDeviceException("invalid register type");
        rc = modbus_range->Type() == REG_HOLDING ?
            modbus_read_registers(Context->Inner, start, count, modbus_range->GetWords()) :
            modbus_read_input_registers(Context->Inner, start, count, modbus_range->GetWords());
    }

    if (rc > 0) {
        modbus_range->SetError(false);
        return;
    }

    modbus_range->SetError(true);
    std::ios::fmtflags f(std::cerr.flags());
    std::cerr << "TModbusDevice::ReadRegisterRange(): failed to read " << modbus_range->GetCount() << " " <<
        modbus_range->TypeName() << "(s) @ " << modbus_range->GetStart() <<
        " of slave " << modbus_range->Slave() << std::endl;
    std::cerr.flags(f);
}
