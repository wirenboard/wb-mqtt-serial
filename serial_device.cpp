#include "serial_device.h"
#include "ir_device_query_factory.h"
#include "ir_device_query.h"
#include "virtual_register.h"
#include "memory_block_factory.h"
#include "memory_block.h"

#include <iostream>
#include <unistd.h>
#include <cassert>
#include <bitset>


namespace
{
    template <typename T = uint64_t>
    inline T MersenneNumber(uint8_t bitCount)
    {
        static_assert(is_unsigned<T>::value, "mersenne number may be only unsigned");
        assert(bitCount <= sizeof(T));
        return (T(1) << bitCount) - 1;
    }
}

TDeviceSetupItem::TDeviceSetupItem(PSerialDevice device, PDeviceSetupItemConfig config)
    : TDeviceSetupItemConfig(*config)
{
    const auto & protocolRegisters = TMemoryBlockFactory::GenerateMemoryBlocks(config->RegisterConfig, device);

    auto queries = TIRDeviceQueryFactory::GenerateQueries({ GetKeysAsSet(protocolRegisters) }, EQueryOperation::Write);
    assert(queries.size() == 1);

    Query = std::dynamic_pointer_cast<TIRDeviceValueQuery>(*queries.begin());
    assert(Query);

    TVirtualRegister::MapValueTo(Query, protocolRegisters, Value);
}

bool TProtocolInfo::IsSingleBitType(const TMemoryBlockType & type) const
{
    return false;
}

int TProtocolInfo::GetMaxReadRegisters() const
{
    return 1;
}

int TProtocolInfo::GetMaxReadBits() const
{
    return 1;
}

int TProtocolInfo::GetMaxWriteRegisters() const
{
    return 1;
}

int TProtocolInfo::GetMaxWriteBits() const
{
    return 1;
}


TSerialDevice::TSerialDevice(PDeviceConfig config, PPort port, PProtocol protocol)
    : Delay(config->Delay)
    , SerialPort(port)
    , _DeviceConfig(config)
    , _Protocol(protocol)
    , LastSuccessfulCycle()
    , IsDisconnected(false)
    , RemainingFailCycles(config->DeviceMaxFailCycles)
{}

TSerialDevice::~TSerialDevice()
{
    /* TSerialDeviceFactory::RemoveDevice(shared_from_this()); */
}

const TProtocolInfo & TSerialDevice::GetProtocolInfo() const
{
    static TProtocolInfo info;
    return info;
}

std::string TSerialDevice::ToString() const
{
    return DeviceConfig()->Name + "(" + DeviceConfig()->SlaveId + ")";
}

void TSerialDevice::Execute(const PIRDeviceQuery & query)
{
    assert(query);

    try {
        try {
            switch(query->Operation) {
                case EQueryOperation::Read:
                    return Read(*query);

                case EQueryOperation::Write:
                    return Write(query->As<TIRDeviceValueQuery>());

                default:
                    throw TSerialDeviceException("unsupported query operation");
            }

            if (!query->IsExecuted()) {
                throw TSerialDeviceUnknownErrorException("query was not executed");
            }

        } catch (const std::exception & e) {
            std::ios::fmtflags f(std::cerr.flags());
            std::cerr << "TSerialDevice::Execute(): failed to execute " << query->DescribeOperation() << " query " << query->Describe() << " of device " << ToString() << ": " << e.what() << std::endl;
            std::cerr.flags(f);
            throw;
        }
    } catch (const TSerialDeviceTransientErrorException & e) {
        query->SetStatus(EQueryStatus::DeviceTransientError);
    } catch (const TSerialDevicePermanentErrorException & e) {
        query->SetStatus(EQueryStatus::DevicePermanentError);
    } catch (const TSerialDeviceUnknownErrorException & e) {
        query->SetStatus(EQueryStatus::UnknownError);
    }

    assert(query->IsExecuted());
}

