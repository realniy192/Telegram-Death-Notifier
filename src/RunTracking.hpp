#pragma once

#include <algorithm>
#include <cmath>
#include <optional>
#include <string>

namespace tg {

enum class RunMode { StartPositions, MinimumProgress };

inline double validPercent(double percent) {
    return std::isfinite(percent) ? std::clamp(percent, 0.0, 100.0) : 0.0;
}

inline int percentScale(int decimalPlaces) {
    int scale = 1;
    for (int i = 0; i < std::clamp(decimalPlaces, 0, 4); ++i) scale *= 10;
    return scale;
}

// Quantize both endpoints before subtraction, so the filter matches the message.
inline int percentUnits(double percent, int decimalPlaces) {
    percent = validPercent(percent);
    decimalPlaces = std::clamp(decimalPlaces, 0, 4);
    return decimalPlaces > 0 ? static_cast<int>(std::round(percent * percentScale(decimalPlaces)))
                             : static_cast<int>(percent);
}

inline std::string formatPercent(int units, int decimalPlaces) {
    decimalPlaces = std::clamp(decimalPlaces, 0, 4);
    if (!decimalPlaces) return std::to_string(units);
    int scale = percentScale(decimalPlaces);
    auto fraction = std::to_string(units % scale);
    return std::to_string(units / scale) + '.' +
        std::string(decimalPlaces - fraction.size(), '0') + fraction;
}

struct RunResult {
    int start;
    int end;
    int decimalPlaces;
    bool fromCheckpoint;

    int distance() const { return std::max(0, end - start); }
    int scale() const { return percentScale(decimalPlaces); }
};

struct RunFilters {
    bool levelEnabled = false;
    RunMode mode = RunMode::StartPositions;
    bool startposEnabled = false;
    int minEndPercent = 0;
    int minRunProgress = 0;
    bool onlyBest = false;
};

inline bool shouldNotify(RunResult const& run, RunFilters const& filters,
                         int previousBest, bool practice, bool completed) {
    if (!filters.levelEnabled) return false;
    if (filters.mode == RunMode::MinimumProgress) {
        return run.distance() >= std::clamp(filters.minRunProgress, 0, 100) * run.scale();
    }
    if (!filters.startposEnabled ||
        run.end < std::clamp(filters.minEndPercent, 0, 100) * run.scale()) return false;
    return completed || practice || run.fromCheckpoint || run.start != 0 ||
        !filters.onlyBest || run.end > previousBest * run.scale();
}

struct RunTracking {
    double m_currentRunStartPercent = 0.0;
    bool ended = false;
    bool fromCheckpoint = false;
    std::optional<double> lastCollisionStep;

    void restart(double percent, bool checkpoint = false) {
        m_currentRunStartPercent = validPercent(percent);
        ended = false;
        fromCheckpoint = checkpoint;
        lastCollisionStep.reset();
    }

    std::optional<RunResult> finish(double percent, int decimalPlaces) {
        if (ended) return std::nullopt;
        ended = true;
        decimalPlaces = std::clamp(decimalPlaces, 0, 4);
        return RunResult { percentUnits(m_currentRunStartPercent, decimalPlaces),
                           percentUnits(percent, decimalPlaces), decimalPlaces, fromCheckpoint };
    }

    std::optional<RunResult> collide(double percent, int decimalPlaces, double gameStep) {
        if (lastCollisionStep && *lastCollisionStep == gameStep) return std::nullopt;
        auto result = finish(percent, decimalPlaces);
        if (result) lastCollisionStep = gameStep;
        return result;
    }

    // A suppressed noclip death begins the next clean segment, but keeps the
    // same-step collision guard so dual/repeated callbacks are not new runs.
    void continueAfterCollision(double percent) {
        m_currentRunStartPercent = validPercent(percent);
        ended = false;
    }
};

}
