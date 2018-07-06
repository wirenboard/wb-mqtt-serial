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
{
    assert(!RegisterConfig->ReadOnly);  // there's no point in readonly setup item

    BoundMemoryBlocks = TMemoryBlockFactory::GenerateMemoryBlocks(RegisterConfig, device);
    ManagedValue = TIRValue::Make(*RegisterConfig);
    ManagedValue->SetTextValue(*RegisterConfig, to_string(Value));
    Query = TIRDeviceQueryFactory::CreateQuery<TIRDeviceValueQuery>(GetKeysAsSet(BoundMemoryBlocks));
    Query->AddValueContext(
        { BoundMemoryBlocks, RegisterConfig->WordOrder, *ManagedValue }    // value context
    );
}

TDeviceSetupItem::~TDeviceSetupItem()
{}

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
