#include <gtest/gtest.h>
#include "metrics.h"

TEST(TMetricsTest, BusLoad)
{
    Metrics::TBusLoadMetric m;

    auto t = std::chrono::steady_clock::now();

    // No requests
    ASSERT_TRUE(m.GetBusLoad(t).empty());

    // Unfinished request
    m.StartPoll({"test"}, t);
    t += std::chrono::seconds(6);
    auto bl = m.GetBusLoad(t);
    ASSERT_EQ(bl.size(), 1);
    ASSERT_EQ(bl.begin()->first.Device, "test");
    ASSERT_DOUBLE_EQ(bl.begin()->second.Minute, 1);
    ASSERT_DOUBLE_EQ(bl.begin()->second.FifteenMinutes, 1);

    // Second request
    m.StartPoll({"test2"}, t);
    t += std::chrono::seconds(6);
    bl = m.GetBusLoad(t);
    ASSERT_EQ(bl.size(), 2);
    ASSERT_DOUBLE_EQ(bl[{"test"}].Minute, 0.5);
    ASSERT_DOUBLE_EQ(bl[{"test"}].FifteenMinutes, 0.5);
    ASSERT_DOUBLE_EQ(bl[{"test2"}].Minute, 0.5);
    ASSERT_DOUBLE_EQ(bl[{"test2"}].FifteenMinutes, 0.5);

    // 2 chunks
    m.StartPoll({"test3"}, t);
    t += std::chrono::seconds(60);
    bl = m.GetBusLoad(t);
    ASSERT_EQ(bl.size(), 3);
    ASSERT_DOUBLE_EQ(bl[{"test"}].Minute, 6.0 / 72.0);
    ASSERT_DOUBLE_EQ(bl[{"test"}].FifteenMinutes, 6.0 / 72.0);
    ASSERT_DOUBLE_EQ(bl[{"test2"}].Minute, 6.0 / 72.0);
    ASSERT_DOUBLE_EQ(bl[{"test2"}].FifteenMinutes, 6.0 / 72.0);
    ASSERT_DOUBLE_EQ(bl[{"test3"}].Minute, 60.0 / 72.0);
    ASSERT_DOUBLE_EQ(bl[{"test3"}].FifteenMinutes, 60.0 / 72.0);

    // More than 15 minutes
    m.StartPoll({"test4"}, t);
    t += std::chrono::minutes(15);
    bl = m.GetBusLoad(t);
    ASSERT_EQ(bl.size(), 1);
    ASSERT_DOUBLE_EQ(bl[{"test4"}].Minute, 1);
    ASSERT_DOUBLE_EQ(bl[{"test4"}].FifteenMinutes, 1);

    m.StartPoll({"test"}, t);
    t += std::chrono::seconds(12);
    bl = m.GetBusLoad(t);
    ASSERT_EQ(bl.size(), 2);
    ASSERT_DOUBLE_EQ(bl[{"test"}].Minute, 12.0 / (60.0 + 12.0 + 12.0));
    ASSERT_DOUBLE_EQ(bl[{"test"}].FifteenMinutes, 12.0 / (14 * 60.0 + 12.0 + 12.0));
    ASSERT_DOUBLE_EQ(bl[{"test4"}].Minute, (60.0 + 12.0) / (60.0 + 12.0 + 12.0));
    ASSERT_DOUBLE_EQ(bl[{"test4"}].FifteenMinutes, (14 * 60.0 + 12.0) / (14 * 60.0 + 12.0 + 12.0));

    // Several controls
    Metrics::TPollItem pi("test3");
    for (size_t i = 0; i < 3; ++i) {
        pi.Controls.push_back("control" + std::to_string(i));
    }
    m.StartPoll(pi, t);
    t += std::chrono::minutes(14);
    bl = m.GetBusLoad(t);
    ASSERT_EQ(bl.size(), 3);
    ASSERT_DOUBLE_EQ(bl[{"test"}].Minute, 0.0);
    ASSERT_DOUBLE_EQ(bl[{"test"}].FifteenMinutes, 12.0 / (14 * 60.0 + 12.0 + 12.0));
    ASSERT_DOUBLE_EQ(bl[{"test4"}].Minute, 0.0);
    ASSERT_DOUBLE_EQ(bl[{"test4"}].FifteenMinutes, 12.0 / (14 * 60.0 + 12.0 + 12.0));
    ASSERT_DOUBLE_EQ(bl[pi].Minute, 1);
    ASSERT_DOUBLE_EQ(bl[pi].FifteenMinutes, (14 * 60.0) / (14 * 60.0 + 12.0 + 12.0));
}

