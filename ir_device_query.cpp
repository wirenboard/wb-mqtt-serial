#include "ir_device_query.h"
#include "ir_device_query_factory.h"
#include "protocol_register.h"
#include "virtual_register.h"
#include "serial_device.h"

using namespace std;

namespace
{
    template <typename V, typename K, typename C>
    map<K, V, C> IndexedSet(const set<K, C> & in)
    {
        map<K, V, C> out;
        V v = 0;

        for (const K & k: in) {
            out[k] = v++;
        }

        return out;
    }

    TPUnorderedSet<PVirtualRegister> GetVirtualRegisters(const TPSet<PProtocolRegister> & registerSet)
    {
        TPUnorderedSet<PVirtualRegister> result;

        for (const auto & protocolRegister: registerSet) {
            const auto & localVirtualRegisters = protocolRegister->GetVirtualRegsiters();
            for (const auto & virtualRegister: localVirtualRegisters) {
                const auto & protocolRegisters = virtualRegister->GetProtocolRegisters();
                if (includes(registerSet.begin(),       registerSet.end(),
                             protocolRegisters.begin(), protocolRegisters.end())
                ) {
                    result.insert(virtualRegister);
                }
            }
        }

        return move(result);
    }

    bool DetectHoles(const TPSet<PProtocolRegister> & registerSet)
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

TIRDeviceQuery::TIRDeviceQuery(const TPSet<PProtocolRegister> & registerSet, EQueryOperation operation)
    : ProtocolRegisters(IndexedSet<uint16_t>(registerSet))
    , VirtualRegisters(GetVirtualRegisters(registerSet))
    , HasHoles(DetectHoles(registerSet))
    , Operation(operation)
    , Status(EQueryStatus::NotExecuted)
{
    assert(!ProtocolRegisters.empty());

    AbleToSplit = (VirtualRegisters.size() > 1);
}

bool TIRDeviceQuery::operator<(const TIRDeviceQuery & rhs) const noexcept
{
    const auto & lhsLastRegister = *ProtocolRegisters.rbegin()->first;
    const auto & rhsFirstRegister = *rhs.ProtocolRegisters.begin()->first;

    return lhsLastRegister < rhsFirstRegister;
}

PSerialDevice TIRDeviceQuery::GetDevice() const
{
    const auto & firstRegister = *ProtocolRegisters.begin()->first;

    return firstRegister.GetDevice();
}

uint32_t TIRDeviceQuery::GetCount() const
{
    const auto & firstRegister = *ProtocolRegisters.begin()->first, lastRegister = *ProtocolRegisters.rbegin()->first;

    return (lastRegister.Address - firstRegister.Address) + 1;
}

uint32_t TIRDeviceQuery::GetStart() const
{
    const auto & firstRegister = *ProtocolRegisters.begin()->first;

    return firstRegister.Address;
}

uint32_t TIRDeviceQuery::GetType() const
{
    const auto & firstRegister = *ProtocolRegisters.begin()->first;

    return firstRegister.Type;
}

const std::string & TIRDeviceQuery::GetTypeName() const
{
    const auto & firstRegister = *ProtocolRegisters.begin()->first;

    return firstRegister.GetTypeName();
}

void TIRDeviceQuery::FinalizeRead(const std::vector<uint64_t> & values) const
{
    assert(Operation == EQueryOperation::Read);
    assert(GetStatus() == EQueryStatus::NotExecuted);
    assert(values.size() == ProtocolRegisters.size());

    for (const auto & regIndex: ProtocolRegisters) {
        regIndex.first->SetValue(values[regIndex.second]);
    }

    SetStatus(EQueryStatus::Ok);
}

void TIRDeviceQuery::FinalizeRead(const uint64_t & value) const
{
    assert(Operation == EQueryOperation::Read);
    assert(GetStatus() == EQueryStatus::NotExecuted);
    assert(ProtocolRegisters.size() == 1);

    ProtocolRegisters.begin()->first->SetValue(value);

    SetStatus(EQueryStatus::Ok);
}

void TIRDeviceQuery::SetStatus(EQueryStatus status) const
{
    Status = status;

    if (Status != EQueryStatus::NotExecuted) {
        if (Operation == EQueryOperation::Read) {
            for (const auto & virtualRegister: VirtualRegisters) {
                virtualRegister->NotifyRead(Status == EQueryStatus::Ok);
            }
        } else if (Operation == EQueryOperation::Write) {
            for (const auto & virtualRegister: VirtualRegisters) {
                virtualRegister->NotifyWrite(Status == EQueryStatus::Ok);
            }
        }
    }
}

void TIRDeviceQuery::SetStatus(EQueryStatus status)
{
    static_cast<const TIRDeviceQuery *>(this)->SetStatus(status);
}

EQueryStatus TIRDeviceQuery::GetStatus() const
{
    return Status;
}

void TIRDeviceQuery::ResetStatus()
{
    SetStatus(EQueryStatus::NotExecuted);
}

void TIRDeviceQuery::SetEnabledWithRegisters(bool enabled)
{
    for (const auto & reg: VirtualRegisters) {
        reg->SetEnabled(enabled);
    }
}

bool TIRDeviceQuery::IsEnabled() const
{
    return any_of(VirtualRegisters.begin(), VirtualRegisters.end(), [](const PVirtualRegister & reg){
        return reg->IsEnabled();
    });
}

bool TIRDeviceQuery::IsExecuted() const
{
    return Status != EQueryStatus::NotExecuted;
}

bool TIRDeviceQuery::IsAbleToSplit() const
{
    return AbleToSplit;
}

void TIRDeviceQuery::SetAbleToSplit(bool ableToSplit)
{
    AbleToSplit = ableToSplit;
}

string TIRDeviceQuery::Describe() const
{
    ostringstream ss;

    ss << "[";

    size_t i = 0;
    for (const auto & regIndex: ProtocolRegisters) {
        ss << regIndex.first->Address;

        if (++i < ProtocolRegisters.size()) {
            ss << ", ";
        }
    }

    ss << "]";

    return ss.str();
}

std::string TIRDeviceQuery::DescribeOperation() const
{
    switch(Operation) {
        case EQueryOperation::Read:
            return "read";
        case EQueryOperation::Write:
            return "write";
        default:
            return "<unknown operation (code: " + to_string((int)Operation) + ")>";
    }
}

void TIRDeviceValueQuery::SetValue(const PProtocolRegister & reg, uint64_t value) const
{
    auto itRegIndex = ProtocolRegisters.find(reg);
    if (itRegIndex != ProtocolRegisters.end()) {
        SetValue(itRegIndex->second, value);
    }
}

void TIRDeviceValueQuery::FinalizeWrite() const
{
    assert(Operation == EQueryOperation::Write);
    assert(GetStatus() == EQueryStatus::NotExecuted);

    IterRegisterValues([this](TProtocolRegister & reg, uint64_t value) {
        reg.SetValue(value);
    });

    SetStatus(EQueryStatus::Ok);
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

TIRDeviceQuerySet::TIRDeviceQuerySet(list<TPSet<PProtocolRegister>> && registerSets, EQueryOperation operation)
{
    Queries = TIRDeviceQueryFactory::GenerateQueries(move(registerSets), operation);

    if (Global::Debug) {
        cerr << "Initialized query set: " << Describe() << endl;
    }

    assert(!Queries.empty());
}

PSerialDevice TIRDeviceQuerySet::GetDevice() const
{
    assert(!Queries.empty());

    const auto & query = *Queries.begin();

    return query->GetDevice();
}
