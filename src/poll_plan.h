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

private:
    std::priority_queue<TItem> Entries;
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

template<class TEntry, class TComparePredicate> class TScheduler
{
public:
    using TQueue = TPriorityQueueSchedule<TEntry, TComparePredicate>;
    using TItem = typename TQueue::TItem;

    TScheduler(std::chrono::milliseconds maxLowPriorityLag): MaxLowPriorityLag(maxLowPriorityLag)
    {}

    void AddEntry(TEntry entry, std::chrono::steady_clock::time_point deadline, TPriority priority)
    {
        if (priority == TPriority::Low) {
            LowPriorityQueue.AddEntry(entry, deadline);
        } else {
            HighPriorityQueue.AddEntry(entry, deadline);
        }
    }

    std::chrono::steady_clock::time_point GetDeadline() const
    {
        return std::min(HighPriorityQueue.GetDeadline(), LowPriorityQueue.GetDeadline());
    }

    std::chrono::steady_clock::time_point GetHighPriorityDeadline() const
    {
        return HighPriorityQueue.GetDeadline();
    }

    /**
     * @brief Selects entries from queue with deadline less or equal to currentTime
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
    void AccumulateNext(std::chrono::steady_clock::time_point currentTime, TAccumulator& accumulator)
    {
        UpdateSelectionTime(currentTime);
        if (HighPriorityQueue.HasReadyItems(currentTime) &&
            (!ShouldSelectLowPriority() || !LowPriorityQueue.HasReadyItems(currentTime)))
        {
            SelectedPriority = TPriority::High;
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
                bool force = ShouldSelectLowPriority() || HighPriorityQueue.IsEmpty();
                bool firstItem = true;
                // Set maximum allowed poll limit to first low priority item,
                // if it is selected to balance load.
                // Following low priority items should be selected only
                // if they poll time is not more than low priority items lag.
                while (LowPriorityQueue.HasReadyItems(currentTime) &&
                       accumulator(LowPriorityQueue.GetTop().Data,
                                   (force && firstItem) ? TItemAccumulationPolicy::Force
                                                        : TItemAccumulationPolicy::AccordingToPollLimitTime,
                                   pollLimit))
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

    std::chrono::milliseconds GetLowPriorityPollLimit(std::chrono::steady_clock::time_point currentTime) const
    {
        if (HighPriorityQueue.IsEmpty()) {
            return std::chrono::milliseconds::max();
        }
        auto delta =
            std::chrono::duration_cast<std::chrono::milliseconds>(HighPriorityQueue.GetDeadline() - currentTime);
        if (delta > std::chrono::milliseconds(0)) {
            return delta;
        }
        return GetLowPriorityLag();
    }

    bool ShouldSelectLowPriority() const
    {
        return (HighPriorityTime >= MaxLowPriorityLag);
    }

    void UpdateSelectionTime(std::chrono::steady_clock::time_point currentTime)
    {
        if (LastQueueSelectionTime != std::chrono::steady_clock::time_point()) {
            auto delta = std::chrono::duration_cast<std::chrono::milliseconds>(currentTime - LastQueueSelectionTime);
            auto OldHighPriorityTime = HighPriorityTime;
            if (SelectedPriority == TPriority::High) {
                HighPriorityTime += delta;
                if (HighPriorityTime < OldHighPriorityTime) { // Prevent overflow
                    HighPriorityTime = MaxLowPriorityLag;
                }
            } else {
                HighPriorityTime -= delta;
                if (HighPriorityTime > OldHighPriorityTime) { // Prevent underflow
                    HighPriorityTime = std::chrono::milliseconds::zero();
                }
            }
        }
        LastQueueSelectionTime = currentTime;
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
