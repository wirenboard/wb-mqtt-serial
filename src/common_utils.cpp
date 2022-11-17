#include "common_utils.h"

std::string util::ConvertToMqttTopicValidString(const std::string& src)
{
    std::string validStr;
    for (auto const& ch: src) {
        switch (ch) {
            case '\'':
                [[fallthrough]];
            case '"':
                [[fallthrough]];
            case '/':
                validStr.append(1, '|');
                break;

            case '+':
                validStr.append("_plus_");
                break;
            case '$':
                [[fallthrough]];
            case '#':
                validStr.append(1, '_');
                break;
            default:
                validStr.append(1, ch);
        }
    }

    return validStr;
}