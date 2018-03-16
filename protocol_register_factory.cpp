#include "protocol_register_factory.h"
#include "protocol_register.h"
#include "register_config.h"
#include "serial_device.h"

#include <cassert>

using namespace std;

TPMap<PProtocolRegister, TProtocolRegisterBindInfo> TProtocolRegisterFactory::GenerateProtocolRegisters(const PRegisterConfig & config, const PSerialDevice & device)
{
    TPMap<PProtocolRegister, TProtocolRegisterBindInfo> registersBindInfo;

    ERegisterFormat regFormat;
    try {
        regFormat = device->Protocol()->GetRegType(config->Type).DefaultFormat;
    } catch (out_of_range &) {
        throw TSerialDeviceException("unknown type name: '" + config->TypeName + "' of " + device->ToString() + ": " + config->ToString());
    }

    const uint8_t registerBitWidth = RegisterFormatByteWidth(regFormat) * 8;
    auto bitsToAllocate = config->GetBitWidth();

    uint32_t regCount = BitCountToRegCount(bitsToAllocate, registerBitWidth);

    if (Global::Debug) {
        cerr << "bits: " << (int)bitsToAllocate << endl;

        cerr << "registers: " << regCount << endl;

        cerr << "split " << config->ToString() << " to " << regCount << " " << config->TypeName << " registers" << endl;
    }

    for (auto regIndex = 0u, regIndexEnd = regCount; regIndex != regIndexEnd;) {
        const auto regReverseIndex = regIndexEnd - regIndex - 1;
        const auto type            = config->Type;
        const auto address         = config->Address + regReverseIndex;

        TProtocolRegisterBindInfo bindInfo {};

        bindInfo.BitStart = 0;
        bindInfo.BitEnd = min(registerBitWidth, bitsToAllocate);

        auto localBitShift = max(int(config->BitOffset) - int(regIndex * registerBitWidth), 0);

        bindInfo.BitStart = min(uint16_t(localBitShift), uint16_t(bindInfo.BitEnd));

        if (bindInfo.BitStart == bindInfo.BitEnd) {
            if (bitsToAllocate) {
                ++regIndexEnd;
            }
            continue;
        }

        auto protocolRegister = device->GetCreateRegister(address, type);

        bitsToAllocate -= bindInfo.BitCount();

        registersBindInfo[protocolRegister] = bindInfo;

        ++regIndex;
    }

    return move(registersBindInfo);
}