PMemoryBlock TSerialDevice::GetCreateRegister(uint32_t address, uint32_t type, uint16_t size)
{
    PMemoryBlock protocolRegister(new TMemoryBlock(address, size, type, shared_from_this()));

    const auto & insRes = MemoryBlocks.insert(protocolRegister);

    if (insRes.second) {
        if (Global::Debug) {
            std::cerr << "device " << ToString() << ": create register at " << address << std::endl;
        }
    } else {
        protocolRegister = *insRes.first;
    }

    return protocolRegister;
}

TPSetRange<PMemoryBlock> TSerialDevice::CreateMemoryBlockRange(const PMemoryBlock & first, const PMemoryBlock & last) const
{
    assert(!MemoryBlocks.empty());

    auto itFirst = MemoryBlocks.find(first);
    auto itLast  = MemoryBlocks.find(last);

    assert(itFirst != MemoryBlocks.end());
    assert(itLast  != MemoryBlocks.end());

    return {itFirst, itLast};
}

TPSetRange<PMemoryBlock> TSerialDevice::StaticCreateMemoryBlockRange(const PMemoryBlock & first, const PMemoryBlock & last)
{
    auto device = first->GetDevice();

    assert(device);

    return device->CreateMemoryBlockRange(first, last);
}

void TSerialDevice::Prepare()
{
    Port()->Sleep(Delay);
}

void TSerialDevice::SleepGuardInterval() const
{
    if (DeviceConfig()->GuardInterval.count()) {
        Port()->Sleep(DeviceConfig()->GuardInterval);
    }
}

void TSerialDevice::Read(const TIRDeviceQuery & query)
{
    assert(query.GetCount() == 1);

    const auto & mb = query.MemoryBlockRange.GetFirst();

    SleepGuardInterval();

    query.FinalizeRead(ReadMemoryBlock(mb));
}

void TSerialDevice::Write(const TIRDeviceValueQuery & query)
{
    assert(query.GetCount() == 1);

    const auto & mb = query.MemoryBlockRange.GetFirst();
    uint64_t value;
    query.GetValues<uint64_t>(&value);

    SleepGuardInterval();

    WriteMemoryBlock(mb, value);
    query.FinalizeWrite();
}

uint64_t TSerialDevice::ReadMemoryBlock(const PMemoryBlock & mb)
{
    throw TSerialDeviceException("ReadMemoryBlock is not implemented");
}

void TSerialDevice::WriteMemoryBlock(const PMemoryBlock & mb, uint64_t value)
{
    throw TSerialDeviceException("WriteMemoryBlock is not implemented");
}

uint64_t TSerialDevice::ReadValue(const TIRDeviceMemoryViewR & memoryView, const TIRDeviceValueDesc & valueDesc) const
{
    uint64_t value = 0;

    uint8_t bitPosition = 0;

    auto readMemoryBlock = [&](const std::pair<const PMemoryBlock, TMemoryBlockBindInfo> & boundMemoryBlock) {
        const auto & memoryBlock = boundMemoryBlock.first;
        const auto & bindInfo = boundMemoryBlock.second;

        const auto mask = MersenneNumber(bindInfo.BitCount()) << bindInfo.BitStart;

        for (uint16_t iByte = BitCountToByteCount(bindInfo.BitStart) - 1; iByte < memoryBlock->Size; ++iByte) {
            uint16_t begin = std::max(uint16_t(iByte * 8), bindInfo.BitStart);
            uint16_t end = std::min(uint16_t((iByte + 1) * 8), bindInfo.BitEnd);

            if (begin >= end)
                continue;

            uint64_t bits = memoryView.GetByte(memoryBlock, iByte) & (mask >> (iByte * 8));
            value |= bits << bitPosition;

            auto bitCount = end - begin;
            bitPosition += bitCount;
        }

        // TODO: check for usefullness
        if (Global::Debug) {
            std::cerr << "mb mask: " << std::bitset<64>(mask) << std::endl;
            std::cerr << "reading " << bindInfo.Describe() << " bits of " << memoryBlock->Describe()
                 << " to [" << (int)bitPosition << ", " << int(bitPosition + bindInfo.BitCount() - 1) << "] bits of value" << std::endl;
        }
    };

    if (valueDesc.WordOrder == EWordOrder::BigEndian) {
        std::for_each(valueDesc.BoundMemoryBlocks.rbegin(), valueDesc.BoundMemoryBlocks.rend(), readMemoryBlock);
    } else {
        std::for_each(valueDesc.BoundMemoryBlocks.begin(), valueDesc.BoundMemoryBlocks.end(), readMemoryBlock);
    }

    if (Global::Debug)
        std::cerr << "map value from registers: " << value << std::endl;

    return value;
}

