#include "common_utils.h"
#include <algorithm>
#include <regex>
#include <unordered_map>

namespace
{
    const std::unordered_map<char, std::string> ConvertToMqttStringMap =
        {{'\'', "|"}, {'"', "|"}, {'/', "|"}, {'+', "_plus_"}, {'$', "_"}, {'#', "_"}};

    static std::vector<int> ParseVersionString(const std::string& version)
    {
        std::string buffer;
        std::istringstream stream(version);
        std::vector<int> list;
        while (getline(stream, buffer, '.')) {
            if (std::regex_match(buffer, std::regex("^[0-9]+$"))) {
                list.push_back(std::stoi(buffer));
                continue;
            }
            if (std::regex_match(buffer, std::regex("^[0-9]+(-rc|\\+wb)[0-9]+$"))) {
                int index = buffer.find("-rc");
                int offset = -128;
                if (index < 0) {
                    index = buffer.find("+wb");
                    offset = 128;
                }
                list.push_back(std::stoi(buffer.substr(0, index)));
                list.push_back(std::stoi(buffer.substr(index + 3)) + offset);
                continue;
            }
            list.push_back(0);
        }
        return list;
    }
} // namespace

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

int util::CompareVersionStrings(const std::string& v1, const std::string& v2)
{
    if (v1.empty() && v2.empty()) {
        return 0;
    }
    if (v1.empty()) {
        return -1;
    }
    if (v2.empty()) {
        return 1;
    }
    std::vector<int> l1 = ParseVersionString(v1);
    std::vector<int> l2 = ParseVersionString(v2);
    for (size_t i = 0; i < std::max(l1.size(), l2.size()); ++i) {
        int n1 = i < l1.size() ? l1[i] : 0;
        int n2 = i < l2.size() ? l2[i] : 0;
        if (n1 > n2) {
            return 1;
        }
        if (n1 < n2) {
            return -1;
        }
    }
    return 0;
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
