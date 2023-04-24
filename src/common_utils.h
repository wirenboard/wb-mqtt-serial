#pragma once
#include <chrono>
#include <string>

namespace util
{
    /// Is the string usable in mqtt topics
    /// \param src string to test
    /// \return true - yes it is
    bool IsValidMqttTopicString(const std::string& src);

    /// Converts a string to a string suitable for use in mqtt topics
    /// \param src original string
    /// \return modified string
    std::string ConvertToValidMqttTopicString(const std::string& src);

    class TSpendTimeMeter
    {
        std::chrono::steady_clock::time_point StartTime;

    public:
        void Start();

        std::chrono::steady_clock::time_point GetStartTime() const;
        std::chrono::microseconds GetSpendTime() const;
    };
}
