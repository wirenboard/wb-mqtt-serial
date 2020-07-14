#pragma once
#include <queue>
#include <chrono>
#include <memory>
#include <functional>

class TPollEntry {
public:
    virtual ~TPollEntry() {}
    virtual std::chrono::milliseconds PollInterval() const = 0;
};

typedef std::shared_ptr<TPollEntry> PPollEntry;

class TPollPlan {
public:
    typedef std::chrono::steady_clock::time_point TTimePoint;
    typedef std::function<TTimePoint()> TClockFunc;
    typedef std::function<void(const PPollEntry& entry)> TCallback;
    TPollPlan(TClockFunc clock_func = std::chrono::steady_clock::now);
    void AddEntry(const PPollEntry& entry);
    void ProcessPending(const TCallback& callback);
    bool PollIsDue();
    TTimePoint GetNextPollTimePoint();
    void Reset();

private:
    struct TQueueItem {
        TQueueItem(TTimePoint* current_time, std::chrono::milliseconds* avg_request_duration,
                   const PPollEntry& entry, int index):
            CurrentTime(current_time), AvgRequestDuration(avg_request_duration),
            Entry(entry), PollInterval(entry->PollInterval()), DueAt(*current_time), Index(index) {}
        TTimePoint* CurrentTime;
        std::chrono::milliseconds* AvgRequestDuration;
        PPollEntry Entry;
        std::chrono::milliseconds PollInterval,
            PollIntervalSum = std::chrono::milliseconds::zero(),
            AvgPollInterval = std::chrono::milliseconds::zero(),
            RequestDuration = std::chrono::milliseconds::zero();
        TTimePoint DueAt, LastPollAt;
        int Index;
        int PollCountAtLeast = 0;
        // NOTE: PollIntervalAveragingWindow of 1 is not supported!
        // (must alter TPollPlan::TQueueItem::Update() to support it)
        static const int PollIntervalAveragingWindow = 10;

        void Update(const std::chrono::milliseconds& new_interval,
                    const std::chrono::milliseconds& request_duration);
        int Importance() const;
    };
    typedef std::shared_ptr<TQueueItem> PQueueItem;
    struct LaterThan {
        bool operator () (const PQueueItem& a, const PQueueItem& b) const {
            // Take Index into account to try to make sorting
            // stable for more predictability (useful for testing)
            return a->DueAt > b->DueAt ||
                (a->DueAt == b->DueAt && a->Index > b->Index);
        }
    };
    struct LessImportantThan {
        bool operator () (const PQueueItem& a, const PQueueItem& b) const {
            // Take Index into account to try to make sorting
            // stable for more predictability (useful for testing)
            return a->Importance() < b->Importance() ||
                (a->Importance() == b->Importance() && a->Index > b->Index);
        }
    };

    TClockFunc ClockFunc;
    TTimePoint CurrentTime;
    std::chrono::milliseconds AvgRequestDuration = std::chrono::milliseconds::zero();
    std::priority_queue<PQueueItem, std::vector<PQueueItem>, LessImportantThan> PendingItems;
    std::priority_queue<PQueueItem, std::vector<PQueueItem>, LaterThan> Queue;
};

typedef std::shared_ptr<TPollPlan> PPollPlan;
