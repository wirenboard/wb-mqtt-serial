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

    bool DetectVirtualRegisters(const vector<PVirtualValue> & values)
    {
        return !values.empty() && dynamic_pointer_cast<TVirtualRegister>(values[0]);
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
    , VirtualValues(move(memoryBlocks.second))
    , HasHoles(DetectHoles(MemoryBlockRange))
    , HasVirtualRegisters(DetectVirtualRegisters(VirtualValues))
    , Operation(operation)
    , Status(EQueryStatus::NotExecuted)
    , Enabled(true)
{
    AbleToSplit = (VirtualValues.size() > 1);

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

TValueSize TIRDeviceQuery::GetBlockSize() const
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

    for (const auto & val: VirtualValues) {
        val->InvalidateReadValues();
    }
}

void TIRDeviceQuery::SetEnabled(bool enabled)
{
    Enabled = enabled;

    if (Global::Debug) {
        cerr << (Enabled ? "re-enabled" : "disabled") << " query: " << Describe() << endl;
    }
}

bool TIRDeviceQuery::IsEnabled() const
{
    return Enabled;
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
    return DescribeOperation() + " " + PrintCollection(MemoryBlockRange, PrintAddr);
}

string TIRDeviceQuery::DescribeVerbose() const
{
    return DescribeOperation() + " " + PrintCollection(MemoryBlockRange, PrintVerbose);
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
    PERF_LOG_SCOPE_DURATION_US

    assert(Operation == EQueryOperation::Read);
    assert(GetStatus() == EQueryStatus::NotExecuted);
    assert(GetSize() == memoryView.Size);

    for (const auto & mb: MemoryBlockRange) {
        *mb->GetCache() = *memoryView[mb];
    }

    for (const auto & val: VirtualValues) {
#ifdef WB_MQTT_SERIAL_VERBOSE_OUTPUT
        const auto & vreg = dynamic_pointer_cast<TVirtualRegister>(val);
        cout << "READING: " << vreg->ToString() << ": " << vreg->Describe() << endl;
#endif
        memoryView.ReadValue(val->GetReadContext());
    }

    SetStatus(EQueryStatus::Ok);
}

TIRDeviceValueQuery::TIRDeviceValueQuery(TAssociatedMemoryBlockSet && memoryBlocks, EQueryOperation operation)
    : TIRDeviceQuery(move(memoryBlocks), operation)
    , MemoryBlocks(move(memoryBlocks.first))
{
    assert(!MemoryBlocks.empty());
}

void TIRDeviceValueQuery::FinalizeWrite() const
{
    PERF_LOG_SCOPE_DURATION_US

    assert(Operation == EQueryOperation::Write);
    assert(GetStatus() == EQueryStatus::NotExecuted);

    // write value to cache
    for (const auto & val: VirtualValues) {
        TIRDeviceMemoryView::WriteValue(val->GetWriteContext(), BlockCache);
    }

    SetStatus(EQueryStatus::Ok);
}

TIRDeviceMemoryView TIRDeviceValueQuery::GetValuesImpl(void * mem, size_t size) const
{
    PERF_LOG_SCOPE_DURATION_US

    assert(GetSize() == size);

    const auto & memoryView = CreateMemoryView(mem, size);
    memoryView.Clear();

    // write cached values first
    for (const auto & mb: MemoryBlockRange) {
        *memoryView[mb] = *mb->GetCache();
    }

    // write payload values on top of cached ones
    for (const auto & val: VirtualValues) {
        memoryView.WriteValue(val->GetWriteContext());
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
