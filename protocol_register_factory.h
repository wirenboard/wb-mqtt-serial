#pragma once

#include "utils.h"
#include "declarations.h"
#include "protocol_register_bind_info.h"

class TProtocolRegisterFactory
{
    TProtocolRegisterFactory() = delete;
public:
    static TPMap<PProtocolRegister, TProtocolRegisterBindInfo> GenerateProtocolRegisters(const PRegisterConfig & config, const PSerialDevice & device);
};
