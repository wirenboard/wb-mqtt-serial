#pragma once

#include "utils.h"
#include "declarations.h"
#include "protocol_register_bind_info.h"

class TProtocolRegisterFactory
{
    TProtocolRegisterFactory() = delete;

    static std::unordered_map<PSerialDevice, TPSet<PProtocolRegister>> ProtocolRegisters;

public:
    static void ResetCache();
    static TPSetView<PProtocolRegister> CreateRegisterSetView(const PProtocolRegister & first, const PProtocolRegister & last);
    static TPMap<PProtocolRegister, TProtocolRegisterBindInfo> GenerateProtocolRegisters(const PRegisterConfig & config, const PSerialDevice & device);
};
