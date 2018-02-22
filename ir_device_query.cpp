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
    PIRDeviceQuery CreateQuery(TPSet<TProtocolRegister> && registerSet)
    {
        return make_shared<Entry>(move(registerSet));
    }

    template <class Entry>
    void AddQuery(TPSet<TProtocolRegister> && registerSet, TPSet<TIRDeviceQuery> & result)
    {
        bool inserted = result.insert(CreateQuery<Entry>(registerSet)).second;
        assert(inserted);
    }

    bool IsReadOperation(EQueryOperation operation)
    {
        switch(operation) {
            case EQueryOperation::READ:
                return true;
            case EQueryOperation::WRITE:
                return false;
            default:
                assert(false);
                throw TSerialDeviceException("unknown operation: " + to_string((int)operation));
        }
    }
}

TIRDeviceQuery::TIRDeviceQuery(TPSet<TProtocolRegister> && registerSet)
    : Registers(move(registerSet))
    , VirtualRegisters(GetVirtualRegisters(Registers))
    , HasHoles(DetectHoles(Registers))
    , Operation(EQueryOperation::READ)
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

void TIRDeviceQuery::SetValuesFromDevice(const std::vector<uint64_t> & values) const
{
    assert(values.size() == Registers.size());

    uint32_t i = 0;
    for (const auto & reg: Registers) {
        reg->SetValueFromDevice(values[i++]);
    }
}

const TPSet<TVirtualRegister> & TIRDeviceQuery::GetAffectedVirtualRegisters() const
{
    return VirtualRegisters;
}

void TIRDeviceQuery::SetStatus(EQueryStatus status) const
{
    Status = status;

    if (Status != EQueryStatus::OK) {

    }
}

EQueryStatus TIRDeviceQuery::GetStatus() const
{
    return Status;
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
        reg.SetValueFromClient(value);
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

TIRDeviceQuerySet::TIRDeviceQuerySet(const TPSet<TProtocolRegister> & registers, EQueryOperation operation)
{
    Queries = GenerateQueries(registers, true, operation);

    if (true) { // TODO: only in debug
        cerr << "Initialized read query: " << Describe() << endl;
    }
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
        const bool isRead        = IsReadOperation(query->Operation);
        const bool singleBitType = protocolInfo.IsSingleBitType(query->GetType());

        const auto & createQuery = isRead ? CreateQuery<TIRDeviceQuery>
                                          : singleBitType ? CreateQuery<TIRDeviceSingleBitQuery>
                                                          : CreateQuery<TIRDevice64BitQuery>;

        if (query->NeedsSplit) {
            if (query->HasHoles) {
                const auto & generatedQueries = GenerateQueries(query->Registers, false, query->Operation);
                newQueries.insert(newQueries.end(), generatedQueries.begin(), generatedQueries.end());
            } else {
                for (const auto & protocolRegister: query->Registers) {
                    newQueries.push_back(createQuery(TPSet<TProtocolRegister>{ protocolRegister }));
                }
            }

            itEntry = Queries.erase(itEntry);
        } else {
            ++itEntry;
        }
    }

    Queries.insert(newQueries.begin(), newQueries.end());
}

TPSet<TIRDeviceQuery> TIRDeviceQuerySet::GenerateQueries(const TPSet<TProtocolRegister> & registers, bool enableHoles, EQueryOperation operation)
{
    assert(!registers.empty());

    const auto & device = (*registers.begin())->GetDevice();

    int prevStart = -1, prevType = -1, prevEnd = -1;

    const auto & deviceConfig = device->DeviceConfig();
    const auto & protocolInfo = device->GetProtocolInfo();

    const bool singleBitType = protocolInfo.IsSingleBitType((*registers.begin())->Type);
    const bool isRead = IsReadOperation(operation);

    const auto & addQuery = isRead ? AddQuery<TIRDeviceQuery>
                                   : singleBitType ? AddQuery<TIRDeviceSingleBitQuery>
                                                   : AddQuery<TIRDevice64BitQuery>;

    int maxHole = enableHoles ? (singleBitType ? deviceConfig->MaxBitHole
                                               : deviceConfig->MaxRegHole)
                              : 0;
    int maxRegs;

    if (isRead) {
        if (singleBitType) {
            maxRegs = protocolInfo.GetMaxReadBits();
        } else {
            if ((deviceConfig->MaxReadRegisters > 0) && (deviceConfig->MaxReadRegisters <= protocolInfo.GetMaxReadRegisters())) {
                maxRegs = deviceConfig->MaxReadRegisters;
            } else {
                maxRegs = protocolInfo.GetMaxReadRegisters();
            }
        }
    } else {
        maxRegs = singleBitType ? protocolInfo.GetMaxWriteBits() : protocolInfo.GetMaxWriteRegisters();
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
                addQuery(move(currentRegisterSet), result);
            }
            prevStart = protocolRegister->Address;
            prevType = protocolRegister->Type;
        }
        currentRegisterSet.insert(protocolRegister);
        prevEnd = newEnd;
    }

    if (!currentRegisterSet.empty()) {
        addQuery(move(currentRegisterSet), result);
    }

    return result;
}

// void TIRDeviceQuerySet::AddQuery(const PEntry & query)
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
