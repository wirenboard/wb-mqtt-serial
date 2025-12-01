#pragma once
#include <chrono>
#include <functional>
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

    /// Compares strings containing version data according to semver
    /// Uses simplified algorithm for WB devices: https://wirenboard.com/wiki/Modbus-hardware-version
    /// For example: empty string < 1.2.3-rc1 < 1.2.3-rc10 < 1.2.3 < 1.2.3+wb1 < 1.2.3+wb10
    /// \param v1 first version string
    /// \param v2 second version string
    /// \return -1 if v1 is lower than v2, 1 if v1 is higher than v2 and 0 if v1 and v2 are equal
    int CompareVersionStrings(const std::string& v1, const std::string& v2);

    typedef std::function<std::chrono::steady_clock::time_point()> TGetNowFn;

    class TSpentTimeMeter
    {
        std::chrono::steady_clock::time_point StartTime;
        TGetNowFn NowFn;

    public:
        TSpentTimeMeter(TGetNowFn nowFn);

        void Start();

        std::chrono::steady_clock::time_point GetStartTime() const;
        std::chrono::microseconds GetSpentTime() const;
    };

    /**
     *  Classes derived from TNonCopyable cannot be copied.
     *  The idea is taken from boost::noncopyable
     */
    class TNonCopyable
    {
    protected:
        TNonCopyable() = default;
        ~TNonCopyable() = default;
        TNonCopyable(const TNonCopyable&) = delete;
        TNonCopyable& operator=(const TNonCopyable&) = delete;
    };
}
