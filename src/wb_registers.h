#pragma once

#include "register.h"

namespace WbRegisters
{
    const std::string BAUD_RATE_REGISTER_NAME = "baud_rate";
    const std::string PARITY_REGISTER_NAME = "parity";
    const std::string STOP_BITS_REGISTER_NAME = "stop_bits";
    const std::string CONTINUOUS_READ_REGISTER_NAME = "continuous_read";
    const std::string SLAVE_ID_REGISTER_NAME = "slave_id";
    const std::string DEVICE_MODEL_REGISTER_NAME = "device_model";
    const std::string DEVICE_MODEL_EX_REGISTER_NAME = "device_model_ex";
    const std::string FW_SIGNATURE_REGISTER_NAME = "fw_signature";
    const std::string FW_VERSION_REGISTER_NAME = "fw_version";

    const uint16_t COUNTINUOUS_READ_DISABLED = 0;
    const uint16_t COUNTINUOUS_READ_ENABLED_TEMPORARY = 1;
    const uint16_t COUNTINUOUS_READ_ENABLED = 2;

    PRegisterConfig GetRegisterConfig(const std::string& name);

} // namespace
