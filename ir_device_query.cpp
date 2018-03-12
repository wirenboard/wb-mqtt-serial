#include "ir_device_query.h"
#include "ir_device_query_factory.h"
#include "protocol_register.h"
#include "virtual_register.h"
#include "serial_device.h"

#include <cstring>

using namespace std;

namespace
{
    void PrintAddr(ostream & s, const PProtocolRegister & reg)
    {
        s << reg->Address;
    }

    TPSet<PVirtualRegister> GetVirtualRegisters(const TPSet<PProtocolRegister> & registerSet)
    {
        TPSet<PVirtualRegister> result;

        for (const auto & protocolRegister: registerSet) {
            const auto & localVirtualRegisters = protocolRegister->GetVirtualRegsiters();
            for (const auto & virtualRegister: localVirtualRegisters) {
                const auto & protocolRegisters = virtualRegister->GetProtocolRegisters();
                if (IsSubset(registerSet, protocolRegisters)) {
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
    : RegView(TSerialDevice::StaticCreateRegisterSetView(*registerSet.begin(), *registerSet.rbegin()))
    , VirtualRegisters(GetVirtualRegisters(registerSet))
    , HasHoles(DetectHoles(registerSet))
    , Operation(operation)
    , Status(EQueryStatus::NotExecuted)
{
    AbleToSplit = (VirtualRegisters.size() > 1);

    for (const auto & reg: VirtualRegisters) {
        cerr << "\t" << reg->ToString() << endl;
    }
}

bool TIRDeviceQuery::operator<(const TIRDeviceQuery & rhs) const noexcept
{
    return *RegView.GetLast() < *rhs.RegView.GetFirst();
}

PSerialDevice TIRDeviceQuery::GetDevice() const
{
    return RegView.GetFirst()->GetDevice();
}

uint32_t TIRDeviceQuery::GetCount() const
{
    return (RegView.GetLast()->Address - RegView.GetFirst()->Address) + 1;
}

uint32_t TIRDeviceQuery::GetStart() const
{
    return RegView.GetFirst()->Address;
}

uint32_t TIRDeviceQuery::GetType() const
{
    return RegView.GetFirst()->Type;
}

const string & TIRDeviceQuery::GetTypeName() const
{
    return RegView.GetFirst()->GetTypeName();
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
    return PrintRange(RegView.Begin(), RegView.End(), PrintAddr);
}

string TIRDeviceQuery::DescribeOperation() const
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

void TIRDeviceQuery::FinalizeReadImpl(const void * mem, size_t size, size_t count) const
{
    assert(Operation == EQueryOperation::Read);
    assert(GetStatus() == EQueryStatus::NotExecuted);
    assert(GetCount() == count);
    assert(size <= sizeof(uint64_t));

    auto bytes = static_cast<const uint8_t*>(mem);

    auto itProtocolRegister = RegView.First;

    for (size_t i = 0; i < count; ++i) {
        const auto & protocolRegister = *itProtocolRegister;

        auto addressShift = protocolRegister->Address - GetStart();

        if (addressShift == i) {    // avoid holes values
            uint64_t value = 0;
            memcpy(&value, bytes, size);
            protocolRegister->SetValue(value);
            ++itProtocolRegister;
        }

        bytes += size;
    }

    assert(itProtocolRegister == RegView.End());

    SetStatus(EQueryStatus::Ok);
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

string TIRDeviceQuerySet::Describe() const
{
    ostringstream ss;

    return PrintCollection(Queries, [](ostream & s, const PIRDeviceQuery & query){
        s << "\t" << query->Describe();
    }, true, "");
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
