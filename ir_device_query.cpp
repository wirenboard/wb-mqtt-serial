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

        for (const auto & reg: registerSet) {
            const auto & localVirtualRegisters = reg->GetVirtualRegsiters();
            result.insert(localVirtualRegisters.begin(), localVirtualRegisters.end());
        }

        return result;
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
    , Status(EQueryStatus::Unknown)
{
    assert(!ProtocolRegisters.empty());
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

void TIRDeviceQuery::SetValuesFromDevice(const std::vector<uint64_t> & values) const
{
    assert(values.size() == ProtocolRegisters.size());

    for (const auto & regIndex: ProtocolRegisters) {
        regIndex.first->SetReadValue(values[regIndex.second]);
    }
}

void TIRDeviceQuery::SetStatus(EQueryStatus status) const
{
    Status = status;

    if (Status != EQueryStatus::Unknown && Status != EQueryStatus::Ok) {
        if (Operation == EQueryOperation::Read) {
            for (const auto & regIndex: ProtocolRegisters) {
                regIndex.first->SetReadError();
            }
        } else if (Operation == EQueryOperation::Write) {
            for (const auto & regIndex: ProtocolRegisters) {
                regIndex.first->SetWriteError();
            }
        }
    }
}

EQueryStatus TIRDeviceQuery::GetStatus() const
{
    return Status;
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

void TIRDeviceValueQuery::SetValue(const PProtocolRegister & reg, uint64_t value)
{
    auto itRegIndex = ProtocolRegisters.find(reg);
    if (itRegIndex != ProtocolRegisters.end()) {
        SetValue(itRegIndex->second, value);
    }
}

void TIRDeviceValueQuery::AcceptValues() const
{
    IterRegisterValues([this](TProtocolRegister & reg, uint64_t value) {
        reg.SetWriteValue(value);
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

TIRDeviceQuerySet::TIRDeviceQuerySet(list<TPSet<PProtocolRegister>> && registerSets, EQueryOperation operation)
{
    Queries = TIRDeviceQueryFactory::GenerateQueries(move(registerSets), true, operation);

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
