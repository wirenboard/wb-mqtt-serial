#pragma once

#include <chrono>
#include <memory>
#include <queue>

template<class TEntry, typename ComparePredicate> class TPriorityQueueSchedule
{
public:
    struct TItem
    {
        TEntry Data;
        std::chrono::steady_clock::time_point NextPollTime;

        bool operator<(const TItem& item) const
        {
            if (NextPollTime > item.NextPollTime) {
                return true;
            }
            if (NextPollTime == item.NextPollTime) {
                return ComparePredicate()(Data, item.Data);
            }
            return false;
        }
    };

    bool IsEmpty() const
    {
        return Entries.empty();
    }

    void AddEntry(TEntry entry, std::chrono::steady_clock::time_point nextPollTime)
    {
        Entries.emplace(TItem{entry, nextPollTime});
    }

    std::chrono::steady_clock::time_point GetNextPollTime() const
    {
        if (Entries.empty()) {
            return std::chrono::steady_clock::time_point::max();
        }
        return Entries.top().NextPollTime;
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
        return !Entries.empty() && (GetNextPollTime() <= time);
    }

private:
    std::priority_queue<TItem> Entries;
};

enum class TPriority
{
    High,
    Low
};

template<class TEntry, class TComparePredicate> class TScheduler
{
public:
    using TQueue = TPriorityQueueSchedule<TEntry, TComparePredicate>;
    using TItem = typename TQueue::TItem;

    TScheduler(std::chrono::microseconds forcedLowPriorityInterval)
        : ForcedLowPriorityInterval(forcedLowPriorityInterval)
    {
        if (ForcedLowPriorityInterval <= std::chrono::microseconds::zero()) {
            throw std::runtime_error("poll interval must be greater than zero ms");
        }
        ForcedLowPriorityCallTime = std::chrono::steady_clock::now() + ForcedLowPriorityInterval;
    }

    void AddEntry(TEntry entry, std::chrono::steady_clock::time_point nextPollTime, TPriority priority)
    {
        if (priority == TPriority::Low) {
            LowPriorityQueue.AddEntry(entry, nextPollTime);
        } else {
            HighPriorityQueue.AddEntry(entry, nextPollTime);
        }
    }

    std::chrono::steady_clock::time_point GetNextPollTime() const
    {
        return std::min(HighPriorityQueue.GetNextPollTime(), LowPriorityQueue.GetNextPollTime());
    }

    template<class TAccumulator> void GetNext(std::chrono::steady_clock::time_point time, TAccumulator& accumulator)
    {
        auto pollLimit = std::chrono::milliseconds::max();
        if (HighPriorityQueue.HasReadyItems(time) &&
            (LastSelectedWasFromLowPriorityQueue || (ForcedLowPriorityCallTime > time) || LowPriorityQueue.IsEmpty()))
        {
            while (HighPriorityQueue.HasReadyItems(time) && accumulator(HighPriorityQueue.GetTop().Data, pollLimit)) {
                HighPriorityQueue.Pop();
                LastSelectedWasFromLowPriorityQueue = false;
            }
            return;
        }
        if (LowPriorityQueue.HasReadyItems(time)) {
            ForcedLowPriorityCallTime = time + ForcedLowPriorityInterval;
            LastSelectedWasFromLowPriorityQueue = true;
            if (!HighPriorityQueue.IsEmpty()) {
                pollLimit =
                    std::chrono::duration_cast<std::chrono::milliseconds>(HighPriorityQueue.GetNextPollTime() - time);
                if (pollLimit <= std::chrono::milliseconds(0)) {
                    pollLimit = std::chrono::milliseconds(1);
                }
            }
            while (LowPriorityQueue.HasReadyItems(time) && accumulator(LowPriorityQueue.GetTop().Data, pollLimit)) {
                LowPriorityQueue.Pop();
            }
        }
    }

private:
    TQueue LowPriorityQueue;
    TQueue HighPriorityQueue;
    std::chrono::microseconds ForcedLowPriorityInterval;
    std::chrono::steady_clock::time_point ForcedLowPriorityCallTime = std::chrono::steady_clock::time_point::min();

    // Flag is used to prevent priority inversion in case of very long low priority polls
    bool LastSelectedWasFromLowPriorityQueue = true;
};
