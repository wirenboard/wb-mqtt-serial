#include "protocol_register_bind_info.h"

#include <sstream>
#include <cassert>

using namespace std;


TMemoryBlockBindInfo::TMemoryBlockBindInfo(uint16_t start, uint16_t end)
    : BitStart(start)
    , BitEnd(end)
{
    assert(BitStart < BitEnd);
}

string TMemoryBlockBindInfo::Describe() const
{
    ostringstream ss;
    ss << "[" << BitStart << ", " << BitEnd - 1 << "]";
    return ss.str();
}
