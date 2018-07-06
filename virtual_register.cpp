#include "virtual_register.h"
#include "memory_block.h"
#include "memory_block_factory.h"
#include "serial_device.h"
#include "types.h"
#include "ir_device_query.h"
#include "binary_semaphore.h"
#include "virtual_register_set.h"
#include "ir_device_memory_view.h"
#include "ir_value.h"

#include <wbmqtt/utils.h>

#include <cassert>
#include <cmath>
#include <tuple>

using namespace std;

namespace // utility
{
    template <typename T>
    inline size_t Hash(const T & value)
    {
        return hash<T>()(value);
    }
}

TVirtualRegister::TVirtualRegister(const PRegisterConfig & config, const PSerialDevice & device)
    : TRegisterConfig(*config)
    , Device(device)
    , FlushNeeded(nullptr)
    , ValueToWrite(config->ReadOnly ? nullptr : TIRValue::Make(*this))
    , CurrentValue(TIRValue::Make(*this))
    , WriteQuery(nullptr)
    , ErrorState(EErrorState::UnknownErrorState)
    , ChangedPublishData(EPublishData::None)
    , Dirty(false)
    , Enabled(true)
    , ValueIsRead(false)
    , ValueWasAccepted(false)
{}

void TVirtualRegister::Initialize()
{
    auto device = Device.lock();
    auto self = shared_from_this();

    assert(device);

    MemoryBlocks = TMemoryBlockFactory::GenerateMemoryBlocks(self, device);

    uint64_t width = 0;
    for (const auto memoryBlockBindInfo: MemoryBlocks) {
        const auto & memoryBlock = memoryBlockBindInfo.first;
        const auto & bindInfo = memoryBlockBindInfo.second;

        assert(Type == memoryBlock->Type.Index);
        width += bindInfo.BitCount();
        memoryBlock->AssociateWith(self);
    }

    if (width > RegisterFormatMaxWidth(Format)) {
        throw TSerialDeviceException("unable to create virtual register with width " + to_string(width) + ": must be <= 64");
    }

    if (!ReadOnly) {
        TIRDeviceQuerySet querySet({ self }, EQueryOperation::Write);
        assert(querySet.Queries.size() == 1);

        WriteQuery = dynamic_pointer_cast<TIRDeviceValueQuery>(*querySet.Queries.begin());
        assert(WriteQuery);
    }

    if (Global::Debug)
        cerr << "New virtual register: " << Describe() << endl;
}

uint64_t TVirtualRegister::GetBitPosition() const
{
    return (uint64_t(Address) * MemoryBlocks.begin()->first->Size * 8) + GetWidth() - BitOffset;
}

const PIRDeviceValueQuery & TVirtualRegister::GetWriteQuery() const
{
    return WriteQuery;
}

void TVirtualRegister::WriteValueToQuery()
{
    //WriteQuery->SetValue(GetValueContext(), ValueToWrite);
}

void TVirtualRegister::AcceptDeviceValue()
{
    if (!NeedToPoll()) {
        return;
    }

    assert(!ValueIsRead);

    ValueIsRead = true;

    bool firstPoll = !ValueWasAccepted;
    ValueWasAccepted = true;

    if (HasErrorValue && to_string(ErrorValue) == CurrentValue->GetTextValue(*this)) {
        if (Global::Debug) {
            std::cerr << "register " << ToString() << " contains error value" << std::endl;
        }
        return UpdateReadError(true);
    }

    if (CurrentValue->IsChanged()) {
        CurrentValue->ResetChanged();

        if (Global::Debug) {
            std::ios::fmtflags f(std::cerr.flags());
            //std::cerr << "new val for " << ToString() << ": " << CurrentValue->DescribeShort() << std::endl;
            std::cerr.flags(f);
        }
        Add(ChangedPublishData, EPublishData::Value);
        return UpdateReadError(false);
    }

    if (firstPoll) {
        Add(ChangedPublishData, EPublishData::Value);
    }
    return UpdateReadError(false);
}

void TVirtualRegister::AcceptWriteValue()
{
    CurrentValue->Assign(*ValueToWrite);

    return UpdateWriteError(false);
}

size_t TVirtualRegister::GetHash() const noexcept
{
    return Hash(GetDevice()) ^ Hash(GetBitPosition());
}

bool TVirtualRegister::operator==(const TVirtualRegister & rhs) const noexcept
{
    if (this == &rhs) {
        return true;
    }

    if (GetDevice() != rhs.GetDevice()) {
        return false;
    }

    return GetBitPosition() == rhs.GetBitPosition();
}

bool TVirtualRegister::operator<(const TVirtualRegister & rhs) const noexcept
{
    // comparison makes sense only if registers are of same device
    assert(GetDevice() == rhs.GetDevice());

    return Type < rhs.Type || (Type == rhs.Type && GetValueContext() < rhs.GetValueContext());
}

void TVirtualRegister::SetFlushSignal(PBinarySemaphore flushNeeded)
{
    FlushNeeded = flushNeeded;
}

PSerialDevice TVirtualRegister::GetDevice() const
{
    return Device.lock();
}

TPSet<PMemoryBlock> TVirtualRegister::GetMemoryBlocks() const
{
    return GetKeysAsSet(MemoryBlocks);
}

const TIRBindInfo & TVirtualRegister::GetMemoryBlockBindInfo(const PMemoryBlock & memoryBlock) const
{
    auto it = MemoryBlocks.find(memoryBlock);

    assert(it != MemoryBlocks.end());

    return it->second;
}

