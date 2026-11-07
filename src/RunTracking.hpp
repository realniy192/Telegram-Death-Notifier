#pragma once

#include <algorithm>
#include <cmath>
#include <optional>
#include <string>

namespace tg {

inline double validPercent(double percent) {
    return std::isfinite(percent) ? std::clamp(percent, 0.0, 100.0) : 0.0;
}

// Quantize both endpoints before subtraction, so the filter matches the message.
inline int percentUnits(double percent, bool decimals) {
    percent = validPercent(percent);
    return decimals ? static_cast<int>(std::round(percent * 100.0))
                    : static_cast<int>(percent);
}

inline std::string formatPercent(int units, bool decimals) {
    if (!decimals) return std::to_string(units);
    auto text = std::to_string(units / 100);
    int fraction = units % 100;
    if (fraction == 0) return text;
    text += '.';
    if (fraction < 10) text += '0';
    text += std::to_string(fraction);
    if (text.back() == '0') text.pop_back();
    return text;
}

struct RunResult {
    int start;
    int end;
    bool decimals;
    bool fromCheckpoint;

    int distance() const { return std::max(0, end - start); }
    int scale() const { return decimals ? 100 : 1; }
};

struct RunFilters {
    bool levelEnabled = false;
    bool legacyEnabled = false;
    int legacyMinPercent = 0;
    bool stepEnabled = false;
    int minRunProgress = 0;
    bool onlyBest = false;
};

inline bool shouldNotify(RunResult const& run, RunFilters const& filters,
                         int previousBest, bool practice, bool completed) {
    if (!filters.levelEnabled) return false;
    bool legacy = filters.legacyEnabled &&
        run.end >= std::clamp(filters.legacyMinPercent, 0, 100) * run.scale();
    if (!completed && !practice && !run.fromCheckpoint && run.start == 0 &&
        filters.onlyBest && run.end <= previousBest * run.scale()) {
        legacy = false;
    }
    bool step = filters.stepEnabled &&
        run.distance() >= std::clamp(filters.minRunProgress, 0, 100) * run.scale();
    return legacy || step;
}

struct RunTracking {
    double m_currentRunStartPercent = 0.0;
    bool ended = false;
    bool fromCheckpoint = false;

    void restart(double percent, bool checkpoint = false) {
        m_currentRunStartPercent = validPercent(percent);
        ended = false;
        fromCheckpoint = checkpoint;
    }

    std::optional<RunResult> finish(double percent, bool decimals) {
        if (ended) return std::nullopt;
        ended = true;
        return RunResult { percentUnits(m_currentRunStartPercent, decimals),
                           percentUnits(percent, decimals), decimals, fromCheckpoint };
    }
};

}
