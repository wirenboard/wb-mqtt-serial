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

    void AddHighPriorityEntry(TEntry entry, std::chrono::steady_clock::time_point nextPollTime)
    {
        HighPriorityQueue.AddEntry(entry, nextPollTime);
    }

    void AddLowPriorityEntry(TEntry entry)
    {
        LowPriorityQueue.emplace(entry);
    }

    std::chrono::steady_clock::time_point GetNextPollTime() const
    {
        if (LowPriorityQueue.empty()) {
            return HighPriorityQueue.GetNextPollTime();
        }
        return std::chrono::steady_clock::now();
    }

    template<class TAccumulator> void GetNext(std::chrono::steady_clock::time_point time, TAccumulator& accumulator)
    {
        if (HighPriorityQueue.HasReadyItems(time) &&
            (LastSelectedWasFromLowPriorityQueue || (ForcedLowPriorityCallTime > time) || LowPriorityQueue.empty()))
        {
            while (HighPriorityQueue.HasReadyItems(time) &&
                   accumulator(HighPriorityQueue.GetTop().Data, std::chrono::milliseconds::max()))
            {
                HighPriorityQueue.Pop();
                LastSelectedWasFromLowPriorityQueue = false;
            }
            return;
        }
        if (!LowPriorityQueue.empty()) {
            ForcedLowPriorityCallTime = time + ForcedLowPriorityInterval;
            LastSelectedWasFromLowPriorityQueue = true;
            auto pollLimit = std::chrono::milliseconds::max();
            if (!HighPriorityQueue.IsEmpty()) {
                pollLimit =
                    std::chrono::duration_cast<std::chrono::milliseconds>(HighPriorityQueue.GetNextPollTime() - time);
                if (pollLimit <= std::chrono::milliseconds(0)) {
                    pollLimit = std::chrono::milliseconds(1);
                }
            }
            while (!LowPriorityQueue.empty() && accumulator(LowPriorityQueue.front(), pollLimit)) {
                LowPriorityQueue.pop();
            }
        }
    }

private:
    std::queue<TEntry> LowPriorityQueue;
    TQueue HighPriorityQueue;
    std::chrono::microseconds ForcedLowPriorityInterval;
    std::chrono::steady_clock::time_point ForcedLowPriorityCallTime = std::chrono::steady_clock::time_point::min();

    // Flag is used to prevent priority inversion in case of very long low priority polls
    bool LastSelectedWasFromLowPriorityQueue = true;
};
