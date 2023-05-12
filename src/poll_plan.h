#pragma once

#include <algorithm>
#include <chrono>
#include <memory>
#include <queue>

template<typename TItem> class TPriorityQueue: public std::priority_queue<TItem>
{
public:
    template<typename Pred> bool Contains(Pred pred) const
    {
        return std::find_if(this->c.cbegin(), this->c.cend(), pred) != this->c.cend();
    }
};

template<class TEntry, typename ComparePredicate> class TPriorityQueueSchedule
{
public:
    struct TItem
    {
        TEntry Data;
        std::chrono::steady_clock::time_point Deadline;

        bool operator<(const TItem& item) const
        {
            if (Deadline > item.Deadline) {
                return true;
            }
            if (Deadline == item.Deadline) {
                return ComparePredicate()(Data, item.Data);
            }
            return false;
        }
    };

    bool IsEmpty() const
    {
        return Entries.empty();
    }

    void AddEntry(TEntry entry, std::chrono::steady_clock::time_point deadline)
    {
        Entries.emplace(TItem{entry, deadline});
    }

    std::chrono::steady_clock::time_point GetDeadline() const
    {
        if (Entries.empty()) {
            return std::chrono::steady_clock::time_point::max();
        }
        return Entries.top().Deadline;
    }

    const TItem& GetTop() const
    {
        return Entries.top();
    }

    void Pop()
    {
        Entries.pop();
    }

    bool HasReadyItems(std::chrono::steady_clock::time_point time) const
    {
        return !Entries.empty() && (GetDeadline() <= time);
    }

    bool Contains(TEntry entry) const
    {
        return Entries.Contains([&](const TItem& item) { return item.Data == entry; });
    }

private:
    TPriorityQueue<TItem> Entries;
};

enum class TPriority
{
    High,
    Low
};

enum class TItemAccumulationPolicy
{
    Force,
    AccordingToPollLimitTime
};

enum class TThrottlingState
{
    NoThrottling,
    LowPriorityRateLimit
};

class TRateLimiter
{
    size_t RateLimit;
    size_t Count = 0;
    std::chrono::steady_clock::time_point StartTime;

public:
    TRateLimiter(size_t rateLimit): RateLimit(rateLimit)
    {}

    void NewItem(std::chrono::steady_clock::time_point time)
    {
        if (RateLimit == 0) {
            return;
        }
        if (time - StartTime <= std::chrono::seconds(1)) {
            ++Count;
            return;
        }
        Count = 1;
        StartTime = std::chrono::time_point_cast<std::chrono::seconds>(time);
    }

    bool IsOverLimit(std::chrono::steady_clock::time_point time) const
    {
        if (RateLimit == 0) {
            return false;
        }
        if (time - StartTime <= std::chrono::seconds(1)) {
            return Count > RateLimit;
        }
        return false;
    }

    std::chrono::steady_clock::time_point GetStartTime() const
    {
        return StartTime;
    }
};

class TTotalTimeBalancer
{
    std::chrono::milliseconds TotalTime;

    //! Maximum allowed TotalTime that does not need to be reduced
    std::chrono::milliseconds TotalTimeThreshold;

    //! Limits TotalTime allowed values by [-MaxTotalTime, MaxTotalTime]
    std::chrono::milliseconds MaxTotalTime;

public:
    TTotalTimeBalancer(std::chrono::milliseconds totalTimeThreshold, std::chrono::milliseconds maxTotalTime)
        : TotalTimeThreshold(totalTimeThreshold),
          MaxTotalTime(maxTotalTime)
    {
        Reset();
    }

    void IncrementTotalTime(const std::chrono::milliseconds& delta)
    {
        TotalTime = std::min(TotalTime + delta, MaxTotalTime);
    }

    void DecrementTotalTime(const std::chrono::milliseconds& delta)
    {
        TotalTime = std::max(TotalTime - delta, -MaxTotalTime);
    }

    bool ShouldDecrement() const
    {
        return (TotalTime >= TotalTimeThreshold);
    }

    std::chrono::milliseconds GetTimeToDecrement() const
    {
        auto delta = (TotalTime - TotalTimeThreshold);
        if (delta > std::chrono::milliseconds::zero()) {
            return delta;
        }
        return std::chrono::milliseconds::zero();
    }

    void Reset()
    {
        TotalTime = std::chrono::milliseconds::zero();
    }
};

