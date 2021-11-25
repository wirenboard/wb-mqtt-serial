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

    TItem GetNext()
    {
        TItem entry(Entries.top());
        Entries.pop();
        return entry;
    }

    const TItem& GetTop() const
    {
        return Entries.top();
    }

private:
    std::priority_queue<TItem> Entries;
};

template<class TEntry, class ComparePredicate, class GroupPredicate> class TScheduler
{
public:
    using TQueue = TPriorityQueueSchedule<TEntry, ComparePredicate>;
    using TItem = typename TQueue::TItem;

    TScheduler(std::chrono::microseconds forcedLowPriorityInterval = std::chrono::seconds(10))
        : ForcedLowPriorityInterval(forcedLowPriorityInterval)
    {
        if (ForcedLowPriorityInterval <= std::chrono::microseconds::zero()) {
            ForcedLowPriorityInterval = std::chrono::seconds(10);
        }
    }

    void AddEntry(TEntry entry, std::chrono::steady_clock::time_point nextPollTime, bool highPriority)
    {
        if (highPriority) {
            HighPriorityQueue.AddEntry(entry, nextPollTime);
        } else {
            LowPriorityQueue.AddEntry(entry, nextPollTime);
        }
    }

    std::chrono::steady_clock::time_point GetNextPollTime() const
    {
        return std::min(LowPriorityQueue.GetNextPollTime(), HighPriorityQueue.GetNextPollTime());
    }

    struct TItemsGroup
    {
        std::vector<TItem> Items;
        std::chrono::milliseconds PollLimit;
        bool IsHighPriority = false;
    };

    TItemsGroup GetNext(std::chrono::steady_clock::time_point time)
    {
        // TODO: Check priority inversion in case of very long low priority polls
        TItemsGroup res;
        if (LastLowPriorityCall + ForcedLowPriorityInterval < time && HasReadyItems(HighPriorityQueue, time)) {
            res.IsHighPriority = true;
            res.Items.push_back(HighPriorityQueue.GetNext());
            while (!HighPriorityQueue.IsEmpty() && CanGroup(HighPriorityQueue.GetTop(), res.Items.back())) {
                res.Items.push_back(HighPriorityQueue.GetNext());
            }
            return res;
        }
        if (HasReadyItems(LowPriorityQueue, time)) {
            LastLowPriorityCall = time;
            res.Items.push_back(LowPriorityQueue.GetNext());
            while (!LowPriorityQueue.IsEmpty() && CanGroup(LowPriorityQueue.GetTop(), res.Items.back())) {
                res.Items.push_back(LowPriorityQueue.GetNext());
            }
            if (HighPriorityQueue.IsEmpty()) {
                res.PollLimit = std::chrono::milliseconds::max();
            } else {
                res.PollLimit =
                    std::chrono::duration_cast<std::chrono::milliseconds>(HighPriorityQueue.GetNextPollTime() - time);
                if (res.PollLimit <= std::chrono::milliseconds(0)) {
                    res.PollLimit = std::chrono::milliseconds(1);
                }
            }
        }
        return res;
    }

private:
    TQueue LowPriorityQueue;
    TQueue HighPriorityQueue;
    std::chrono::microseconds ForcedLowPriorityInterval;
    std::chrono::steady_clock::time_point LastLowPriorityCall;

    bool HasReadyItems(const TQueue& q, std::chrono::steady_clock::time_point time) const
    {
        return !q.IsEmpty() && (q.GetNextPollTime() <= time);
    }

    bool CanGroup(const TItem& v1, const TItem& v2) const
    {
        if (v1.NextPollTime != v2.NextPollTime) {
            return false;
        }
        return GroupPredicate()(v1.Data, v2.Data);
    }
};
