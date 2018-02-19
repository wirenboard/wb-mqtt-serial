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

    TPSet<TVirtualRegister> GetVirtualRegisters(const TPSet<TProtocolRegister> & registerSet)
    {
        TPSet<TVirtualRegister> result;

        for (const auto & reg: registerSet) {
            const auto & localVirtualRegisters = reg->GetVirtualRegsiters();
            result.insert(localVirtualRegisters.begin(), localVirtualRegisters.end());
        }

        return result;
    }

    template <class Entry>
    PIRDeviceQuery CreateEntry(TPSet<TProtocolRegister> && registerSet)
    {
        return make_shared<Entry>(move(registerSet));
    }

    template <class Entry>
    void AddEntry(TPSet<TProtocolRegister> && registerSet, TPSet<TIRDeviceQuery> & result)
    {
        bool inserted = result.insert(CreateEntry<Entry>(registerSet)).second;
        assert(inserted);
    }
}

TIRDeviceQuery::TIRDeviceQuery(TPSet<TProtocolRegister> && registerSet)
    : Registers(move(registerSet))
    , VirtualRegisters(GetVirtualRegisters(Registers))
    , HasHoles(DetectHoles(Registers))
    , Status(EQueryStatus::OK)
{
    assert(!Registers.empty());
}

bool TIRDeviceQuery::operator<(const TIRDeviceQuery & rhs) const noexcept
{
    const auto & lhsLastRegister = *Registers.rbegin();
    const auto & rhsFirstRegister = *rhs.Registers.begin();

    return *lhsLastRegister < *rhsFirstRegister;
}

PSerialDevice TIRDeviceQuery::GetDevice() const
{
    const auto & firstRegister = *Registers.begin();

    return firstRegister->GetDevice();
}

uint32_t TIRDeviceQuery::GetCount() const
{
    const auto & firstRegister = *Registers.begin(), lastRegister = *Registers.rbegin();

    return (lastRegister->Address - firstRegister->Address) + 1;
}

uint32_t TIRDeviceQuery::GetStart() const
{
    const auto & firstRegister = *Registers.begin();

    return firstRegister->Address;
}

uint32_t TIRDeviceQuery::GetType() const
{
    const auto & firstRegister = *Registers.begin();

    return firstRegister->Type;
}

const std::string & TIRDeviceQuery::GetTypeName() const
{
    const auto & firstRegister = *Registers.begin();

    return firstRegister->GetTypeName();
}

bool TIRDeviceQuery::NeedsSplit() const
{
    return Status == EQueryStatus::DEVICE_PERMANENT_ERROR && GetCount() > 1;
}

const TPSet<TVirtualRegister> & TIRDeviceQuery::GetAffectedVirtualRegisters() const
{
    return VirtualRegisters;
}

string TIRDeviceQuery::Describe() const
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

void TIRDeviceValueQuery::AcceptValues() const
{
    IterRegisterValues([this](TProtocolRegister & reg, uint64_t value) {
        reg.SetValueFromDevice(value);
    });
}

std::string TIRDeviceQuerySet::Describe() const
{
    ostringstream ss;

    ss << "[" << endl;
    for (const auto & query: Queries) {
        ss << "\t" << query->Describe() << endl;
    }
    ss << "]";

    return ss.str();
}

TIRDeviceQuerySet::TIRDeviceQuerySet(const TPSet<TProtocolRegister> & registers)
{
    Initialize(registers, true);
}

PSerialDevice TIRDeviceQuerySet::GetDevice() const
{
    assert(!Queries.empty());

    const auto & query = *Queries.begin();

    return query->GetDevice();
}

void TIRDeviceQuerySet::SplitIfNeeded()
{
    const auto & protocolInfo = GetDevice()->GetProtocolInfo();

    vector<PIRDeviceQuery> newQueries;

    for (auto itEntry = Queries.begin(); itEntry != Queries.end();) {
        const auto & query       = *itEntry;
        const auto & createEntry = protocolInfo.IsSingleBitType(query->GetType()) ? CreateEntry<TIRDeviceSingleBitQuery>
                                                                                  : CreateEntry<TIRDevice64BitQuery>;

        if (query->NeedsSplit) {
            if (query->HasHoles) {
                const auto & generatedQueries = GenerateQueries(query->Registers, false);
                newQueries.insert(newQueries.end(), generatedQueries.begin(), generatedQueries.end());
            } else {
                for (const auto & protocolRegister: query->Registers) {
                    newQueries.push_back(createEntry(TPSet<TProtocolRegister>{ protocolRegister }));
                }
            }

            itEntry = Queries.erase(itEntry);
        } else {
            ++itEntry;
        }
    }

    Queries.insert(newQueries.begin(), newQueries.end());
}

TPSet<TIRDeviceQuery> TIRDeviceQuerySet::GenerateQueries(const TPSet<TProtocolRegister> & registers, bool enableHoles)
{
    assert(!registers.empty());

    const auto & device = (*registers.begin())->GetDevice();

    int prevStart = -1, prevType = -1, prevEnd = -1;

    const auto & deviceConfig = device->DeviceConfig();
    const auto & protocolInfo = device->GetProtocolInfo();

    bool isSingleBitType = protocolInfo.IsSingleBitType((*registers.begin())->Type);

    const auto & addEntry = isSingleBitType ? AddEntry<TIRDeviceSingleBitQuery>
                                            : AddEntry<TIRDevice64BitQuery>;

    int maxHole = enableHoles ? (isSingleBitType ? deviceConfig->MaxBitHole
                                                 : deviceConfig->MaxRegHole)
                              : 0;
    int maxRegs;

    if (isSingleBitType) {
        maxRegs = protocolInfo.GetMaxReadBits();
    } else {
        if ((deviceConfig->MaxReadRegisters > 0) && (deviceConfig->MaxReadRegisters <= protocolInfo.GetMaxReadRegisters())) {
            maxRegs = deviceConfig->MaxReadRegisters;
        } else {
            maxRegs = protocolInfo.GetMaxReadRegisters();
        }
    }

    TPSet<TIRDeviceQuery> result;
    TPSet<TProtocolRegister>   currentRegisterSet;

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
                addEntry(move(currentRegisterSet), result);
            }
            prevStart = protocolRegister->Address;
            prevType = protocolRegister->Type;
        }
        currentRegisterSet.insert(protocolRegister);
        prevEnd = newEnd;
    }

    if (!currentRegisterSet.empty()) {
        addEntry(move(currentRegisterSet), result);
    }

    return result;
}

void TIRDeviceQuerySet::Initialize(const TPSet<TProtocolRegister> & registers, bool enableHoles)
{
    Queries = GenerateQueries(registers, enableHoles);

    if (true) { // TODO: only in debug
        cerr << "Initialized read query: " << Describe() << endl;
    }
}

// void TIRDeviceQuerySet::AddEntry(const PEntry & query)
// {
//     const auto & insertionResult = Queries.insert(query);

//     bool inserted = insertionResult.second;
//     assert(inserted);
// }

// void TIRDeviceQuerySet::AddRegister(const PProtocolRegister & reg)
// {
//     assert(reg->GetDevice() == Device);

//     auto searchQuery = make_shared<TIRDeviceQuery>(reg);

//     const auto & itEntry = Queries.lower_bound(searchQuery);

//     if (itEntry != Queries.end()) {

//     }

// }