void TSerialDevice::WriteValue(const TIRDeviceMemoryViewRW & memoryView, const TIRDeviceValueDesc & valueDesc, uint64_t value) const
{
    if (Global::Debug)
        std::cerr << "map value to registers: " << value << std::endl;

    uint8_t bitPosition = 0;

    auto writeMemoryBlock = [&](const std::pair<const PMemoryBlock, TMemoryBlockBindInfo> & protocolRegisterBindInfo){
        const auto & memoryBlock = protocolRegisterBindInfo.first;
        const auto & bindInfo = protocolRegisterBindInfo.second;

        const auto mask = MersenneNumber(bindInfo.BitCount()) << bindInfo.BitStart;

        for (uint16_t iByte = BitCountToByteCount(bindInfo.BitStart) - 1; iByte < memoryBlock->Size; ++iByte) {
            uint16_t begin = std::max(uint16_t(iByte * 8), bindInfo.BitStart);
            uint16_t end = std::min(uint16_t((iByte + 1) * 8), bindInfo.BitEnd);

            if (begin >= end)
                continue;

            uint8_t byteMask = mask >> (iByte * 8);

            auto cachedBits = memoryBlock->GetCachedByte(iByte);

            memoryView.GetByte(memoryBlock, iByte) = (~byteMask & cachedBits) | (byteMask & (value >> bitPosition));

            auto bitCount = end - begin;
            bitPosition += bitCount;
        }
    };

    if (valueDesc.WordOrder == EWordOrder::BigEndian) {
        for_each(valueDesc.BoundMemoryBlocks.rbegin(), valueDesc.BoundMemoryBlocks.rend(), writeMemoryBlock);
    } else {
        for_each(valueDesc.BoundMemoryBlocks.begin(), valueDesc.BoundMemoryBlocks.end(), writeMemoryBlock);
    }
}

void TSerialDevice::EndPollCycle() {}

void TSerialDevice::OnCycleEnd(bool ok)
{
    // disable reconnect functionality option
    if (_DeviceConfig->DeviceTimeout.count() < 0 || _DeviceConfig->DeviceMaxFailCycles < 0) {
        return;
    }

    if (ok) {
        LastSuccessfulCycle = std::chrono::steady_clock::now();
        IsDisconnected = false;
        RemainingFailCycles = _DeviceConfig->DeviceMaxFailCycles;
    } else {
        if (LastSuccessfulCycle == std::chrono::steady_clock::time_point()) {
            LastSuccessfulCycle = std::chrono::steady_clock::now();
        }

        if (RemainingFailCycles > 0) {
            --RemainingFailCycles;
        }

        if ((std::chrono::steady_clock::now() - LastSuccessfulCycle > _DeviceConfig->DeviceTimeout) &&
            RemainingFailCycles == 0)
        {
            if (!IsDisconnected) {
                IsDisconnected = true;
                std::cerr << "device " << ToString() << " disconnected" << std::endl;
            }
        }
    }
}

bool TSerialDevice::GetIsDisconnected() const
{
	return IsDisconnected;
}

