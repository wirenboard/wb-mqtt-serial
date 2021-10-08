#include "metrics.h"
#include <iostream>

using namespace std;
using namespace chrono;

namespace
{
    const seconds IntervalDuration(60);

    const size_t MaxBusLoadChunks       = 15;
    const size_t MaxPollIntervalsChunks = 2;
}

Metrics::TPollItem::TPollItem(const std::string& device): Device(device)
{}

Metrics::TPollItem::TPollItem(const std::string& device, const std::string& control): Device(device)
{
    Controls.push_back(control);
}

bool Metrics::TPollItem::operator<(const Metrics::TPollItem& item) const
{
    if (Device == item.Device) {
        return Controls < item.Controls;
    }
    return Device < item.Device;
}

void Metrics::TPollIntervalMetric::RotateChunks()
{
    Intervals.emplace_front();
    if (Intervals.size() > MaxPollIntervalsChunks) {
        Intervals.pop_back();
    }
}

void Metrics::TPollIntervalMetric::AddIntervals(TPollIntervalsMap&        intervals,
                                                std::chrono::milliseconds interval,
                                                size_t                    count) const
{
    auto res = intervals.emplace(interval, count);
    if (!res.second) {
        res.first->second += count;
    }
}

Metrics::TPollIntervalHist Metrics::TPollIntervalMetric::GetHist() const
{
    TPollIntervalsMap intervals;
    size_t            n = 0;
    for (const auto& chunk: Intervals) {
        for (const auto& interval: chunk) {
            AddIntervals(intervals, interval.first, interval.second);
            n += interval.second;
        }
    }
    TPollIntervalHist res;
    if (intervals.empty() || n == 0) {
        return res;
    }
    auto   it    = intervals.rbegin();
    size_t count = 0;
    for (; count < n / 20 && it != intervals.rend(); ++it) {
        count += it->second;
    }
    res.P95 = (count == n / 20) ? it->first : (it--)->first;
    for (; count < n / 2 && it != intervals.rend(); ++it) {
        count += it->second;
    }
    res.P50 = (count == n / 2) ? it->first : (it--)->first;
    return res;
}

void Metrics::TPollIntervalMetric::Poll(steady_clock::time_point time)
{
    if (LastPoll == steady_clock::time_point()) {
        LastPoll = time;
        return;
    }
    if (time >= LastPoll) {
        // Add mising intervals if channel poll start time is after last measured time interval
        for (size_t n = 0; IntervalStartTime + IntervalDuration < time; ++n) {
            IntervalStartTime += IntervalDuration;
            RotateChunks();
            if (n >= MaxPollIntervalsChunks) {
                IntervalStartTime += ((time - IntervalStartTime) / IntervalDuration) * IntervalDuration;
            }
        }
        AddIntervals(Intervals.front(), duration_cast<milliseconds>(time - LastPoll), 1);
        LastPoll = time;
    }
}

void Metrics::TBusLoadMetric::RotateChunks()
{
    Intervals.emplace_front();
    if (Intervals.size() > MaxBusLoadChunks) {
        Intervals.pop_back();
    }
}

void Metrics::TBusLoadMetric::AddInterval(microseconds interval)
{
    auto res = Intervals.front().emplace(PollItem, interval);
    if (!res.second) {
        res.first->second += interval;
    }
}

void Metrics::TBusLoadMetric::StartPoll(const TPollItem& pollItem, steady_clock::time_point time)
{
    // First call
    if (IntervalStartTime == steady_clock::time_point()) {
        IntervalStartTime = time;
        StartPollTime     = time;
        PollItem          = pollItem;
        Intervals.emplace_front();
        return;
    }

    // Add mising intervals if channel poll start time is after last measured time interval
    while (IntervalStartTime + IntervalDuration < time) {
        AddInterval(IntervalDuration - duration_cast<microseconds>(StartPollTime - IntervalStartTime));
        IntervalStartTime += IntervalDuration;
        StartPollTime = IntervalStartTime;
        RotateChunks();
    }

    if (time >= StartPollTime) {
        AddInterval(duration_cast<microseconds>(time - StartPollTime));
        StartPollTime = time;
        PollItem      = pollItem;
    }
}

map<Metrics::TPollItem, Metrics::TBusLoad> Metrics::TBusLoadMetric::GetBusLoad(steady_clock::time_point time)
{
    map<TPollItem, TBusLoad> res;
    if (Intervals.empty()) {
        return res;
    }

    StartPoll(PollItem, time); // Adjust intervals

    auto lastInterval = duration_cast<microseconds>(StartPollTime - IntervalStartTime);
    auto fullInterval = (Intervals.size() - 1) * IntervalDuration + lastInterval;
    if (Intervals.size() > 1) {
        lastInterval += IntervalDuration;
    }
    for (auto chunkIt = Intervals.begin(); chunkIt != Intervals.end(); ++chunkIt) {
        for (const auto& it: *chunkIt) {
            auto& item = res[it.first];
            if (chunkIt == Intervals.begin() || (Intervals.size() > 1 && chunkIt == Intervals.begin() + 1)) {
                item.Minute += it.second.count() / double(lastInterval.count());
            }
            item.FifteenMinutes += it.second.count() / double(fullInterval.count());
        }
    }
    return res;
}

void Metrics::TMetrics::StartPoll(const Metrics::TPollItem& pollItem, chrono::steady_clock::time_point time)
{
    unique_lock<mutex> lk(Mutex);
    BusLoad.StartPoll(pollItem, time);
    PollIntervals[pollItem].Poll(time);
}

map<Metrics::TPollItem, Metrics::TMetrics::TResult> Metrics::TMetrics::GetBusLoad(chrono::steady_clock::time_point time)
{
    map<TPollItem, TResult> res;
    unique_lock<mutex>      lk(Mutex);
    auto                    bl = BusLoad.GetBusLoad(time);
    for (auto it = PollIntervals.begin(); it != PollIntervals.end(); ++it) {
        res.emplace(it->first, TResult{it->second.GetHist(), bl[it->first]});
    }
    return res;
}
