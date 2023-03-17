#include "poll_plan.h"
#include "gtest/gtest.h"
#include <chrono>
#include <vector>

using namespace std::chrono_literals;

struct Accumulator
{
    std::vector<int> Data;

    bool operator()(int value, TItemAccumulationPolicy policy, std::chrono::milliseconds pollLimit)
    {
        Data.push_back(value);
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
    // Emplace 2 high priority items and 3 low priority items
    scheduler.AddEntry(1, std::chrono::steady_clock::now(), TPriority::High);
    scheduler.AddEntry(2, std::chrono::steady_clock::now(), TPriority::Low);
    scheduler.AddEntry(3, std::chrono::steady_clock::now(), TPriority::High);
    scheduler.AddEntry(4, std::chrono::steady_clock::now(), TPriority::Low);
    scheduler.AddEntry(5, std::chrono::steady_clock::now(), TPriority::Low);
    scheduler.AddEntry(6, std::chrono::steady_clock::now(), TPriority::Low);
    Accumulator accumulator;
    // First 2 high priority items should be taken, no throttling
    EXPECT_EQ(scheduler.AccumulateNext(std::chrono::steady_clock::now(), accumulator), TThrottlingState::NoThrottling);
    EXPECT_EQ(accumulator.Data.size(), 2);
    EXPECT_EQ(accumulator.Data[0], 1);
    EXPECT_EQ(accumulator.Data[1], 3);
    accumulator.Data.clear();
    // Next 3 low priority items should be taken, throttling (rate limit is 2 per 1s)
    EXPECT_EQ(scheduler.AccumulateNext(std::chrono::steady_clock::now(), accumulator),
              TThrottlingState::LowPriorityRateLimit);
    EXPECT_EQ(accumulator.Data.size(), 3);
    EXPECT_EQ(accumulator.Data[0], 2);
    EXPECT_EQ(accumulator.Data[1], 4);
    EXPECT_EQ(accumulator.Data[2], 5);
    accumulator.Data.clear();
    // Wait for 1s (rate limit is 2 per 1s)
    sleep(1);
    // Next 1 low priority item should be taken, no throttling
    EXPECT_EQ(scheduler.AccumulateNext(std::chrono::steady_clock::now(), accumulator), TThrottlingState::NoThrottling);
    EXPECT_EQ(accumulator.Data.size(), 1);
    EXPECT_EQ(accumulator.Data[0], 6);
}
