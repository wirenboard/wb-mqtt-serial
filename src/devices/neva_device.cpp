#include "neva_device.h"

#include <stddef.h>
#include <string.h>
#include <unistd.h>

#include "iec_common.h"

namespace
{
    const char* LOG_PREFIX = "[NEVA] ";

    const TRegisterTypes RegisterTypes{// Deprecated. For backward compatibility
                                       {0, "obis_cdef", "value", Double, true},
                                       {1, "obis_cdef_pf", "value", Double, true},
                                       {2, "obis_cdef_temp", "value", Double, true},
                                       {3, "obis_cdef_1", "value", Double, true},
                                       {4, "obis_cdef_2", "value", Double, true},
                                       {5, "obis_cdef_3", "value", Double, true},
                                       {6, "obis_cdef_4", "value", Double, true},
                                       {7, "obis_cdef_5", "value", Double, true},
                                       // Actual register types
                                       {8, "default", "value", Double, true},
                                       {9, "power_factor", "value", Double, true},
                                       {10, "temperature", "value", Double, true},
                                       {11, "item_1", "value", Double, true},
                                       {12, "item_2", "value", Double, true},
                                       {13, "item_3", "value", Double, true},
                                       {14, "item_4", "value", Double, true},
                                       {15, "item_5", "value", Double, true}};

    std::unordered_map<std::string, size_t> RegisterTypeValueIndices = {
        // Deprecated. For backward compatibility
        {"obis_cdef", 0},
        {"obis_cdef_pf", 0},
        {"obis_cdef_temp", 0},
        {"obis_cdef_1", 0},
        {"obis_cdef_2", 1},
        {"obis_cdef_3", 2},
        {"obis_cdef_4", 3},
        {"obis_cdef_5", 4},
        // Actual register types
        {"item_1", 0},
        {"power_factor", 0},
        {"temperature", 0},
        {"item_2", 1},
        {"item_3", 2},
        {"item_4", 3},
        {"item_5", 4}};
}

void TNevaDevice::Register(TSerialDeviceFactory& factory)
{
    factory.RegisterProtocol(new TIEC61107Protocol("neva", RegisterTypes),
                             new TBasicDeviceFactory<TNevaDevice>("#/definitions/simple_device_with_broadcast",
                                                                  "#/definitions/common_channel"));
}

TNevaDevice::TNevaDevice(PDeviceConfig device_config, PPort port, PProtocol protocol)
    : TIEC61107ModeCDevice(device_config, port, protocol, LOG_PREFIX, IEC::CalcXorCRC)
{
    if (DeviceConfig()->Password.empty()) {
        DeviceConfig()->Password = std::vector<uint8_t>{0x00, 0x00, 0x00, 0x00};
    }
}

std::string TNevaDevice::GetParameterRequest(const TRegister& reg) const
{
    // Address is 0xCCDDEEFF OBIS value groups
    std::stringstream ss;
    ss << std::hex << std::uppercase << std::setfill('0') << std::setw(8) << GetUint32RegisterAddress(reg.GetAddress())
       << "()";
    return ss.str();
}

TRegisterValue TNevaDevice::GetRegisterValue(const TRegister& reg, const std::string& v)
{
    if (v.size() < 3 || v.front() != '(' || v.back() != ')') {
        throw TSerialDeviceTransientErrorException("malformed response");
    }
    std::string value(v.substr(1, v.size() - 2));
    std::vector<double> result(5);
    int ret = sscanf(value.c_str(), "%lf,%lf,%lf,%lf,%lf", &result[0], &result[1], &result[2], &result[3], &result[4]);
    result.resize(ret);

    size_t val_index = RegisterTypeValueIndices[reg.TypeName];

    if (result.size() < val_index + 1) {
        throw TSerialDeviceTransientErrorException("not enough data in response");
    }

    auto val = result[val_index];

    if (reg.TypeName == "power_factor" || reg.TypeName == "obis_cdef_pf") {
        // Y: 0, 1 or 2     (C, L or ?)	YХ.ХХХ
        if (val >= 20) {
            val -= 20;
        } else if (val >= 10) {
            val = -(val - 10);
        }
    } else if (reg.TypeName == "temperature" || reg.TypeName == "obis_cdef_temp") {
        if (val >= 100.0) {
            val = -(val - 100.0);
        }
    }
    return TRegisterValue{CopyDoubleToUint64(val)};
}
