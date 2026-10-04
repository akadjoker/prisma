#pragma once

#include "platform.h"

#include <stdio.h>
#include <time.h>

namespace zenapp
{

inline double monotonicSeconds()
{
    timespec value;
    clock_gettime(CLOCK_MONOTONIC, &value);
    return static_cast<double>(value.tv_sec) + static_cast<double>(value.tv_nsec) * 1e-9;
}

class FrameStats
{
public:
    enum
    {
        kMaxPhases = 6
    };

    FrameStats(const char* title, const char* const* names, int phaseCount)
        : title_(title), names_(names), phaseCount_(phaseCount < kMaxPhases ? phaseCount : kMaxPhases)
    {
    }

    void begin()
    {
        frameStart_ = monotonicSeconds();
        last_ = frameStart_;
        if (windowStart_ == 0.0) windowStart_ = frameStart_;
    }

    void phase(int index)
    {
        const double now = monotonicSeconds();
        const double milliseconds = (now - last_) * 1000.0;
        last_ = now;
        if (index < 0 || index >= phaseCount_) return;
        sum_[index] += milliseconds;
        if (milliseconds > worst_[index]) worst_[index] = milliseconds;
    }

    void end()
    {
        const double now = monotonicSeconds();
        const double milliseconds = (now - frameStart_) * 1000.0;
        totalSum_ += milliseconds;
        if (milliseconds > totalWorst_) totalWorst_ = milliseconds;
        ++frames_;
        if (milliseconds > 25.0) ++slow_;
        if (now - windowStart_ < 1.0) return;

        char line[512];
        int length = snprintf(line, sizeof(line), "%s: %.1f fps, frame avg %.1f ms worst %.1f ms, %d over 25 ms |",
                title_, static_cast<double>(frames_) / (now - windowStart_), totalSum_ / frames_,
                totalWorst_, slow_);
        for (int i = 0; i < phaseCount_ && length > 0 && length < static_cast<int>(sizeof(line)); ++i)
            length += snprintf(line + length, sizeof(line) - static_cast<size_t>(length),
                    " %s %.1f/%.1f", names_[i], sum_[i] / frames_, worst_[i]);
        log_error("%s", line);

        windowStart_ = now;
        frames_ = 0;
        slow_ = 0;
        totalSum_ = 0.0;
        totalWorst_ = 0.0;
        for (int i = 0; i < kMaxPhases; ++i)
        {
            sum_[i] = 0.0;
            worst_[i] = 0.0;
        }
    }

private:
    const char* title_;
    const char* const* names_;
    int phaseCount_;
    double frameStart_ = 0.0;
    double last_ = 0.0;
    double windowStart_ = 0.0;
    int frames_ = 0;
    int slow_ = 0;
    double totalSum_ = 0.0;
    double totalWorst_ = 0.0;
    double sum_[kMaxPhases] = {};
    double worst_[kMaxPhases] = {};
};

} // namespace zenapp
