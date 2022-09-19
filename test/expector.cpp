#include "expector.h"
#include <string>

std::vector<int> ExpectVectorFromString(const std::string& str)
{
    std::vector<int> res(str.begin(), str.end());
    return res;
}