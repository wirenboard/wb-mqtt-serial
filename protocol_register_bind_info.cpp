#include "protocol_register_bind_info.h"

#include <sstream>
#include <cassert>

using namespace std;


TProtocolRegisterBindInfo::TProtocolRegisterBindInfo(uint8_t start, uint8_t end)
    : BitStart(start)
    , BitEnd(end)
{
    assert(BitStart < BitEnd);
}

string TProtocolRegisterBindInfo::Describe() const
{
    ostringstream ss;
    ss << "[" << (int)BitStart << ", " << (int)BitEnd - 1 << "]";
    return ss.str();
}
