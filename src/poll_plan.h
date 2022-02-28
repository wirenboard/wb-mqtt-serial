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

    TScheduler(std::chrono::milliseconds maxLowPriorityLag): MaxLowPriorityLag(maxLowPriorityLag)
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

    std::chrono::steady_clock::time_point GetNextHighPriorityPollTime() const
    {
        return HighPriorityQueue.GetNextPollTime();
    }

    template<class TAccumulator>
    void AccumulateNext(std::chrono::steady_clock::time_point time, TAccumulator& accumulator)
    {
        UpdateSelectionTime(time);
        if (HighPriorityQueue.HasReadyItems(time) &&
            (!ShouldSelectLowPriority(time) || !LowPriorityQueue.HasReadyItems(time))) {
            SelectedPriority = TPriority::High;
            while (HighPriorityQueue.HasReadyItems(time) &&
                   accumulator(HighPriorityQueue.GetTop().Data, true, std::chrono::milliseconds::max()))
            {
                HighPriorityQueue.Pop();
            }
        } else {
            if (LowPriorityQueue.HasReadyItems(time)) {
                const auto pollLimit = GetLowPriorityPollLimit(time);
                bool force = ShouldSelectLowPriority(time) || HighPriorityQueue.IsEmpty();
                bool firstItem = true;
                // Set maximum allowed poll limit to first low priority item,
                // if it is selected to balance load.
                // Following low priority items should be selected only
                // if they poll time is not more than low priority items lag.
                while (LowPriorityQueue.HasReadyItems(time) &&
                       accumulator(LowPriorityQueue.GetTop().Data, force && firstItem, pollLimit))
                {
                    LowPriorityQueue.Pop();
                    firstItem = false;
                }
                if (!firstItem) {
                    SelectedPriority = TPriority::Low;
                }
            }
        }
    }

private:
    TQueue LowPriorityQueue;
    TQueue HighPriorityQueue;
    std::chrono::milliseconds HighPriorityTime = std::chrono::milliseconds(0);
    std::chrono::milliseconds MaxLowPriorityLag;
    std::chrono::steady_clock::time_point LastQueueSelectionTime;
    TPriority SelectedPriority;

    std::chrono::milliseconds GetLowPriorityPollLimit(std::chrono::steady_clock::time_point time) const
    {
        if (HighPriorityQueue.IsEmpty()) {
            return std::chrono::milliseconds::max();
        }
        auto delta = std::chrono::duration_cast<std::chrono::milliseconds>(HighPriorityQueue.GetNextPollTime() - time);
        if (delta > std::chrono::milliseconds(0)) {
            return delta;
        }
        return GetLowPriorityLag();
    }

    bool ShouldSelectLowPriority(std::chrono::steady_clock::time_point time) const
    {
        return (HighPriorityTime >= MaxLowPriorityLag);
    }

    void UpdateSelectionTime(std::chrono::steady_clock::time_point time)
    {
        if (LastQueueSelectionTime != std::chrono::steady_clock::time_point()) {
            auto delta = std::chrono::duration_cast<std::chrono::milliseconds>(time - LastQueueSelectionTime);
            if (SelectedPriority == TPriority::High) {
                HighPriorityTime += delta;
                if (HighPriorityTime > 10 * MaxLowPriorityLag) { // Prevent overflow
                    HighPriorityTime = MaxLowPriorityLag;
                }
            } else {
                HighPriorityTime -= delta;
                if (HighPriorityTime < (-10 * MaxLowPriorityLag)) { // Prevent underflow
                    HighPriorityTime = std::chrono::milliseconds::zero();
                }
            }
        }
        LastQueueSelectionTime = time;
    }

    std::chrono::milliseconds GetLowPriorityLag() const
    {
        auto delta = (HighPriorityTime - MaxLowPriorityLag);
        if (delta > std::chrono::milliseconds::zero()) {
            return delta;
        }
        return std::chrono::milliseconds::zero();
    }
};
