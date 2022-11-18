#include "common_utils.h"
#include <unordered_map>

namespace
{
    const std::unordered_map<char, std::string> ConvertToMqttStringMap =
        {{'\'', "|"}, {'"', "|"}, {'/', "|"}, {'+', "_plus_"}, {'$', "_"}, {'#', "_"}};
}

std::string util::ConvertToMqttTopicValidString(const std::string& src)
{
    std::string validStr;
    for (auto const& ch: src) {
        if (const auto it = ConvertToMqttStringMap.find(ch); it != ConvertToMqttStringMap.end()) {
            validStr += it->second;
        } else {
            validStr += ch;
        }
    }

    return validStr;
}