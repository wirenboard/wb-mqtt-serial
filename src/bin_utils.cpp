#include "bin_utils.h"
#include <assert.h>
#include <stdexcept>

uint64_t BinUtils::GetLSBMask(uint8_t bitCount)
{
    if (bitCount < sizeof(uint64_t) * 8) {
        return (uint64_t(1) << bitCount) - 1;
    }
    return 0xFFFFFFFFFFFFFFFF;
}

void BinUtils::DecodeByteStuffing(std::vector<uint8_t>& data,
                                  const std::unordered_map<uint8_t, std::vector<uint8_t>>& rules)
{
    auto writeIt = data.begin();
    for (auto readIt = data.begin(); readIt != data.end(); ++readIt) {
        bool changed = false;
        for (auto rule: rules) {
            if (rule.second.empty()) {
                throw std::invalid_argument("invalid byte stuffing rule");
            }
            if ((static_cast<size_t>(data.end() - readIt) > rule.second.size()) &&
                std::equal(rule.second.begin(), rule.second.end(), readIt))
            {
                *writeIt = rule.first;
                readIt += rule.second.size() - 1;
                changed = true;
                break;
            }
        }
        if (!changed) {
            *writeIt = *readIt;
        }
        ++writeIt;
    }
    data.resize(writeIt - data.begin());
}
