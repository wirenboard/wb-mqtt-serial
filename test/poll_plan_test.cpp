#include <cmath>
#include <string>
#include <vector>
#include <gtest/gtest.h>

#include "poll_plan.h"

namespace {
    double IntervalDiffTolerance = 0.05;
};

struct TFakePollEntry: public TPollEntry {
    TFakePollEntry(const std::string& name, int poll_interval): Name(name), Interval(poll_interval) {}
    std::chrono::milliseconds PollInterval() const { return std::chrono::milliseconds(Interval); }
    std::string Name;
    int Interval;
    int NumPolls = 0;
};

typedef std::shared_ptr<TFakePollEntry> PFakePollEntry;

class TPollPlanTest: public ::testing::Test {
protected:
    void SetUp();
    void Elapse(int ms);
    void Verify();
    void VerifyPollPeriod(int count, int loop_interval, int request_time, bool expect_poll_to_be_due = false);

    TPollPlan::TTimePoint StartTime = TPollPlan::TTimePoint(std::chrono::milliseconds(0)), CurrentTime;
    PPollPlan Plan;
    std::vector<PFakePollEntry> Entries;
};

void TPollPlanTest::SetUp()
{
    Plan = std::make_shared<TPollPlan>([this]() { return CurrentTime; });
    CurrentTime = StartTime;
    Entries = {
        std::make_shared<TFakePollEntry>("33ms", 33),
        std::make_shared<TFakePollEntry>("100ms-1", 100),
        std::make_shared<TFakePollEntry>("100ms-2", 100),
        std::make_shared<TFakePollEntry>("300ms", 300),
        std::make_shared<TFakePollEntry>("1s", 1000)
    };
    for (auto entry: Entries) {
        Plan->AddEntry(entry);
    }
}

void TPollPlanTest::Elapse(int ms)
{
    CurrentTime += std::chrono::milliseconds(ms);
}

void TPollPlanTest::Verify()
{
    double elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(CurrentTime - StartTime).count();
    for (auto entry: Entries) {
        if (!entry->NumPolls) {
            ADD_FAILURE() << "Entry wasn't polled: " << entry->Name;
            continue;
        }
        double actualPollInterval = elapsedMs / entry->NumPolls,
            diff = std::abs(actualPollInterval - entry->Interval),
            maxDiff = entry->Interval * IntervalDiffTolerance;
        if (diff > maxDiff)
            ADD_FAILURE() << entry->Name << " poll interval is " <<
                actualPollInterval << " which is off by " <<
                diff << " milliseconds. Max diff is " << maxDiff <<
                " milliseconds.";
    }
}

void TPollPlanTest::VerifyPollPeriod(int count, int loop_interval, int request_time,
                                     bool expect_poll_to_be_due) {
    for (int i = 0; i < count; ++i) {
        if (expect_poll_to_be_due) {
            ASSERT_TRUE(Plan->PollIsDue());
            ASSERT_EQ(CurrentTime, Plan->GetNextPollTimePoint());
        }
        if (loop_interval >= 0)
            Elapse(loop_interval);
        else {
            auto next_poll_tp = Plan->GetNextPollTimePoint();
            ASSERT_LE(CurrentTime, next_poll_tp);
            CurrentTime = next_poll_tp;
        }
        Plan->ProcessPending([this, request_time](const PPollEntry& entry) {
                std::dynamic_pointer_cast<TFakePollEntry>(entry)->NumPolls++;
                Elapse(request_time);
            });
    }
    Verify();
}

TEST_F(TPollPlanTest, PollPeriod1)
{
    VerifyPollPeriod(10000, 1, 1);
}

TEST_F(TPollPlanTest, PollPeriod2)
{
    VerifyPollPeriod(10000, 3, 1);
}

TEST_F(TPollPlanTest, PollPeriod3)
{
    VerifyPollPeriod(10000, 1, 10);
}

TEST_F(TPollPlanTest, PollPeriodWithProperWaitTime)
{
    VerifyPollPeriod(10000, -1, 10);
}

TEST_F(TPollPlanTest, EntryWithZeroPollPeriod)
{
    PFakePollEntry entry_with_no_period = std::make_shared<TFakePollEntry>("no period", 0);
    Plan->AddEntry(entry_with_no_period);
    VerifyPollPeriod(10000, -1, 1, true);
    ASSERT_EQ(10000, entry_with_no_period->NumPolls); // polled upon every iteration
}
