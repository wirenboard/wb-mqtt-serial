#include "device_setup_item.h"
#include "memory_block_factory.h"
#include "memory_block.h"
#include "ir_device_query_factory.h"
#include "ir_device_query.h"

#include <cassert>

using namespace std;

TDeviceSetupItem::TDeviceSetupItem(PSerialDevice device, PDeviceSetupItemConfig config)
    : TDeviceSetupItemConfig(*config)
{
    assert(!RegisterConfig->ReadOnly);  // there's no point in readonly setup item

    BoundMemoryBlocks = TMemoryBlockFactory::GenerateMemoryBlocks(RegisterConfig, device);
    Query = TIRDeviceQueryFactory::CreateQuery<TIRDeviceValueQuery>(GetKeysAsSet(BoundMemoryBlocks));
    Query->SetValue({ BoundMemoryBlocks, RegisterConfig->WordOrder }, config->Value);
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
