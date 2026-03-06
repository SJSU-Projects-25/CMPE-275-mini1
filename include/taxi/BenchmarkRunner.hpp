#pragma once

#include <chrono>
#include <cmath>
#include <vector>
#include <functional>

namespace taxi {

struct RunStats {
    double avg_ms   = 0.0;
    double stddev_ms = 0.0;
    double min_ms   = 0.0;
    double max_ms   = 0.0;
    int    runs     = 0;
};

/**
 * @brief Times a callable over N runs and returns aggregate statistics.
 *
 * Usage:
 *   RunStats s = BenchmarkRunner::time_n([&]{ engine.search_by_fare(q); }, 10);
 */
class BenchmarkRunner {
public:
    template<typename F>
    static RunStats time_n(F&& func, int n) {
        if (n <= 0) return {};

        std::vector<double> times;
        times.reserve(n);

        for (int i = 0; i < n; ++i) {
            auto t0 = std::chrono::steady_clock::now();
            func();
            auto t1 = std::chrono::steady_clock::now();
            times.push_back(
                std::chrono::duration<double, std::milli>(t1 - t0).count());
        }

        RunStats s;
        s.runs  = n;
        s.min_ms = times[0];
        s.max_ms = times[0];
        double sum = 0.0;
        for (double t : times) {
            sum += t;
            if (t < s.min_ms) s.min_ms = t;
            if (t > s.max_ms) s.max_ms = t;
        }
        s.avg_ms = sum / n;

        double sq_sum = 0.0;
        for (double t : times) {
            double d = t - s.avg_ms;
            sq_sum += d * d;
        }
        s.stddev_ms = (n > 1) ? std::sqrt(sq_sum / (n - 1)) : 0.0;

        return s;
    }
};

} // namespace taxi
