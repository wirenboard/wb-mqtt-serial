#include "device_setup_item.h"
#include "memory_block_factory.h"
#include "memory_block.h"
#include "ir_device_query_factory.h"
#include "ir_device_query.h"
#include "ir_value.h"

#include <cassert>

using namespace std;

TDeviceSetupItem::TDeviceSetupItem(PSerialDevice device, PDeviceSetupItemConfig config)
    : TDeviceSetupItemConfig(*config)
    , ValueDesc{{}, RegisterConfig->WordOrder}
{
    assert(!RegisterConfig->ReadOnly);  // there's no point in readonly setup item

    ValueDesc.MemoryBlocks = TMemoryBlockFactory::GenerateMemoryBlocks(RegisterConfig, device);
    ManagedValue = TIRValue::Make(*RegisterConfig);
    ManagedValue->SetTextValue(*RegisterConfig, to_string(Value));
}

void TDeviceSetupItem::Initialize()
{
    Query = TIRDeviceQueryFactory::CreateQuery<TIRDeviceValueQuery>(
        {
            GetKeysAsSet(ValueDesc.MemoryBlocks),
            { shared_from_this() }
        }
    );
}

PDeviceSetupItem TDeviceSetupItem::Create(PSerialDevice device, PDeviceSetupItemConfig config)
{
    PDeviceSetupItem instance(new TDeviceSetupItem(device, config));
    instance->Initialize();
    return instance;
}

TDeviceSetupItem::~TDeviceSetupItem()
{}

TIRDeviceValueContext TDeviceSetupItem::GetReadContext() const
{
    assert(false && "Setup item has no read context");
    return GetWriteContext();
}

TIRDeviceValueContext TDeviceSetupItem::GetWriteContext() const
{
    return { ValueDesc, *ManagedValue };
}

void TDeviceSetupItem::InvalidateReadValues()
{
    assert(false && "Read operation on setup item!");
}

string TDeviceSetupItem::ToString() const
{
    return RegisterConfig->ToString();
}

string TDeviceSetupItem::Describe() const
{
    return Query->Describe();
}

PSerialDevice TDeviceSetupItem::GetDevice() const
{
    return Query->GetDevice();
}