void TSerialDevice::InitializeMemoryBlocksCache()
{
    assert(Cache.capacity() == 0);

    auto size = std::accumulate(MemoryBlocks.begin(), MemoryBlocks.end(), size_t(0), [](size_t size, const PMemoryBlock & mb){
        if (mb->NeedsCaching()) {
            size += mb->Size;
        }
    });

    Cache.reserve(size);

    for (const auto mb: MemoryBlocks) {
        if (mb->NeedsCaching()) {
            Cache.resize(Cache.size() + mb->Size);
            mb->AssignCache(&Cache[Cache.size() - mb->Size]);
        }
    }

    assert(Cache.capacity() == size);
    assert(Cache.size() == size);
}

void TSerialDevice::InitSetupItems()
{
	for (auto& setup_item_config: _DeviceConfig->SetupItemConfigs) {
		SetupItems.push_back(std::make_shared<TDeviceSetupItem>(shared_from_this(), setup_item_config));
	}
}

bool TSerialDevice::HasSetupItems() const
{
    return !SetupItems.empty();
}

bool TSerialDevice::WriteSetupRegisters(bool tryAll)
{
    bool did_write = false;
    for (const auto & setupItem : SetupItems) {
        std::cerr << "Init: " << setupItem->Name << ": setup register " <<
                setupItem->Query->Describe() << " <-- " << setupItem->Value << std::endl;
        Execute(setupItem->Query);

        bool ok = setupItem->Query->GetStatus() == EQueryStatus::Ok;

        setupItem->Query->ResetStatus();

        if (ok) {
            did_write = true;
        } else {
            std::cerr << "WARNING: device '" << setupItem->Query->GetDevice()->ToString() <<
                "' registers '" << setupItem->Query->Describe() <<
                "' setup failed" << std::endl;
            if (!did_write && !tryAll) {
                break;
            }
        }
    }

    return did_write;
}

std::unordered_map<std::string, PProtocol>
    *TSerialDeviceFactory::Protocols = 0;

void TSerialDeviceFactory::RegisterProtocol(PProtocol protocol)
{
    if (!Protocols)
        Protocols = new std::unordered_map<std::string, PProtocol>();

    Protocols->insert(std::make_pair(protocol->GetName(), protocol));
}

const PProtocol TSerialDeviceFactory::GetProtocolEntry(PDeviceConfig device_config)
{
    return TSerialDeviceFactory::GetProtocol(device_config->Protocol);
}

PProtocol TSerialDeviceFactory::GetProtocol(const std::string &name)
{
    auto it = Protocols->find(name);
    if (it == Protocols->end())
        throw TSerialDeviceException("unknown serial protocol: " + name);
    return it->second;
}

PSerialDevice TSerialDeviceFactory::CreateDevice(PDeviceConfig device_config, PPort port)
{
    return GetProtocolEntry(device_config)->CreateDevice(device_config, port);
}

void TSerialDeviceFactory::RemoveDevice(PSerialDevice device)
{
    if (device) {
        device->Protocol()->RemoveDevice(device);
    } else {
        throw TSerialDeviceException("can't remove empty device");
    }
}

PSerialDevice TSerialDeviceFactory::GetDevice(const std::string& slave_id, const std::string& protocol_name, PPort port)
{
    return GetProtocol(protocol_name)->GetDevice(slave_id, port);
}

PRegisterTypeMap TSerialDeviceFactory::GetRegisterTypes(PDeviceConfig device_config)
{
    return GetProtocolEntry(device_config)->GetRegTypes();
}

template<>
int TBasicProtocolConverter<int>::ConvertSlaveId(const std::string &s) const
{
    try {
        return std::stoi(s, /* pos = */ 0, /* base = */ 0);
    } catch (const std::logic_error &e) {
        throw TSerialDeviceException("slave ID \"" + s + "\" is not convertible to string");
    }
}

template<>
TAggregatedSlaveId TBasicProtocolConverter<TAggregatedSlaveId>::ConvertSlaveId(const std::string &s) const
{
    try {
        auto delimiter_it = s.find(':');
        return {std::stoi(s.substr(0, delimiter_it), 0, 0), std::stoi(s.substr(delimiter_it + 1), 0, 0)};
    } catch (const std::logic_error &e) {
        throw TSerialDeviceException("slave ID \"" + s + "\" is not convertible to string");
    }
}
