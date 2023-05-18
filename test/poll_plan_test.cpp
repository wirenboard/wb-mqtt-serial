#include "poll_plan.h"
#include "gtest/gtest.h"
#include <chrono>
#include <vector>

using namespace std::chrono_literals;

struct Accumulator
{
    std::vector<std::tuple<int, TItemAccumulationPolicy, std::chrono::milliseconds>> Data;

    bool operator()(int value, TItemAccumulationPolicy policy, std::chrono::milliseconds pollLimit)
    {
        Data.push_back({value, policy, pollLimit});
        return true;
    }
};

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

TEST(PollPlanTest, Scheduler)
{
    TScheduler<int, std::less<int>> scheduler(1ms, 2);
    auto init = std::chrono::steady_clock::now();
    // Emplace 2 high priority items and 3 low priority items
    scheduler.AddEntry(1, init, TPriority::High);
    scheduler.AddEntry(2, init + 1us, TPriority::Low);
    scheduler.AddEntry(3, init + 2us, TPriority::High);
    scheduler.AddEntry(4, init + 3us, TPriority::Low);
    scheduler.AddEntry(5, init + 4us, TPriority::Low);
    scheduler.AddEntry(6, init + 5us, TPriority::Low);
    Accumulator accumulator;
    auto now = init + 1ms;
    // First 2 high priority items should be taken, no throttling
    EXPECT_EQ(scheduler.AccumulateNext(now, accumulator), TThrottlingState::NoThrottling);
    EXPECT_EQ(accumulator.Data.size(), 2);
    EXPECT_EQ(std::get<0>(accumulator.Data[0]), 1);
    EXPECT_EQ(std::get<1>(accumulator.Data[0]), TItemAccumulationPolicy::Force);
    EXPECT_EQ(std::get<2>(accumulator.Data[0]), std::chrono::milliseconds::max());
    EXPECT_EQ(std::get<0>(accumulator.Data[1]), 3);
    EXPECT_EQ(std::get<1>(accumulator.Data[1]), TItemAccumulationPolicy::AccordingToPollLimitTime);
    EXPECT_EQ(std::get<2>(accumulator.Data[1]), std::chrono::milliseconds::max());
    accumulator.Data.clear();
    scheduler.UpdateSelectionTime(1ms, TPriority::High);
    // Emplace another 1 high priority item
    scheduler.AddEntry(7, init + 6us, TPriority::High);
    // Next 3 low priority items should be taken, throttling (rate limit is 2 per 1s)
    EXPECT_EQ(scheduler.AccumulateNext(now, accumulator), TThrottlingState::LowPriorityRateLimit);
    EXPECT_EQ(accumulator.Data.size(), 3);
    EXPECT_EQ(std::get<0>(accumulator.Data[0]), 2);
    EXPECT_EQ(std::get<1>(accumulator.Data[0]), TItemAccumulationPolicy::Force);
    EXPECT_EQ(std::get<2>(accumulator.Data[0]), std::chrono::milliseconds::zero());
    EXPECT_EQ(std::get<0>(accumulator.Data[1]), 4);
    EXPECT_EQ(std::get<1>(accumulator.Data[1]), TItemAccumulationPolicy::AccordingToPollLimitTime);
    EXPECT_EQ(std::get<2>(accumulator.Data[1]), std::chrono::milliseconds::zero());
    EXPECT_EQ(std::get<0>(accumulator.Data[2]), 5);
    EXPECT_EQ(std::get<1>(accumulator.Data[2]), TItemAccumulationPolicy::AccordingToPollLimitTime);
    EXPECT_EQ(std::get<2>(accumulator.Data[2]), std::chrono::milliseconds::zero());
    accumulator.Data.clear();
    scheduler.UpdateSelectionTime(1ms, TPriority::Low);
    // Wait for 1s (rate limit is 2 per 1s)
    sleep(1);
    // Next 1 high priority item should be taken, no throttling
    now += 1s;
    EXPECT_EQ(scheduler.AccumulateNext(now, accumulator), TThrottlingState::NoThrottling);
    EXPECT_EQ(accumulator.Data.size(), 1);
    EXPECT_EQ(std::get<0>(accumulator.Data[0]), 7);
    EXPECT_EQ(std::get<1>(accumulator.Data[0]), TItemAccumulationPolicy::Force);
    EXPECT_EQ(std::get<2>(accumulator.Data[0]), std::chrono::milliseconds::max());
    accumulator.Data.clear();
    scheduler.UpdateSelectionTime(1ms, TPriority::High);
    // Next 1 low priority item should be taken, no throttling
    now += 1ms;
    EXPECT_EQ(scheduler.AccumulateNext(now, accumulator), TThrottlingState::NoThrottling);
    EXPECT_EQ(accumulator.Data.size(), 1);
    EXPECT_EQ(std::get<0>(accumulator.Data[0]), 6);
    EXPECT_EQ(std::get<1>(accumulator.Data[0]), TItemAccumulationPolicy::Force);
    EXPECT_EQ(std::get<2>(accumulator.Data[0]), std::chrono::milliseconds::max());
}
