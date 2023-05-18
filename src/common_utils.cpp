#include "common_utils.h"
#include <algorithm>
#include <unordered_map>

namespace
{
    const std::unordered_map<char, std::string> ConvertToMqttStringMap =
        {{'\'', "|"}, {'"', "|"}, {'/', "|"}, {'+', "_plus_"}, {'$', "_"}, {'#', "_"}};
}

bool util::IsValidMqttTopicString(const std::string& src)
{
    return std::find_if(src.begin(), src.end(), [](auto ch) {
               return ConvertToMqttStringMap.find(ch) != ConvertToMqttStringMap.end();
           }) == src.end();
}

std::string util::ConvertToValidMqttTopicString(const std::string& src)
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

util::TSpentTimeMeter::TSpentTimeMeter(util::TGetNowFn nowFn): NowFn(nowFn)
{}

void util::TSpentTimeMeter::Start()
{
    StartTime = NowFn();
}

std::chrono::steady_clock::time_point util::TSpentTimeMeter::GetStartTime() const
{
    return StartTime;
}

std::chrono::microseconds util::TSpentTimeMeter::GetSpentTime() const
{
    return std::chrono::ceil<std::chrono::microseconds>(NowFn() - StartTime);
}
