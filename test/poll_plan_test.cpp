#include "poll_plan.h"
#include "gtest/gtest.h"
#include <chrono>
#include <vector>

using namespace std::chrono_literals;

namespace
{
    struct Accumulator
    {
        std::vector<std::tuple<int, TItemAccumulationPolicy, std::chrono::milliseconds>> Data;

        bool operator()(int value, TItemAccumulationPolicy policy, std::chrono::milliseconds pollLimit)
        {
            Data.push_back({value, policy, pollLimit});
            return true;
        }
    };

    void TakeLowPriorityItems(TScheduler<int, std::less<int>>& scheduler,
                              std::chrono::steady_clock::time_point now,
                              Accumulator& accumulator,
                              std::chrono::milliseconds pollLimit,
                              TItemAccumulationPolicy firstItemPolicy,
                              int startingItemIndex,
                              size_t count)
    {
        accumulator.Data.clear();
        scheduler.AccumulateNext(now, accumulator, TItemSelectionPolicy::All);
        auto tpUs = std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch());
        EXPECT_EQ(accumulator.Data.size(), count) << tpUs.count();
        for (size_t i = 0; i < count; ++i) {
            EXPECT_EQ(std::get<0>(accumulator.Data[i]), startingItemIndex + i) << tpUs.count();
            EXPECT_EQ(std::get<1>(accumulator.Data[i]),
                      i == 0 ? firstItemPolicy : TItemAccumulationPolicy::AccordingToPollLimitTime)
                << tpUs.count();
            EXPECT_EQ(std::get<2>(accumulator.Data[i]), pollLimit) << tpUs.count();
        }
    }

    void TakeHighPriorityItem(TScheduler<int, std::less<int>>& scheduler,
                              std::chrono::steady_clock::time_point now,
                              Accumulator& accumulator,
                              int itemIndex)
    {
        accumulator.Data.clear();
        scheduler.AccumulateNext(now, accumulator, TItemSelectionPolicy::All);
        auto tpUs = std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch());
        EXPECT_EQ(accumulator.Data.size(), 1) << tpUs.count();
        EXPECT_EQ(std::get<0>(accumulator.Data[0]), itemIndex) << tpUs.count();
        EXPECT_EQ(std::get<1>(accumulator.Data[0]), TItemAccumulationPolicy::Force) << tpUs.count();
        EXPECT_EQ(std::get<2>(accumulator.Data[0]), std::chrono::milliseconds::max()) << tpUs.count();
    }

    void ScheduleItems(TScheduler<int, std::less<int>>& scheduler,
                       std::chrono::steady_clock::time_point now,
                       int startingItemIndex,
                       size_t count,
                       TPriority priority)
    {
        for (size_t i = 0; i < count; ++i) {
            scheduler.AddEntry(startingItemIndex + i, now, priority);
            now += 1us;
        }
    }

    void NothingReady(TScheduler<int, std::less<int>>& scheduler,
                      std::chrono::steady_clock::time_point now,
                      Accumulator& accumulator)
    {
        accumulator.Data.clear();
        scheduler.AccumulateNext(now, accumulator, TItemSelectionPolicy::All);
        auto tpUs = std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch());
        EXPECT_EQ(accumulator.Data.size(), 0) << tpUs.count();
    }
}

TEST(PollPlanTest, PriorityQueueSchedule)
{
    TPriorityQueueSchedule<int, std::less<int>> schedule;
    EXPECT_TRUE(schedule.IsEmpty());
    schedule.AddEntry(1, std::chrono::steady_clock::now());
    EXPECT_FALSE(schedule.IsEmpty());
    schedule.AddEntry(2, std::chrono::steady_clock::now() + 100ms);
    EXPECT_FALSE(schedule.IsEmpty());
    schedule.AddEntry(3, std::chrono::steady_clock::now());
    EXPECT_FALSE(schedule.IsEmpty());
    EXPECT_TRUE(schedule.HasReadyItems(std::chrono::steady_clock::now()));
    EXPECT_EQ(1, schedule.GetTop().Data);
    schedule.Pop();
    EXPECT_EQ(3, schedule.GetTop().Data);
    schedule.Pop();
    EXPECT_EQ(2, schedule.GetTop().Data);
    schedule.Pop();
    EXPECT_TRUE(schedule.IsEmpty());
    EXPECT_FALSE(schedule.HasReadyItems(std::chrono::steady_clock::now()));
}

TEST(PollPlanTest, RateLimiter)
{
    TRateLimiter limiter(2);
    limiter.NewItem(std::chrono::steady_clock::now());
    EXPECT_FALSE(limiter.IsOverLimit(std::chrono::steady_clock::now()));
    limiter.NewItem(std::chrono::steady_clock::now());
    EXPECT_FALSE(limiter.IsOverLimit(std::chrono::steady_clock::now()));
    limiter.NewItem(std::chrono::steady_clock::now());
    EXPECT_TRUE(limiter.IsOverLimit(std::chrono::steady_clock::now()));
}

TEST(PollPlanTest, OneHighPriority)
{
    TScheduler<int, std::less<int>> scheduler(1ms);
    auto init = std::chrono::steady_clock::time_point();
    scheduler.AddEntry(1, init, TPriority::High);
    Accumulator accumulator;

    auto now = init + 1ms;
    TakeHighPriorityItem(scheduler, now, accumulator, 1);
    EXPECT_EQ(scheduler.IsEmpty(), true);
    scheduler.UpdateSelectionTime(1ms, TPriority::High);

    for (size_t i = 0; i < 10; ++i) {
        scheduler.AddEntry(1, now + 1us, TPriority::High);
        accumulator.Data.clear();

        NothingReady(scheduler, now, accumulator);
        EXPECT_EQ(scheduler.IsEmpty(), false) << i;

        now += 1ms;
        TakeHighPriorityItem(scheduler, now, accumulator, 1);
        EXPECT_EQ(scheduler.IsEmpty(), true) << i;
        scheduler.UpdateSelectionTime(1ms, TPriority::High);
    }
}