TEST(TMetricsTest, PollInterval)
{
    // No requests
    {
        Metrics::TPollIntervalMetric p;
        auto h = p.GetHist();
        ASSERT_EQ(h.P50, std::chrono::milliseconds::zero());
        ASSERT_EQ(h.P95, std::chrono::milliseconds::zero());
    }

    // One request
    {
        Metrics::TPollIntervalMetric p;
        auto t = std::chrono::steady_clock::now();
        p.Poll(t);
        auto h = p.GetHist();
        ASSERT_EQ(h.P50, std::chrono::milliseconds::zero());
        ASSERT_EQ(h.P95, std::chrono::milliseconds::zero());
    }

    // 2 intervals
    {
        Metrics::TPollIntervalMetric p;
        auto t = std::chrono::steady_clock::now();
        p.Poll(t);
        for (size_t i = 1; i <= 2; ++i) {
            t += std::chrono::milliseconds(i);
            p.Poll(t);
        }
        auto h = p.GetHist();
        ASSERT_EQ(h.P50, std::chrono::milliseconds(1));
        ASSERT_EQ(h.P95, std::chrono::milliseconds(2));
    }

    // 3 intervals
    {
        Metrics::TPollIntervalMetric p;
        auto t = std::chrono::steady_clock::now();
        p.Poll(t);
        for (size_t i = 1; i <= 3; ++i) {
            t += std::chrono::milliseconds(i);
            p.Poll(t);
        }
        auto h = p.GetHist();
        ASSERT_EQ(h.P50, std::chrono::milliseconds(2));
        ASSERT_EQ(h.P95, std::chrono::milliseconds(3));
    }

    // Many intervals
    {
        Metrics::TPollIntervalMetric p;
        auto t = std::chrono::steady_clock::now();
        p.Poll(t);
        for (size_t i = 1; i <= 20; ++i) {
            t += std::chrono::milliseconds(i);
            p.Poll(t);
        }
        auto h = p.GetHist();
        ASSERT_EQ(h.P50, std::chrono::milliseconds(10));
        ASSERT_EQ(h.P95, std::chrono::milliseconds(19));
    }

    // One interval more than 2 minutes
    {
        Metrics::TPollIntervalMetric p;
        auto t = std::chrono::steady_clock::now();
        p.Poll(t);
        t += std::chrono::milliseconds(180);
        p.Poll(t);
        auto h = p.GetHist();
        ASSERT_EQ(h.P50, std::chrono::milliseconds(180));
        ASSERT_EQ(h.P95, std::chrono::milliseconds(180));
    }

    // Many intervals and last more than 2 minutes
    {
        Metrics::TPollIntervalMetric p;
        auto t = std::chrono::steady_clock::now();
        p.Poll(t);
        for (size_t i = 1; i <= 20; ++i) {
            t += std::chrono::milliseconds(i);
            p.Poll(t);
        }
        t += std::chrono::milliseconds(180000);
        p.Poll(t);
        auto h = p.GetHist();
        ASSERT_EQ(h.P50, std::chrono::milliseconds(180000));
        ASSERT_EQ(h.P95, std::chrono::milliseconds(180000));
    }
}