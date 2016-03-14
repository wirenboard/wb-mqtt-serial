#include "poll_plan.h"

#undef POLL_PLAN_DEBUG

#ifdef POLL_PLAN_DEBUG
#include <iostream>
#endif

void TPollPlan::TQueueItem::Update(const std::chrono::milliseconds& new_interval,
                                   const std::chrono::milliseconds& request_duration)
{
    // http://www.daycounter.com/LabBook/Moving-Average.phtml
    PollCountAtLeast++;
    if (PollCountAtLeast > 1) {
        PollIntervalSum += new_interval;
        if (PollCountAtLeast > PollIntervalAveragingWindow) {
            PollIntervalSum -= AvgPollInterval;
            PollCountAtLeast = PollIntervalAveragingWindow;
        }
        AvgPollInterval = PollIntervalSum / PollCountAtLeast;
    }
    RequestDuration = request_duration;

#ifdef POLL_PLAN_DEBUG
    std::cout << "Poll interval " << PollInterval.count() << ": CurrentTime is " <<
        std::chrono::duration_cast<std::chrono::milliseconds>(CurrentTime->time_since_epoch()).count() <<
        ". Was scheduled at " <<
        std::chrono::duration_cast<std::chrono::milliseconds>(DueAt.time_since_epoch()).count() <<
        ". Rescheduling to " <<
        std::chrono::duration_cast<std::chrono::milliseconds>((*CurrentTime + PollInterval).time_since_epoch()).count() <<
        std::endl;
#endif
    DueAt = *CurrentTime + PollInterval;
}

int TPollPlan::TQueueItem::Importance() const
{
    long long v = std::chrono::duration_cast<std::chrono::milliseconds>(*CurrentTime - DueAt).count() * 1000;
    if (PollInterval != std::chrono::milliseconds::zero()) {
        // the longer is poll interval, the lower is priority
        v /= PollInterval.count();
        if (AvgPollInterval != std::chrono::milliseconds::zero()) {
            // If average poll interval is considerably greater than the requested poll
            // interval, give the entry more priority to compensate for this problem
            v *= AvgPollInterval.count();
            v /= PollInterval.count();
        }
    }
    if (RequestDuration != std::chrono::milliseconds::zero() &&
        *AvgRequestDuration != std::chrono::milliseconds::zero()) {
        // requests that require more time have lower priority
        v *= AvgRequestDuration->count();
        v /= RequestDuration.count();
    }
    return v;
}

TPollPlan::TPollPlan(TClockFunc clock_func): ClockFunc(clock_func)
{
    CurrentTime = ClockFunc();
}

void TPollPlan::AddEntry(const PPollEntry& entry)
{
    Queue.emplace(std::make_shared<TQueueItem>(&CurrentTime, &AvgRequestDuration, entry, Queue.size()));
}

void TPollPlan::ProcessPending(const TCallback& callback)
{
    CurrentTime = ClockFunc();
    while (!PendingItems.empty())
        PendingItems.pop();
#ifdef POLL_PLAN_DEBUG
    if (!Queue.empty())
        std::cout << "top due at " <<
            std::chrono::duration_cast<std::chrono::milliseconds>(Queue.top()->DueAt.time_since_epoch()).count() <<
            "; now is " <<
            std::chrono::duration_cast<std::chrono::milliseconds>(CurrentTime.time_since_epoch()).count() <<
            std::endl;
#endif
    while (!Queue.empty() && Queue.top()->DueAt <= CurrentTime) {
        PendingItems.push(Queue.top());
        Queue.pop();
    }
    int n = 0;
    std::chrono::milliseconds avg_duration = std::chrono::milliseconds::zero();
    while (!PendingItems.empty()) {
        auto item = PendingItems.top();
        auto start = ClockFunc();
        callback(item->Entry);
        auto request_duration = std::chrono::duration_cast<std::chrono::milliseconds>(ClockFunc() - start);
        item->Update(item->PollCountAtLeast > 1 ?
                     std::chrono::duration_cast<std::chrono::milliseconds>(start - item->LastPollAt) :
                     std::chrono::milliseconds(0),
                     request_duration);
        avg_duration += request_duration;
        ++n;
        Queue.push(item);
        PendingItems.pop();
    }

    if (n > 0)
        AvgRequestDuration = std::chrono::milliseconds(avg_duration.count() / n);
}

bool TPollPlan::PollIsDue()
{
    CurrentTime = ClockFunc();
    return Queue.top()->DueAt <= CurrentTime;
}

TPollPlan::TTimePoint TPollPlan::GetNextPollTimePoint()
{
    if (Queue.empty())
        return TTimePoint::max();
    if (PollIsDue())
        return TTimePoint(CurrentTime);
    return Queue.top()->DueAt;
}

void TPollPlan::Reset()
{
    AvgRequestDuration = std::chrono::milliseconds::zero();
    while (!PendingItems.empty())
        PendingItems.pop();
    while (!Queue.empty())
        Queue.pop();
}
