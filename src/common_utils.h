#pragma once
#include <string>

namespace util
{
    /// Converts a string to a string suitable for use in mqtt topics
    /// \param src original string
    /// \return modified string
    std::string ConvertToMqttTopicValidString(const std::string& src);
}