EErrorState TVirtualRegister::GetErrorState() const
{
    return ErrorState;
}

std::string TVirtualRegister::Describe() const
{
    ostringstream ss;

    ss << "bit pos: (" << GetBitPosition() << ") [" << endl;

    for (const auto & memoryBlockBindInfo: MemoryBlocks) {
        const auto & memoryBlock = memoryBlockBindInfo.first;
        const auto & bindInfo = memoryBlockBindInfo.second;

        ss << "\t" << memoryBlock->Address << ": " << bindInfo.Describe() << endl;
    }

    ss << "]";

    return ss.str();
}

std::string TVirtualRegister::GetTextValue() const
{
    auto textValue = CurrentValue->GetTextValue(*this);

    return OnValue.empty() ? textValue : (textValue == OnValue ? "1" : "0");
}

void TVirtualRegister::SetTextValue(const std::string & value)
{
    if (ReadOnly) {
        cerr << "WARNING: attempt to write to read-only register. Ignored" << endl;
        return;
    }

    Dirty.store(true);
    ValueToWrite->SetTextValue(*this, OnValue.empty() ? value : (value == "1" ? OnValue : "0"));

    if (FlushNeeded) {
        FlushNeeded->Signal();
    }
}

bool TVirtualRegister::NeedToPoll() const
{
    return Poll && !Dirty;
}

bool TVirtualRegister::IsChanged(EPublishData state) const
{
    return Has(ChangedPublishData, state);
}

bool TVirtualRegister::NeedToFlush() const
{
    return Dirty.load();
}

void TVirtualRegister::Flush()
{
    if (Dirty.load()) {
        Dirty.store(false);

        assert(WriteQuery);
        WriteQuery->ResetStatus();

        //WriteQuery->SetValue(GetValueContext());

        GetDevice()->Execute(WriteQuery);

        UpdateWriteError(WriteQuery->GetStatus() != EQueryStatus::Ok);
    }
}

bool TVirtualRegister::IsEnabled() const
{
    return Enabled;
}

void TVirtualRegister::SetEnabled(bool enabled)
{
    Enabled = enabled;

    if (Global::Debug) {
        cerr << (Enabled ? "re-enabled" : "disabled") << " register: " << ToString() << endl;
    }
}

std::string TVirtualRegister::ToString() const
{
    return "<" + GetDevice()->ToString() + ":" + TRegisterConfig::ToString() + ">";
}

bool TVirtualRegister::AreOverlapping(const TVirtualRegister & other) const
{
    if (GetDevice() == other.GetDevice() && Type == other.Type) {
        const auto & thisValueDesc = GetValueContext();
        const auto & otherValueDesc = other.GetValueContext();

        return !(thisValueDesc < otherValueDesc) &&
               !(otherValueDesc < thisValueDesc);
    }

    return false;
}

void TVirtualRegister::AssociateWithSet(const PVirtualRegisterSet & virtualRegisterSet)
{
    VirtualRegisterSet = virtualRegisterSet;
}

PAbstractVirtualRegister TVirtualRegister::GetTopLevel()
{
    if (const auto & virtualRegisterSet = VirtualRegisterSet.lock()) {
        return virtualRegisterSet;
    } else {
        return shared_from_this();
    }
}

void TVirtualRegister::ResetChanged(EPublishData state)
{
    Remove(ChangedPublishData, state);
}

bool TVirtualRegister::GetValueIsRead() const
{
    return ValueIsRead;
}

TIRDeviceValueContext TVirtualRegister::GetValueContext() const
{
    return { MemoryBlocks, WordOrder, *CurrentValue };
}

TIRDeviceValueContext TVirtualRegister::GetValueToWriteContext() const
{
    assert(ValueToWrite);
    return { MemoryBlocks, WordOrder, *ValueToWrite };
}

void TVirtualRegister::UpdateReadError(bool error)
{
    EErrorState before = ErrorState;

    if (ErrorState == EErrorState::UnknownErrorState) {
        ErrorState = EErrorState::NoError;
    }

    if (error) {
        Add(ErrorState, EErrorState::ReadError);
    } else {
        Remove(ErrorState, EErrorState::ReadError);
    }

    if (ErrorState != before) {
        Add(ChangedPublishData, EPublishData::Error);
        if (Global::Debug) {
            cerr << "UpdateReadError: changed error to " << (int)ErrorState << endl;
        }
    }
}

void TVirtualRegister::UpdateWriteError(bool error)
{
    EErrorState before = ErrorState;

    if (ErrorState == EErrorState::UnknownErrorState) {
        ErrorState = EErrorState::NoError;
    }

    if (error) {
        Add(ErrorState, EErrorState::WriteError);
    } else {
        Remove(ErrorState, EErrorState::WriteError);
    }

    if (ErrorState != before) {
        Add(ChangedPublishData, EPublishData::Error);
        if (Global::Debug) {
            cerr << "UpdateWriteError: changed error to " << (int)ErrorState << endl;
        }
    }
}

void TVirtualRegister::InvalidateReadValues()
{
    ValueIsRead = false;
}

PVirtualRegister TVirtualRegister::Create(const PRegisterConfig & config, const PSerialDevice & device)
{
    auto reg = PVirtualRegister(new TVirtualRegister(config, device));
    reg->Initialize();

    return reg;
}

TVirtualRegister::~TVirtualRegister()
{}
