#include "protocol_register_factory.h"
#include "protocol_register.h"
#include "register_config.h"
#include "serial_device.h"

#include <cassert>

using namespace std;

void TProtocolRegisterFactory::ResetCache()
{
    ProtocolRegisters.clear();
}

TPSetView<PProtocolRegister> TProtocolRegisterFactory::CreateRegisterSetView(const PProtocolRegister & first, const PProtocolRegister & last)
{
    const auto & device = first->GetDevice();

    assert(device);

    const auto & registerSet = ProtocolRegisters[device];

    assert(!registerSet.empty());

    auto begin = registerSet.find(first);
    auto end = registerSet.find(last);

    assert(begin != registerSet.end());
    assert(end++ != registerSet.end());

    return {begin, end};
}

TPMap<PProtocolRegister, TProtocolRegisterBindInfo> TProtocolRegisterFactory::GenerateProtocolRegisters(const PRegisterConfig & config, const PSerialDevice & device)
{
    TPMap<PProtocolRegister, TProtocolRegisterBindInfo> registersBindInfo;

    const auto & regType = device->Protocol()->GetRegTypes()->at(config->TypeName);

    const uint8_t registerBitWidth = RegisterFormatByteWidth(regType.DefaultFormat) * 8;
    auto bitsToAllocate = config->GetBitWidth();

    uint32_t regCount = BitCountToRegCount(bitsToAllocate, registerBitWidth);

    if (Global::Debug) {
        cerr << "bits: " << (int)bitsToAllocate << endl;

        cerr << "registers: " << regCount << endl;

        cerr << "split " << config->ToString() << " to " << regCount << " " << config->TypeName << " registers" << endl;
    }

    for (auto regIndex = 0u, regIndexEnd = regCount; regIndex != regIndexEnd; ++regIndex) {
        auto type    = config->Type;
        auto address = config->Address + regIndex;

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

        auto protocolRegister = make_shared<TProtocolRegister>(address, type);

        const auto & insRes = ProtocolRegisters[device].insert(protocolRegister);

        if (insRes.second) {
            if (Global::Debug) {
                cerr << "create protocol register at " << address << endl;
            }
        } else {
            protocolRegister = *insRes.first;
        }

        bitsToAllocate -= bindInfo.BitCount();

        registersBindInfo[protocolRegister] = bindInfo;
    }

    return move(registersBindInfo);
}
