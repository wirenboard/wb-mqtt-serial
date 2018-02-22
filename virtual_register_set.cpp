#include "virtual_register_set.h"
#include "virtual_register.h"
#include "serial_exc.h"

#include <wbmqtt/utils.h>

#include <sstream>

using namespace std;

TVirtualRegisterSet::TVirtualRegisterSet(vector<PVirtualRegister> && virtualRegisters)
    : VirtualRegisters(move(virtualRegisters))
{}

std::string TVirtualRegisterSet::GetTextValue() const
{
    ostringstream ss;
    bool first = true;

    for (const auto & virtualRegister: VirtualRegisters) {
        if (!first) {
            ss << ";";
        }

        ss << virtualRegister->GetTextValue();
        first = false;
    }

    return ss.str();
}

void TVirtualRegisterSet::SetTextValue(const std::string & value)
{
    const auto & textValues = StringSplit(value, ';');
    const auto valueCount = textValues.size();
    {
        const auto expectedValueCount = VirtualRegisters.size();

        if (expectedValueCount != valueCount) {
            throw TSerialDeviceException("value count mismatch for register set: expected '" +
                                            to_string(expectedValueCount) + "' actual '" +
                                            to_string(valueCount));
        }
    }

    for (size_t i = 0; i < valueCount; ++i) {
        VirtualRegisters[i]->SetTextValue(textValues[i]);
    }
}
