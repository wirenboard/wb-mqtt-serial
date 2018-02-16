#include "ir_device_query.h"
#include "protocol_register.h"
#include "serial_device.h"

#include <cassert>

using namespace std;

namespace
{
    bool DetectHoles(const TPSet<TProtocolRegister> & registerSet)
    {
        int prev = -1;
        for (const auto & reg: registerSet) {
            if (prev >= 0 && (reg->Address - prev) > 1) {
                return true;
            }

            prev = reg->Address;
        }

        return false;
    }
}

TIRDeviceQueryEntry::TIRDeviceQueryEntry(TPSet<TProtocolRegister> && registerSet)
    : Registers(move(registerSet))
    , HasHoles(DetectHoles(Registers))
{
    assert(!Registers.empty());
}

bool TIRDeviceQueryEntry::operator<(const TIRDeviceQueryEntry & rhs) const noexcept
{
    const auto & lhsLastRegister = *Registers.rbegin();
    const auto & rhsFirstRegister = *rhs.Registers.begin();

    return *lhsLastRegister < *rhsFirstRegister;
}

PSerialDevice TIRDeviceQueryEntry::GetDevice() const
{
    const auto & firstRegister = *Registers.begin();

    return firstRegister->GetDevice();
}

uint32_t TIRDeviceQueryEntry::GetCount() const
{
    const auto & firstRegister = *Registers.begin(), lastRegister = *Registers.rbegin();

    return (lastRegister->Address - firstRegister->Address) + 1;
}

uint32_t TIRDeviceQueryEntry::GetStart() const
{
    const auto & firstRegister = *Registers.begin();

    return firstRegister->Address;
}

uint32_t TIRDeviceQueryEntry::GetType() const
{
    const auto & firstRegister = *Registers.begin();

    return firstRegister->Type;
}

const std::string & TIRDeviceQueryEntry::GetTypeName() const
{
    const auto & firstRegister = *Registers.begin();

    return firstRegister->GetTypeName();
}

bool TIRDeviceQueryEntry::NeedsSplit() const
{
    return Status == EQueryStatus::DEVICE_PERMANENT_ERROR && GetCount() > 1;
}

string TIRDeviceQueryEntry::Describe() const
{
    ostringstream ss;

    ss << "[";

    int i = 0;
    for (const auto & reg: Registers) {
        ss << reg->Address;

        if (++i < Registers.size()) {
            ss << ", ";
        }
    }

    ss << "]";

    return ss.str();
}

std::string TIRDeviceQuery::Describe() const
{
    ostringstream ss;

    ss << "[" << endl;
    for (const auto & entry: Entries) {
        ss << "\t" << entry->Describe() << endl;
    }
    ss << "]";

    return ss.str();
}

TIRDeviceReadQuery::TIRDeviceReadQuery(const TPSet<TProtocolRegister> & registers)
{
    Initialize(registers, true);
}

void TIRDeviceReadQuery::SplitIfNeeded()
{
    vector<PEntry> newEntries;

    for (auto itEntry = Entries.begin(); itEntry != Entries.end();) {
        const auto & entry = static_pointer_cast<ActualEntry>(*itEntry);

        if (entry->NeedsSplit) {
            if (entry->HasHoles) {
                const auto & generatedEntries = GenerateEntries(entry->Registers, false);
                newEntries.insert(newEntries.end(), generatedEntries.begin(), generatedEntries.end());
            } else {
                for (const auto & protocolRegister: entry->Registers) {
                    newEntries.push_back(make_shared<ActualEntry>(TPSet<TProtocolRegister>{ protocolRegister }));
                }
            }

            itEntry = Entries.erase(itEntry);
        } else {
            ++itEntry;
        }
    }

    Entries.insert(newEntries.begin(), newEntries.end());
}

TPSet<TIRDeviceReadQuery::Entry> TIRDeviceReadQuery::GenerateEntries(const TPSet<TProtocolRegister> & registers, bool enableHoles)
{
    assert(!registers.empty());

    const auto & device = (*registers.begin())->GetDevice();

    int prevStart = -1, prevType = -1, prevEnd = -1;

    const auto & deviceConfig = device->DeviceConfig();
    const auto & protocolInfo = device->GetProtocolInfo();

    int maxHole = enableHoles ? (protocolInfo.IsSingleBitType((*registers.begin())->Type) ? deviceConfig->MaxBitHole : deviceConfig->MaxRegHole) : 0;
    int maxRegs;

    if (protocolInfo.IsSingleBitType((*registers.begin())->Type)) {
        maxRegs = protocolInfo.GetMaxReadBits();
    } else {
        if ((deviceConfig->MaxReadRegisters > 0) && (deviceConfig->MaxReadRegisters <= protocolInfo.GetMaxReadRegisters())) {
            maxRegs = deviceConfig->MaxReadRegisters;
        } else {
            maxRegs = protocolInfo.GetMaxReadRegisters();
        }
    }

    TPSet<Entry>                result;
    TPSet<TProtocolRegister>    currentRegisterSet;

    for (const auto & protocolRegister: registers) {
        assert(protocolRegister->GetDevice() == device);
        assert(protocolRegister->Type == prevType);

        int newEnd = protocolRegister->Address + 1;
        if (!(prevEnd >= 0 &&
            protocolRegister->Address >= prevEnd &&
            protocolRegister->Address <= prevEnd + maxHole &&
            newEnd - prevStart <= maxRegs))
        {
            if (!currentRegisterSet.empty()) {
                bool inserted = result.insert(make_shared<ActualEntry>(move(currentRegisterSet))).second;
                assert(inserted);
            }
            prevStart = protocolRegister->Address;
            prevType = protocolRegister->Type;
        }
        currentRegisterSet.insert(protocolRegister);
        prevEnd = newEnd;
    }
    if (!currentRegisterSet.empty()) {
        bool inserted = result.insert(make_shared<ActualEntry>(move(currentRegisterSet))).second;
        assert(inserted);
    }

    return result;
}

void TIRDeviceReadQuery::Initialize(const TPSet<TProtocolRegister> & registers, bool enableHoles)
{
    Entries = GenerateEntries(registers, enableHoles);

    if (true) { // TODO: only in debug
        cerr << "Initialized read query: " << Describe() << endl;
    }
}

void TIRDeviceReadQuery::AddEntry(const PEntry & entry)
{
    const auto & insertionResult = Entries.insert(entry);

    bool inserted = insertionResult.second;
    assert(inserted);
}

// void TIRDeviceReadQuery::AddRegister(const PProtocolRegister & reg)
// {
//     assert(reg->GetDevice() == Device);

//     auto searchQuery = make_shared<TIRDeviceQueryEntry>(reg);

//     const auto & itEntry = Entries.lower_bound(searchQuery);

//     if (itEntry != Entries.end()) {

//     }

// }
