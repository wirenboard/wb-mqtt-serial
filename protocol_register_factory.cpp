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
    const auto formatBitWidth = config->GetFormatBitWidth();
    auto bitsToAllocate = config->GetBitWidth();

    if (formatBitWidth <= config->BitOffset) {
        throw TSerialDeviceException("unable to create protocol registers by config " + config->ToString() +
                                     ": specified bit shift (" + to_string((int)config->BitOffset) +
                                     ") must be less than specified format's (" + RegisterFormatName(config->Format) + ") bit width");
    }

    if (bitsToAllocate > (formatBitWidth - config->BitOffset)) {
        throw TSerialDeviceException("unable to create protocol registers by config " + config->ToString() +
                                     ": specified bit width (" + to_string((int)registerBitWidth) +
                                     ") and shift (" + to_string((int)config->BitOffset) +
                                     ") does not fit into specified format (" + RegisterFormatName(config->Format) + ")");
    }

    uint8_t regIndexStart = config->BitOffset / registerBitWidth;
    uint8_t regIndexEnd = BitCountToRegCount(formatBitWidth, registerBitWidth);

    if (Global::Debug) {
        cerr << "bits: " << (int)bitsToAllocate << endl;
        cerr << "split " << config->ToString() << " to " << (regIndexEnd - regIndexStart) << " " << config->TypeName << " registers" << endl;
    }

    for (auto regIndex = regIndexStart; regIndex < regIndexEnd; ++regIndex) {
        const auto regReverseIndex = regIndexEnd - regIndex - 1;
        const auto type            = config->Type;
        const auto address         = config->Address + regReverseIndex;

        uint8_t startBit = max(int(config->BitOffset) - int(regIndex * registerBitWidth), 0);
        uint8_t endBit   = min(registerBitWidth, uint8_t(startBit + bitsToAllocate));

        auto protocolRegister = device->GetCreateRegister(address, type);

        const auto & insertResult = registersBindInfo.insert({ protocolRegister, { startBit, endBit } });
        const auto & bindInfo = insertResult.first->second;
        bitsToAllocate -= bindInfo.BitCount();

        assert(insertResult.second); // emplace occured - no duplicates
        assert(regIndex != regIndexEnd || bitsToAllocate == 0); // at end of loop all bit should be allocated

        if (!bitsToAllocate) {
            break;
        }
    }

    return move(registersBindInfo);
}
