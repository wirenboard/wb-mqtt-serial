#include "poll_plan.h"

TPreemptivePolicy::TPreemptivePolicy(std::chrono::milliseconds maxLowPriorityLag): MaxLowPriorityLag(maxLowPriorityLag)
{}

bool TPreemptivePolicy::ShouldSelectLowPriority(std::chrono::steady_clock::time_point time) const
{
    return (HighPriorityTime >= MaxLowPriorityLag);
}

void TPreemptivePolicy::SelectQueue(std::chrono::steady_clock::time_point time, TPriority priority)
{
    auto delta = std::chrono::duration_cast<std::chrono::milliseconds>(time - LastTime);
    LastTime = time;
    if (FirstTime) {
        FirstTime = false;
        return;
    }
    if (priority == TPriority::High) {
        HighPriorityTime += delta;
        if (HighPriorityTime > 2 * MaxLowPriorityLag) { // Prevent overflow
            HighPriorityTime = MaxLowPriorityLag;
        }
    } else {
        HighPriorityTime -= delta;
        if (HighPriorityTime < (-2 * MaxLowPriorityLag)) { // Prevent underflow
            HighPriorityTime = std::chrono::milliseconds::zero();
        }
    }
}

std::chrono::milliseconds TPreemptivePolicy::GetLowPriorityLag() const
{
    auto delta = (HighPriorityTime - MaxLowPriorityLag);
    if (delta > std::chrono::milliseconds::zero()) {
        return delta;
    }
    return std::chrono::milliseconds::zero();
}