TEST(PollPlanTest, LowPriority)
{
    TScheduler<int, std::less<int>> scheduler(1ms);
    auto init = std::chrono::steady_clock::time_point();
    ScheduleItems(scheduler, init, 1, 3, TPriority::Low);
    Accumulator accumulator;

    auto now = init + 1ms;
    TakeLowPriorityItems(scheduler,
                         now,
                         accumulator,
                         std::chrono::milliseconds::max(),
                         TItemAccumulationPolicy::Force,
                         1,
                         3);
    EXPECT_EQ(scheduler.IsEmpty(), true);
    scheduler.UpdateSelectionTime(1ms, TPriority::Low);

    for (size_t i = 0; i < 10; ++i) {
        ScheduleItems(scheduler, now + 1us, 1, 3, TPriority::Low);
        accumulator.Data.clear();

        NothingReady(scheduler, now, accumulator);
        EXPECT_EQ(scheduler.IsEmpty(), false) << i;

        now += 1ms;
        TakeLowPriorityItems(scheduler,
                             now,
                             accumulator,
                             std::chrono::milliseconds::max(),
                             TItemAccumulationPolicy::Force,
                             1,
                             3);
        EXPECT_EQ(scheduler.IsEmpty(), true) << i;
        scheduler.UpdateSelectionTime(1ms, TPriority::Low);
    }
}

TEST(PollPlanTest, LotsOfLowAndRareHigh)
{
    TScheduler<int, std::less<int>> scheduler(5ms);
    auto init = std::chrono::steady_clock::time_point();
    scheduler.AddEntry(1, init, TPriority::High);
    ScheduleItems(scheduler, init, 2, 3, TPriority::Low);
    Accumulator accumulator;

    auto now = init + 1ms;
    TakeHighPriorityItem(scheduler, now, accumulator, 1);
    scheduler.UpdateSelectionTime(1ms, TPriority::High);

    for (size_t i = 0; i < 10; ++i) {
        scheduler.AddEntry(1, now + 3ms, TPriority::High);

        TakeLowPriorityItems(scheduler, now, accumulator, 3ms, TItemAccumulationPolicy::AccordingToPollLimitTime, 2, 3);
        scheduler.UpdateSelectionTime(1ms, TPriority::Low);
        ScheduleItems(scheduler, now + 1ms, 2, 3, TPriority::Low);

        now += 2ms;
        TakeLowPriorityItems(scheduler, now, accumulator, 1ms, TItemAccumulationPolicy::AccordingToPollLimitTime, 2, 3);
        scheduler.UpdateSelectionTime(1ms, TPriority::Low);
        ScheduleItems(scheduler, now, 2, 3, TPriority::Low);

        now += 1ms;
        TakeHighPriorityItem(scheduler, now, accumulator, 1);
        scheduler.UpdateSelectionTime(1ms, TPriority::High);

        now += 1ms;
    }
}

TEST(PollPlanTest, LotsOfHighAndRareLow)
{
    // Check forcing low priority items

    TScheduler<int, std::less<int>> scheduler(5ms);
    auto init = std::chrono::steady_clock::time_point();
    scheduler.AddEntry(1, init, TPriority::High);
    ScheduleItems(scheduler, init, 2, 3, TPriority::Low);
    Accumulator accumulator;

    auto now = init + 1ms;

    for (size_t i = 0; i < 10; ++i) {
        // Threshold is not exceeded, read high priority
        TakeHighPriorityItem(scheduler, now, accumulator, 1);
        scheduler.UpdateSelectionTime(3ms, TPriority::High);
        scheduler.AddEntry(1, now + 2ms, TPriority::High);
        ASSERT_EQ(scheduler.GetTotalTime(), 3ms);

        // Free time after high priority, read low
        TakeLowPriorityItems(scheduler, now, accumulator, 2ms, TItemAccumulationPolicy::AccordingToPollLimitTime, 2, 3);
        scheduler.UpdateSelectionTime(2ms, TPriority::Low);
        ScheduleItems(scheduler, now, 2, 3, TPriority::Low);
        ASSERT_EQ(scheduler.GetTotalTime(), 1ms);

        // Pass low priority, read high
        now += 2ms;
        TakeHighPriorityItem(scheduler, now, accumulator, 1);
        scheduler.UpdateSelectionTime(3ms, TPriority::High);
        scheduler.AddEntry(1, now + 2ms, TPriority::High);
        ASSERT_EQ(scheduler.GetTotalTime(), 4ms);

        // Again pass low priority, read high
        now += 2ms;
        TakeHighPriorityItem(scheduler, now, accumulator, 1);
        scheduler.UpdateSelectionTime(3ms, TPriority::High);
        scheduler.AddEntry(1, now + 2ms, TPriority::High);
        ASSERT_EQ(scheduler.GetTotalTime(), 7ms);

        // Force low priority
        now += 2ms;
        TakeLowPriorityItems(scheduler, now, accumulator, 2ms, TItemAccumulationPolicy::Force, 2, 3);
        scheduler.UpdateSelectionTime(7ms, TPriority::Low);
        ScheduleItems(scheduler, now, 2, 3, TPriority::Low);
        ASSERT_EQ(scheduler.GetTotalTime(), 0ms);

        now += 1ms;
    }
}
