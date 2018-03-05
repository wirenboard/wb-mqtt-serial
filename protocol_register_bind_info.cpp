#include "protocol_register_bind_info.h"

#include <sstream>

using namespace std;

string TProtocolRegisterBindInfo::Describe() const
{
    ostringstream ss;
    ss << "[" << (int)BitStart << ", " << (int)BitEnd - 1 << "]";
    return ss.str();
}
