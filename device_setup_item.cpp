#include "device_setup_item.h"
#include "virtual_register.h"


using namespace std;

TDeviceSetupItem::TDeviceSetupItem(PSerialDevice device, PDeviceSetupItemConfig config)
    : TDeviceSetupItemConfig(*config)
{
    Register = TVirtualRegister::Create(RegisterConfig, device);
}

bool TDeviceSetupItem::Write() const
{
    Register->SetValue(Value);
    Register->Flush();
    return !Has(Register->GetErrorState(), EErrorState::WriteError);
}

string TDeviceSetupItem::ToString() const
{
    return Register->ToString();
}

string TDeviceSetupItem::Describe() const
{
    return Register->Describe();
}

PSerialDevice TDeviceSetupItem::GetDevice() const
{
    return Register->GetDevice();
}
