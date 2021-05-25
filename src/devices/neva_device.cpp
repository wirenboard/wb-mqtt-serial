#include "neva_device.h"

#include <string.h>
#include <stddef.h>
#include <unistd.h>

#include "iec_common.h"

namespace
{
    const char* LOG_PREFIX = "[NEVA] ";

    const TRegisterTypes RegisterTypes{
        { 0, "obis_cdef",      "value", Double, true },
        { 1, "obis_cdef_pf",   "value", Double, true },
        { 2, "obis_cdef_temp", "value", Double, true },
        { 3, "obis_cdef_1",    "value", Double, true },
        { 4, "obis_cdef_2",    "value", Double, true },
        { 5, "obis_cdef_3",    "value", Double, true },
        { 6, "obis_cdef_4",    "value", Double, true },
        { 7, "obis_cdef_5",    "value", Double, true },
    };

    std::unordered_map<std::string, size_t> RegisterTypeValueIndices = {
        {"obis_cdef",      0},
        {"obis_cdef_pf",   0},
        {"obis_cdef_temp", 0},
        {"obis_cdef_1",    0},
        {"obis_cdef_2",    1},
        {"obis_cdef_3",    2},
        {"obis_cdef_4",    3},
        {"obis_cdef_5",    4},
    };

    uint8_t GetXorCRC(const uint8_t* data, size_t size)
    {
        uint8_t crc = 0;
        for (size_t i = 0; i < size; ++i) {
            crc ^= data[i];
        }
        return crc & 0x7F;
    }
}

void TNevaDevice::Register(TSerialDeviceFactory& factory)
{
    factory.RegisterProtocol(new TIEC61107Protocol("neva", RegisterTypes), 
                             new TBasicDeviceFactory<TNevaDevice>("#/definitions/simple_device_with_broadcast",
                                                                  "#/definitions/common_channel"));
}

TNevaDevice::TNevaDevice(PDeviceConfig device_config, PPort port, PProtocol protocol)
    : TIEC61107ModeCDevice(device_config, port, protocol, LOG_PREFIX, GetXorCRC)
{}

std::string TNevaDevice::GetParameterRequest(const TRegister& reg) const
{
    // Address is 0xCCDDEEFF OBIS value groups
    std::stringstream ss;
    ss << std::hex << std::uppercase << std::setfill('0') << std::setw(8) << GetUint32RegisterAddress(reg.GetAddress()) << "()";
    return ss.str();
}

uint64_t TNevaDevice::GetRegisterValue(const TRegister& reg, const std::string& value)
{
    std::vector<double> result(5);
    int ret = sscanf(value.c_str(), "%lf,%lf,%lf,%lf,%lf", &result[0], &result[1], &result[2], &result[3], &result[4]);
    result.resize(ret);

    size_t val_index = RegisterTypeValueIndices[reg.TypeName];
    
    if (result.size() < val_index + 1) {
        throw TSerialDeviceTransientErrorException("not enough data in response");
    }

    auto val = result[val_index];

    if (reg.TypeName == "obis_cdef_pf") {
        // Y: 0, 1 or 2     (C, L or ?)	YХ.ХХХ
        if (val >= 20) {
            val-=20;
        } else if (val >= 10) {
            val = -(val-10);
        }
    } else if (reg.TypeName == "obis_cdef_temp") {
        if (val >= 100.0) {
            val = -(val - 100.0);
        }
    }

    uint64_t res = 0;
    static_assert((sizeof(res) >= sizeof(val)), "Can't fit double into uint64_t");
    memcpy(&res, &val, sizeof(val));

    return res;
}
