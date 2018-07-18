#include "ir_device_query.h"
#include "ir_device_query_factory.h"
#include "memory_block.h"
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

    void PrintVerbose(ostream & s, const PMemoryBlock & mb)
    {
        s << mb->Describe();
    }

    TPSetRange<PMemoryBlock> GetMemoryBlockRange(const TPSet<PMemoryBlock> & memoryBlocks)
    {
        return TSerialDevice::StaticCreateMemoryBlockRange(*memoryBlocks.begin(), *memoryBlocks.rbegin());
    }

    bool DetectHoles(const TPSetRange<PMemoryBlock> & memoryBlockRange)
    {
        int prev = -1;
        for (const auto & mb: memoryBlockRange) {
            if (prev >= 0 && (mb->Address - prev) > 1) {
                return true;
            }

            prev = mb->Address;
        }

        return false;
    }

    bool IsSameTypeAndSize(const TPSetRange<PMemoryBlock> & memoryBlockRange)
    {
        auto typeIndex = (*memoryBlockRange.begin())->Type.Index;
        auto size = (*memoryBlockRange.begin())->Size;

        return AllOf(memoryBlockRange, [&](const PMemoryBlock & mb){
            return mb->Type.Index == typeIndex && mb->Size == size;
        });
    }

    TIRDeviceMemoryBlockView BlockCache(const CPMemoryBlock & mb){
        return mb->GetCache();
    }
}

TIRDeviceQuery::TIRDeviceQuery(TAssociatedMemoryBlockSet && memoryBlocks, EQueryOperation operation)
    : MemoryBlockRange(GetMemoryBlockRange(memoryBlocks.first))
    , VirtualRegisters(move(memoryBlocks.second))
    , HasHoles(DetectHoles(MemoryBlockRange))
    , Operation(operation)
    , Status(EQueryStatus::NotExecuted)
{
    AbleToSplit = (VirtualRegisters.size() > 1);

    assert(IsSameTypeAndSize(MemoryBlockRange));
}

TIRDeviceQuery::TIRDeviceQuery(const TPSet<PMemoryBlock> & memoryBlocks, EQueryOperation operation)
    : MemoryBlockRange(GetMemoryBlockRange(memoryBlocks))
    , VirtualRegisters()
    , HasHoles(DetectHoles(MemoryBlockRange))
    , Operation(operation)
    , Status(EQueryStatus::NotExecuted)
    , AbleToSplit(false)
{
    assert(IsSameTypeAndSize(MemoryBlockRange));
}

bool TIRDeviceQuery::operator<(const TIRDeviceQuery & rhs) const noexcept
{
    return *MemoryBlockRange.GetLast() < *rhs.MemoryBlockRange.GetFirst();
}

PSerialDevice TIRDeviceQuery::GetDevice() const
{
    return MemoryBlockRange.GetFirst()->GetDevice();
}

uint32_t TIRDeviceQuery::GetBlockCount() const
{
    return (MemoryBlockRange.GetLast()->Address - MemoryBlockRange.GetFirst()->Address) + 1;
}

uint32_t TIRDeviceQuery::GetValueCount() const
{
    return GetBlockCount() * GetType().GetValueCount();
}

uint32_t TIRDeviceQuery::GetStart() const
{
    return MemoryBlockRange.GetFirst()->Address;
}

uint16_t TIRDeviceQuery::GetBlockSize() const
{
    return (*MemoryBlockRange.begin())->Size;    // it is guaranteed that all blocks in query have same size and type
}

