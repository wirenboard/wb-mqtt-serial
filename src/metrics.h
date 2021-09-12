#pragma once

#include <chrono>
#include <deque>
#include <vector>
#include <string>
#include <map>
#include <mutex>

namespace Metrics
{
    struct TPollIntervalHist
    {
        //! Maximum poll interval of 50% of requests during last 1-2 minutes
        std::chrono::milliseconds P50 = std::chrono::milliseconds::zero();

        //! Maximum poll interval of 95% of requests during last 1-2 minutes
        std::chrono::milliseconds P95 = std::chrono::milliseconds::zero();
    };

    //! Class for channel's poll interval metric calculation
    class TPollIntervalMetric
    {
        //! Map: key - poll interval, value - number of such intervals in a time period
        typedef std::map<std::chrono::milliseconds, size_t> TPollIntervalsMap;

        std::deque<TPollIntervalsMap>         Intervals;
        std::chrono::steady_clock::time_point IntervalStartTime;
        std::chrono::steady_clock::time_point LastPoll;

        void RotateChunks();
        void AddIntervals(TPollIntervalsMap& intervals, std::chrono::milliseconds interval, size_t count) const;
    public:

        TPollIntervalHist GetHist() const;
        void Poll(std::chrono::steady_clock::time_point time);
    };

    struct TBusLoad
    {
        //! Bus load during last small time interval (1-2 minutes) [0, 1]
        double Minute = 0.0;

        //! Bus load during last big interval (14-15 minutes) [0, 1]
        double FifteenMinutes = 0.0;
    };

    class TBusLoadMetric
    {
        //! Map: key - channel name, value - bus time used by the channel
        typedef std::map<std::string, std::chrono::microseconds> TBusLoadMap;

        std::deque<TBusLoadMap>               Intervals;
        std::chrono::steady_clock::time_point IntervalStartTime;
        std::string                           Channel;
        std::chrono::steady_clock::time_point ChannelStartPollTime;

        void RotateChunks();
        void AddInterval(std::chrono::microseconds interval);
    public:

        void StartPoll(const std::string& channel, std::chrono::steady_clock::time_point time);
        std::map<std::string, TBusLoad> GetBusLoad(std::chrono::steady_clock::time_point time);
    };

    class TMetrics
    {
        std::map<std::string, TPollIntervalMetric> PollIntervals;
        TBusLoadMetric                             BusLoad;
        std::mutex                                 Mutex;
    public:
        struct TResult
        {
            TPollIntervalHist Histogram;
            TBusLoad          BusLoad;
        };

        void StartPoll(const std::string& channel, std::chrono::steady_clock::time_point time);

        std::map<std::string, TResult> GetBusLoad(std::chrono::steady_clock::time_point time);
    };
}