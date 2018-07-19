#pragma once

#include <chrono>
#include <functional>

namespace utils
{
    template <typename TimeUnit>
    class TScopeTime
    {
        using TCallback = std::function<void(const TimeUnit &)>;
        using sc = std::chrono::steady_clock;

        TCallback       Callback;
        sc::time_point  Start;

        void Finalize() const
        {
            Callback(std::chrono::duration_cast<TimeUnit>(sc::now() - Start));
        }

    public:
        TScopeTime(TCallback callback)
            : Callback(std::move(callback))
            , Start(sc::now())
        {}

        ~TScopeTime()
        {
            if (std::uncaught_exception()) {
                try {
                    Finalize();
                } catch (...) {
                    // pass
                }
            } else {
                Finalize();
            }
        }
    };
}

#ifdef WB_MEASURE_PERFORMANCE
 #define PERF_LOG_SCOPE_DURATION(time_unit, time_unit_suffix) \
 auto _func = __PRETTY_FUNCTION__; \
 utils::TScopeTime<time_unit> _st([_func](const time_unit & duration){ \
     std::cout << "[performance] " << _func << ": " \
               << duration.count() << time_unit_suffix << std::endl; \
 });
#else
 #define PERF_LOG_SCOPE_DURATION(time_unit, time_unit_suffix)
#endif

#define PERF_LOG_SCOPE_DURATION_US PERF_LOG_SCOPE_DURATION(std::chrono::microseconds, "us")
#define PERF_LOG_SCOPE_DURATION_MS PERF_LOG_SCOPE_DURATION(std::chrono::milliseconds, "ms")
