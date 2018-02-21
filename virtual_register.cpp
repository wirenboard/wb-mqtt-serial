#include "virtual_register.h"
#include "basic_virtual_register.h"
#include "compound_virtual_register.h"
#include "protocol_register.h"

#include <cassert>

using namespace std;

namespace
{
    inline uint64_t MersenneNumber(uint8_t bitCount)
    {
        assert(bitCount <= 64);
        return (uint64_t(1) << bitCount) - 1;
    }
}


PVirtualRegister TVirtualRegister::Create(const vector<PRegisterConfig> & configs, const PSerialDevice & device, PBinarySemaphore flushNeeded, TInitContext & context)
{
    vector<PVirtualRegister> registers;

    registers.reserve(configs.size());

    for (const auto & config: configs) {
        registers.push_back(make_shared<TBasicVirtualRegister>(config, device, flushNeeded, context));
    }

    assert(!registers.empty());

    if (registers.size() > 1) {
        return make_shared<TCompoundVirtualRegister>(registers);
    }

    return registers.front();
}

uint64_t TVirtualRegister::MapValueFrom(const TPMap<TProtocolRegister, TRegisterBindInfo> & registerMap)
{
    uint64_t value;

    uint8_t bitPosition = 0;
    for (const auto & protocolRegisterReady: registerMap) {
        const auto & protocolRegister = protocolRegisterReady.first;
        const auto & bindInfo = protocolRegisterReady.second;

        assert(bindInfo.IsRead);

        auto absBegin = bindInfo.BitStart + bitPosition;

        auto mask = MersenneNumber(bindInfo.BitCount());
        value |= ((mask & protocolRegister->Value) << absBegin);

        bitPosition += bindInfo.BitCount();
    }

    return value;
}

void TVirtualRegister::MapValueTo(const TPMap<TProtocolRegister, TRegisterBindInfo> & registerMap, uint64_t value)
{
    uint8_t bitPosition = 0;
    for (const auto & protocolRegisterReady: registerMap) {
        const auto & protocolRegister = protocolRegisterReady.first;
        const auto & bindInfo = protocolRegisterReady.second;

        auto mask = MersenneNumber(bindInfo.BitCount());

        auto cachedValue = protocolRegister->Value;

        auto absBegin = bindInfo.BitStart + bitPosition;

        auto registerValue = (~mask & cachedValue) | (mask & (value >> absBegin));

        protocolRegister->SetValueFromClient(registerValue);

        bitPosition += bindInfo.BitCount();
    }
}
