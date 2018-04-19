#include "protocol_register_bind_info.h"

#include <sstream>
#include <cassert>

using namespace std;


TProtocolRegisterBindInfo::TProtocolRegisterBindInfo(uint16_t start, uint16_t end)
    : BitStart(start)
    , BitEnd(end)
{
    assert(BitStart < BitEnd);
}

string TProtocolRegisterBindInfo::Describe() const
{
    ostringstream ss;
    ss << "[" << BitStart << ", " << BitEnd - 1 << "]";
    return ss.str();
}
