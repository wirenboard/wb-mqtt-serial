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

template<class TEntry, class TComparePredicate, class TQueueSelectionPolicy> class TScheduler
{
public:
    using TQueue = TPriorityQueueSchedule<TEntry, TComparePredicate>;
    using TItem = typename TQueue::TItem;

    TScheduler(const TQueueSelectionPolicy& queueSelectionPolicy): QueueSelectionPolicy(queueSelectionPolicy)
    {}

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

    template<class TAccumulator>
    void AccumulateNext(std::chrono::steady_clock::time_point time, TAccumulator& accumulator)
    {
        if (HighPriorityQueue.HasReadyItems(time) &&
            (!QueueSelectionPolicy.ShouldSelectLowPriority(time) || !LowPriorityQueue.HasReadyItems(time)))
        {
            while (HighPriorityQueue.HasReadyItems(time) &&
                   accumulator(HighPriorityQueue.GetTop().Data, std::chrono::milliseconds::max()))
            {
                HighPriorityQueue.Pop();
                QueueSelectionPolicy.SelectQueue(time, TPriority::High);
            }
            return;
        }
        if (LowPriorityQueue.HasReadyItems(time)) {
            const auto pollLimit = GetLowPriorityPollLimit(time);
            while (LowPriorityQueue.HasReadyItems(time) && accumulator(LowPriorityQueue.GetTop().Data, pollLimit)) {
                LowPriorityQueue.Pop();
                QueueSelectionPolicy.SelectQueue(time, TPriority::Low);
            }
        }
    }

private:
    TQueue LowPriorityQueue;
    TQueue HighPriorityQueue;
    TQueueSelectionPolicy QueueSelectionPolicy;

    std::chrono::milliseconds GetLowPriorityPollLimit(std::chrono::steady_clock::time_point time) const
    {
        if (HighPriorityQueue.IsEmpty()) {
            return std::chrono::milliseconds::max();
        }
        auto delta = std::chrono::duration_cast<std::chrono::milliseconds>(HighPriorityQueue.GetNextPollTime() - time);
        if (delta > std::chrono::milliseconds(0)) {
            return delta;
        }
        return QueueSelectionPolicy.GetLowPriorityLag();
    }
};

class TPreemptivePolicy
{
    std::chrono::milliseconds HighPriorityTime = std::chrono::milliseconds(0);
    std::chrono::milliseconds MaxLowPriorityLag;
    std::chrono::steady_clock::time_point LastTime;
    TPriority Priority;
    bool FirstTime = true;

public:
    TPreemptivePolicy(std::chrono::milliseconds maxLowPriorityLag);

    bool ShouldSelectLowPriority(std::chrono::steady_clock::time_point time) const;
    void SelectQueue(std::chrono::steady_clock::time_point time, TPriority priority);
    std::chrono::milliseconds GetLowPriorityLag() const;
};
