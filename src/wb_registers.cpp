#include "wb_registers.h"
#include "modbus_common.h"

namespace
{
    const auto BAUD_RATE_REGISTER_ADDRESS = 110;
    const auto PARITY_REGISTER_ADDRESS = 111;
    const auto STOP_BITS_REGISTER_ADDRESS = 112;
    const auto CONTINUOUS_READ_REGISTER_ADDRESS = 114;
    const auto SLAVE_ID_REGISTER_ADDRESS = 128;
    const auto DEVICE_MODEL_REGISTER_ADDRESS = 200;
    const auto FW_SIGNATURE_REGISTER_ADDRESS = 290;
    const auto FW_VERSION_REGISTER_ADDRESS = 250;
    const auto SN_REGISTER_ADDRESS = 270;

    const std::unordered_map<std::string, PRegisterConfig> RegConfigs = {
        {WbRegisters::BAUD_RATE_REGISTER_NAME,
         TRegisterConfig::Create(
             Modbus::REG_HOLDING,
             TRegisterDesc{std::make_shared<TUint32RegisterAddress>(BAUD_RATE_REGISTER_ADDRESS), 0, 0},
             U16,
             100)},
        {WbRegisters::PARITY_REGISTER_NAME,
         TRegisterConfig::Create(
             Modbus::REG_HOLDING,
             TRegisterDesc{std::make_shared<TUint32RegisterAddress>(PARITY_REGISTER_ADDRESS), 0, 0})},
        {WbRegisters::STOP_BITS_REGISTER_NAME,
         TRegisterConfig::Create(
             Modbus::REG_HOLDING,
             TRegisterDesc{std::make_shared<TUint32RegisterAddress>(STOP_BITS_REGISTER_ADDRESS), 0, 0})},
        {WbRegisters::CONTINUOUS_READ_REGISTER_NAME,
         TRegisterConfig::Create(
             Modbus::REG_HOLDING,
             TRegisterDesc{std::make_shared<TUint32RegisterAddress>(CONTINUOUS_READ_REGISTER_ADDRESS), 0, 0})},
        {WbRegisters::SLAVE_ID_REGISTER_NAME,
         TRegisterConfig::Create(
             Modbus::REG_HOLDING,
             TRegisterDesc{std::make_shared<TUint32RegisterAddress>(SLAVE_ID_REGISTER_ADDRESS), 0, 0})},
        {WbRegisters::DEVICE_MODEL_REGISTER_NAME,
         TRegisterConfig::Create(Modbus::REG_HOLDING,
                                 TRegisterDesc{std::make_shared<TUint32RegisterAddress>(DEVICE_MODEL_REGISTER_ADDRESS),
                                               0,
                                               6 * sizeof(char) * 8},
                                 String)},
        {WbRegisters::DEVICE_MODEL_EX_REGISTER_NAME,
         TRegisterConfig::Create(Modbus::REG_HOLDING,
                                 TRegisterDesc{std::make_shared<TUint32RegisterAddress>(DEVICE_MODEL_REGISTER_ADDRESS),
                                               0,
                                               20 * sizeof(char) * 8},
                                 String)},
        {WbRegisters::FW_SIGNATURE_REGISTER_NAME,
         TRegisterConfig::Create(Modbus::REG_HOLDING,
                                 TRegisterDesc{std::make_shared<TUint32RegisterAddress>(FW_SIGNATURE_REGISTER_ADDRESS),
                                               0,
                                               12 * sizeof(char) * 8},
                                 String)},
        {WbRegisters::FW_VERSION_REGISTER_NAME,
         TRegisterConfig::Create(Modbus::REG_HOLDING,
                                 TRegisterDesc{std::make_shared<TUint32RegisterAddress>(FW_VERSION_REGISTER_ADDRESS),
                                               0,
                                               16 * sizeof(char) * 8},
                                 String)},
        {WbRegisters::SN_REGISTER_NAME,
         TRegisterConfig::Create(Modbus::REG_HOLDING,
                                 TRegisterDesc{std::make_shared<TUint32RegisterAddress>(SN_REGISTER_ADDRESS), 0, 0},
                                 U32)},
    };
}

PRegisterConfig WbRegisters::GetRegisterConfig(const std::string& name)
{
    auto res = RegConfigs.find(name);
    if (res != RegConfigs.end()) {
        return res->second;
    }
    return nullptr;
}
