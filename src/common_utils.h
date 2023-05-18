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
