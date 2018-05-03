#include "ir_device_query.h"
#include "ir_device_query_factory.h"
#include "ir_device_memory_view.h"
#include "protocol_register.h"
#include "virtual_register.h"
#include "serial_device.h"

#include <cstring>

using namespace std;

namespace
{
    void PrintAddr(ostream & s, const PMemoryBlock & mb)
    {
        s << mb->Address;
    }

    TPSet<PVirtualRegister> GetVirtualRegisters(const TPSet<PMemoryBlock> & registerSet)
    {
        TPSet<PVirtualRegister> result;

        for (const auto & protocolRegister: registerSet) {
            const auto & localVirtualRegisters = protocolRegister->GetVirtualRegsiters();
            for (const auto & virtualRegister: localVirtualRegisters) {
                const auto & protocolRegisters = virtualRegister->GetMemoryBlocks();
                if (IsSubset(registerSet, protocolRegisters)) {
                    result.insert(virtualRegister);
                }
            }
        }

        return move(result);
    }

    bool DetectHoles(const TPSet<PMemoryBlock> & registerSet)
    {
        int prev = -1;
        for (const auto & mb: registerSet) {
            if (prev >= 0 && (mb->Address - prev) > 1) {
                return true;
            }

            prev = mb->Address;
        }

        return false;
    }

    bool IsSameTypeAndSize(const TPSet<PMemoryBlock> & memoryBlocks)
    {
        auto typeIndex = (*memoryBlocks.begin())->Type.Index;
        auto size = (*memoryBlocks.begin())->Size;

        return all_of(memoryBlocks.begin(), memoryBlocks.end(), [&](const PMemoryBlock & mb){
            return mb->Type.Index == typeIndex && mb->Size == size;
        });
    }
}

TIRDeviceQuery::TIRDeviceQuery(const TPSet<PMemoryBlock> & registerSet, EQueryOperation operation)
    : MemoryBlockRange(TSerialDevice::StaticCreateMemoryBlockRange(*registerSet.begin(), *registerSet.rbegin()))
    , VirtualRegisters(GetVirtualRegisters(registerSet))
    , HasHoles(DetectHoles(registerSet))
    , Operation(operation)
    , Status(EQueryStatus::NotExecuted)
{
    AbleToSplit = (VirtualRegisters.size() > 1);

    assert(IsSameTypeAndSize(registerSet));
}

bool TIRDeviceQuery::operator<(const TIRDeviceQuery & rhs) const noexcept
{
    return *MemoryBlockRange.GetLast() < *rhs.MemoryBlockRange.GetFirst();
}

PSerialDevice TIRDeviceQuery::GetDevice() const
{
    return MemoryBlockRange.GetFirst()->GetDevice();
}

uint32_t TIRDeviceQuery::GetCount() const
{
    return (MemoryBlockRange.GetLast()->Address - MemoryBlockRange.GetFirst()->Address) + 1;
}

uint32_t TIRDeviceQuery::GetStart() const
{
    return MemoryBlockRange.GetFirst()->Address;
}

const TMemoryBlockType & TIRDeviceQuery::GetType() const
{
    return MemoryBlockRange.GetFirst()->Type;
}

const string & TIRDeviceQuery::GetTypeName() const
{
    return MemoryBlockRange.GetFirst()->GetTypeName();
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

void TIRDeviceQuery::InvalidateReadValues()
{
    assert(Operation == EQueryOperation::Read);

    for (const auto & reg: VirtualRegisters) {
        reg->InvalidateReadValues();
    }
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
    return PrintRange(MemoryBlockRange.begin(), MemoryBlockRange.end(), PrintAddr);
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

void TIRDeviceQuery::FinalizeReadImpl(const TIRDeviceMemoryView & memoryView) const
{
    auto memoryBlockSize = (*MemoryBlockRange.begin())->Size;    // it is guaranteed that all blocks in query have same size and type

    assert(Operation == EQueryOperation::Read);
    assert(GetStatus() == EQueryStatus::NotExecuted);

    {
        auto expectedSize = GetCount() * memoryBlockSize;

        if (expectedSize != memoryView.Size) {
            throw TSerialDeviceTransientErrorException("FinalizeRead: unexpected size: " + to_string(memoryView.Size) + " instead of " + to_string(expectedSize));
        }
    }

    for (const auto & mb: MemoryBlockRange) {
        mb->CacheIfNeeded(memoryView.GetMemoryBlockData(mb));
    }

    for (const auto & reg: VirtualRegisters) {
        auto value = memoryView.Get({ reg->GetBoundMemoryBlocks(), reg->WordOrder });

        // TODO: accept value by register
    }

    SetStatus(EQueryStatus::Ok);
}

void TIRDeviceValueQuery::FinalizeWrite() const
{
    assert(Operation == EQueryOperation::Write);
    assert(GetStatus() == EQueryStatus::NotExecuted);

    IterRegisterValues([this](TMemoryBlock & mb, uint64_t value) {
        mb.SetValue(value);
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

TIRDeviceQuerySet::TIRDeviceQuerySet(list<TPSet<PMemoryBlock>> && registerSets, EQueryOperation operation)
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