template<class TEntry, class TComparePredicate = std::less<TEntry>> class TScheduler
{
public:
    using TQueue = TPriorityQueueSchedule<TEntry, TComparePredicate>;
    using TItem = typename TQueue::TItem;

    TScheduler(std::chrono::milliseconds maxLowPriorityLag, size_t lowPriorityRateLimit)
        : TimeBalancer(maxLowPriorityLag, 10 * maxLowPriorityLag),
          LowPriorityRateLimit(lowPriorityRateLimit)
    {
        ResetLoadBalancing();
    }

    void AddEntry(TEntry entry, std::chrono::steady_clock::time_point deadline, TPriority priority)
    {
        if (priority == TPriority::Low) {
            LowPriorityQueue.AddEntry(entry, deadline);
        } else {
            HighPriorityQueue.AddEntry(entry, deadline);
        }
    }

    std::chrono::steady_clock::time_point GetDeadline(std::chrono::steady_clock::time_point time) const
    {
        auto lowPriorityDeadline = LowPriorityQueue.GetDeadline();
        if (!LowPriorityQueue.IsEmpty() && LowPriorityRateLimit.IsOverLimit(time)) {
            lowPriorityDeadline = LowPriorityRateLimit.GetStartTime() + std::chrono::seconds(1);
        }
        return std::min(HighPriorityQueue.GetDeadline(), lowPriorityDeadline);
    }

    std::chrono::steady_clock::time_point GetHighPriorityDeadline() const
    {
        return HighPriorityQueue.GetDeadline();
    }

    /**
     * @brief Selects entries with same priority from queue with deadline less or equal to currentTime
     *
     * @tparam TAccumulator - function or class with method
     *                        bool operator()(TEntry& e,
     *                                        TItemAccumulationPolicy policy,
     *                                        std::chrono::milliseconds limit)
     *                        It is called for each item with expired deadline
     *                        If it returns false next items processing stops.
     * @param currentTime - time against which items deadline is compared
     * @param accumulator - object of TAccumulator
     */
    template<class TAccumulator>
    TThrottlingState AccumulateNext(std::chrono::steady_clock::time_point currentTime, TAccumulator& accumulator)
    {
        if (!LowPriorityQueue.HasReadyItems(currentTime)) {
            ResetLoadBalancing();
        }

        if (HighPriorityQueue.HasReadyItems(currentTime) &&
            (!ShouldSelectLowPriority(currentTime) || !LowPriorityQueue.HasReadyItems(currentTime)))
        {
            bool firstItem = true;
            while (HighPriorityQueue.HasReadyItems(currentTime) &&
                   accumulator(HighPriorityQueue.GetTop().Data,
                               firstItem ? TItemAccumulationPolicy::Force
                                         : TItemAccumulationPolicy::AccordingToPollLimitTime,
                               std::chrono::milliseconds::max()))
            {
                HighPriorityQueue.Pop();
                firstItem = false;
            }
        } else {
            if (LowPriorityQueue.HasReadyItems(currentTime)) {
                const auto pollLimit = GetLowPriorityPollLimit(currentTime);
                bool force = ShouldSelectLowPriority(currentTime);
                bool firstItem = true;
                // Set maximum allowed poll limit to first low priority item,
                // if it is selected to balance load.
                // Following low priority items should be selected only
                // if they poll time is not more than low priority items lag.
                while (LowPriorityQueue.HasReadyItems(currentTime) && !LowPriorityRateLimit.IsOverLimit(currentTime) &&
                       accumulator(LowPriorityQueue.GetTop().Data,
                                   (force && firstItem) ? TItemAccumulationPolicy::Force
                                                        : TItemAccumulationPolicy::AccordingToPollLimitTime,
                                   pollLimit))
                {
                    LowPriorityQueue.Pop();
                    LowPriorityRateLimit.NewItem(currentTime);
                    firstItem = false;
                }
                return LowPriorityRateLimit.IsOverLimit(currentTime) ? TThrottlingState::LowPriorityRateLimit
                                                                     : TThrottlingState::NoThrottling;
            }
        }
        return TThrottlingState::NoThrottling;
    }

    void UpdateSelectionTime(const std::chrono::milliseconds& delta, TPriority priority)
    {
        if (priority == TPriority::High) {
            TimeBalancer.IncrementTotalTime(delta);
        } else {
            TimeBalancer.DecrementTotalTime(delta);
        }
    }

    void ResetLoadBalancing()
    {
        TimeBalancer.Reset();
    }

    bool Contains(TEntry entry)
    {
        return LowPriorityQueue.Contains(entry) || HighPriorityQueue.Contains(entry);
    }

    bool IsEmpty() const
    {
        return LowPriorityQueue.IsEmpty() && HighPriorityQueue.IsEmpty();
    }

private:
    TQueue LowPriorityQueue;
    TQueue HighPriorityQueue;
    TTotalTimeBalancer TimeBalancer;
    TRateLimiter LowPriorityRateLimit;

    std::chrono::milliseconds GetLowPriorityPollLimit(std::chrono::steady_clock::time_point currentTime) const
    {
        if (HighPriorityQueue.IsEmpty()) {
            return std::chrono::milliseconds::max();
        }
        auto delta = std::chrono::ceil<std::chrono::milliseconds>(HighPriorityQueue.GetDeadline() - currentTime);
        if (delta > std::chrono::milliseconds(0)) {
            return delta;
        }
        return GetLowPriorityLag();
    }

    bool ShouldSelectLowPriority(std::chrono::steady_clock::time_point currentTime) const
    {
        return !LowPriorityRateLimit.IsOverLimit(currentTime) && TimeBalancer.ShouldDecrement();
    }

    std::chrono::milliseconds GetLowPriorityLag() const
    {
        return TimeBalancer.GetTimeToDecrement();
    }
};
