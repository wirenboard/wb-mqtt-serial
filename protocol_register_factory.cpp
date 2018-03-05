#include "protocol_register_factory.h"
#include "protocol_register.h"
#include "register_config.h"
#include "serial_device.h"

using namespace std;

TPMap<PProtocolRegister, TProtocolRegisterBindInfo> TProtocolRegisterFactory::GenerateProtocolRegisters(const PRegisterConfig & config, const PSerialDevice & device, TRegisterCache && cache)
{
    TPMap<PProtocolRegister, TProtocolRegisterBindInfo> registersBindInfo;

    const auto & regType = device->Protocol()->GetRegTypes()->at(config->TypeName);

    const uint8_t registerBitWidth = RegisterFormatByteWidth(regType.DefaultFormat) * 8;
    auto bitsToAllocate = config->GetBitWidth();

    cerr << "bits: " << (int)bitsToAllocate << endl;

    uint32_t regCount = BitCountToRegCount(bitsToAllocate, registerBitWidth);

    cerr << "registers: " << regCount << endl;

    cerr << "split " << config->ToString() << " to " << regCount << " " << config->TypeName << " registers" << endl;

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

        PProtocolRegister _protocolRegisterFallback = nullptr;

        auto & protocolRegister = (cache ? cache(address) : _protocolRegisterFallback);

        if (!protocolRegister) {
            cerr << "create protocol register at " << address << endl;
            protocolRegister = make_shared<TProtocolRegister>(address, type);
        }

        bitsToAllocate -= bindInfo.BitCount();

        registersBindInfo[protocolRegister] = bindInfo;
    }

    return move(registersBindInfo);
}
