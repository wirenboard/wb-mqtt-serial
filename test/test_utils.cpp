#include "test_utils.h"

::testing::AssertionResult ArraysMatch(const std::vector<uint8_t>& v1, const std::vector<uint8_t>& v2)
{
    if (v1 == v2) {
        return ::testing::AssertionSuccess();
    }
    auto res = ::testing::AssertionFailure();
    std::stringstream ss;
    for(auto i: v1) {
        ss << std::hex << std::setw(2) << std::setfill('0') << int(i) << " ";
    }
    ss << "!= ";
    for(auto i: v2) {
        ss << std::hex << std::setw(2) << std::setfill('0') << int(i) << " ";
    }
    res << ss.str();
    return res;
}