uint32_t TIRDeviceQuery::GetSize() const
{
    return GetBlockSize() * GetBlockCount();
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

    if (Status != EQueryStatus::NotExecuted && Status != EQueryStatus::Ok) {
        if (Operation == EQueryOperation::Read) {
            for (const auto & virtualRegister: VirtualRegisters) {
                virtualRegister->UpdateReadError(true);
            }
        } else if (Operation == EQueryOperation::Write) {
            for (const auto & virtualRegister: VirtualRegisters) {
                virtualRegister->UpdateWriteError(true);
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
    return AnyOf(VirtualRegisters, [](const PVirtualRegister & reg){
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
    return DescribeOperation() + " " + PrintRange(MemoryBlockRange.begin(), MemoryBlockRange.end(), PrintAddr);
}

string TIRDeviceQuery::DescribeVerbose() const
{
    return DescribeOperation() + " " + PrintRange(MemoryBlockRange.begin(), MemoryBlockRange.end(), PrintVerbose);
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

TIRDeviceMemoryView TIRDeviceQuery::CreateMemoryView(void * mem, size_t size) const
{
    assert(GetSize() == size);

    return {
        static_cast<uint8_t*>(mem),
        static_cast<uint32_t>(size),
        GetType(), GetStart(), GetBlockSize()
    };
}

TIRDeviceMemoryView TIRDeviceQuery::CreateMemoryView(const void * mem, size_t size) const
{
    assert(GetSize() == size);

    return {
        static_cast<const uint8_t *>(mem),
        static_cast<uint32_t>(size),
        GetType(), GetStart(), GetBlockSize()
    };
}

void TIRDeviceQuery::FinalizeRead(const TIRDeviceMemoryView & memoryView) const
{
    assert(Operation == EQueryOperation::Read);
    assert(GetStatus() == EQueryStatus::NotExecuted);
    assert(GetSize() == memoryView.Size);

    for (const auto & mb: MemoryBlockRange) {
        *mb->GetCache() = *memoryView[mb];
    }

    for (const auto & reg: VirtualRegisters) {
#ifdef WB_MQTT_SERIAL_VERBOSE_OUTPUT
        cout << "READING: " << reg->ToString() << ": " << reg->Describe() << endl;
#endif
        memoryView.ReadValue(reg->GetValueContext());
        reg->AcceptDeviceValue();
    }

    SetStatus(EQueryStatus::Ok);
}

TIRDeviceValueQuery::TIRDeviceValueQuery(TAssociatedMemoryBlockSet && memoryBlocks, EQueryOperation operation)
    : TIRDeviceQuery(move(memoryBlocks), operation)
    , MemoryBlocks(move(memoryBlocks.first))
{
    assert(!MemoryBlocks.empty());

    for (const auto & vreg: VirtualRegisters) {
        AddValueContext(vreg->GetValueToWriteContext());
    }
}

TIRDeviceValueQuery::TIRDeviceValueQuery(TPSet<PMemoryBlock> && memoryBlocks, EQueryOperation operation)
    : TIRDeviceQuery(memoryBlocks, operation)
    , MemoryBlocks(move(memoryBlocks))
{
    assert(!MemoryBlocks.empty());
}

void TIRDeviceValueQuery::AddValueContext(const TIRDeviceValueContext & context)
{
    ValueContexts.push_back(context);
}

void TIRDeviceValueQuery::FinalizeWrite() const
{
    assert(Operation == EQueryOperation::Write);
    assert(GetStatus() == EQueryStatus::NotExecuted);

    // write value to cache
    for (const auto & context: ValueContexts) {
        TIRDeviceMemoryView::WriteValue(context, BlockCache);
    }

    for (const auto & reg: VirtualRegisters) {
        reg->AcceptWriteValue();
    }

    SetStatus(EQueryStatus::Ok);
}

TIRDeviceMemoryView TIRDeviceValueQuery::GetValuesImpl(void * mem, size_t size) const
{
    assert(GetSize() == size);

    const auto & memoryView = CreateMemoryView(mem, size);
    memoryView.Clear();

    // write cached values first
    for (const auto & mb: MemoryBlockRange) {
        *memoryView[mb] = *mb->GetCache();
    }

    // write payload values on top of cached ones
    for (const auto & context: ValueContexts) {
        memoryView.WriteValue(context);
    }

    return memoryView;
}

string TIRDeviceQuerySet::Describe() const
{
    ostringstream ss;

    return PrintCollection(Queries, [](ostream & s, const PIRDeviceQuery & query){
        s << "\t" << query->Describe();
    }, true, "");
}

TIRDeviceQuerySet::TIRDeviceQuerySet(const vector<PVirtualRegister> & virtualRegisters, EQueryOperation operation)
{
    Queries = TIRDeviceQueryFactory::GenerateQueries(virtualRegisters, operation);

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
