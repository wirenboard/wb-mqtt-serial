#pragma once

#include <sstream>
#include <string>


inline void HexDump(std::stringstream& output, const std::vector<uint8_t>& value)
{
    // TBD: move this to libwbmqtt (HexDump?)
    std::ios::fmtflags f(output.flags());
    output << std::hex << std::uppercase << std::setfill('0');
    bool first = true;
    for (uint8_t b: value) {
        if (first)
            first = false;
        else
            output << " ";
        output << std::setw(2) << int(b);
    }
    output.flags(f);
}

inline std::string HexDump(const std::vector<uint8_t>& value)
{
    std::stringstream output;
    HexDump(output, value);
    return output.str();
}
