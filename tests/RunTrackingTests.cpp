#include "../src/RunTracking.hpp"

#include <cstdlib>
#include <iostream>
#include <limits>

static int checks = 0;

static void check(bool passed, char const* description) {
    ++checks;
    if (!passed) {
        std::cerr << "FAIL: " << description << '\n';
        std::exit(1);
    }
}

int main() {
    tg::RunFilters step { true, false, 95, true, 40, true };
    tg::RunTracking tracker;
    auto qualifies = [&](double start, double end, bool decimals = false) {
        tracker.restart(start);
        return tg::shouldNotify(*tracker.finish(end, decimals), step, 99, false, false);
    };

    check(qualifies(2, 42), "2 -> 42 meets 40 exactly, independent of best/old filter");
    check(qualifies(10, 55), "10 -> 55 exceeds 40");
    check(!qualifies(10, 49), "10 -> 49 below 40");
    check(!qualifies(90, 10), "backward progress never wraps");
    check(qualifies(2.9, 42.1), "whole endpoints are truncated before subtraction");
    check(!qualifies(2.9, 42.1, true), "decimal endpoints measure 39.20");
    check(qualifies(2.125, 42.125, true), "hundredths equality avoids floating subtraction errors");
    check(!qualifies(4.25, 44.24, true), "one hundredth below threshold fails");

    tracker.restart(0);
    // Checkpoint creation/removal has no tracking API and does not restart an attempt.
    check(tracker.m_currentRunStartPercent == 0, "placed checkpoint leaves clean start at zero");
    auto cleanPractice = tracker.finish(54, false);
    check(cleanPractice && cleanPractice->start == 0 && cleanPractice->distance() == 54,
          "uninterrupted practice run remains 0 -> 54");
    check(tg::shouldNotify(*cleanPractice, step, 99, true, false), "practice step passes");
    check(!tracker.finish(60, false), "repeat collision / dual player emits no second result");
    check(!tracker.finish(100, false), "noclip completion after first collision emits no result");

    tracker.restart(4.257, true);
    check(tracker.m_currentRunStartPercent == 4.257, "checkpoint stores raw restored percent");
    auto checkpointRun = tracker.finish(54.259, true);
    check(checkpointRun && checkpointRun->start == 426 && checkpointRun->end == 5426,
          "checkpoint endpoints quantized to hundredths");
    check(checkpointRun->fromCheckpoint && checkpointRun->distance() == 5000,
          "checkpoint segment is fifty percent");
    step.minRunProgress = 50;
    check(tg::shouldNotify(*checkpointRun, step, 99, true, false), "4.26 -> 54.26 meets 50");
    check(!tracker.finish(90, true), "checkpoint run is consumed once");
    tracker.restart(4, true);
    check(tracker.finish(54, false)->distance() == 50, "respawn re-arms tracking");

    step.minRunProgress = 40;
    tracker.restart(61);
    auto shortWin = tracker.finish(100, false);
    check(!tg::shouldNotify(*shortWin, step, 0, true, true), "completion respects step");
    tracker.restart(60);
    auto win = tracker.finish(100, false);
    check(tg::shouldNotify(*win, step, 100, false, true), "clean 60 -> 100 accepted");
    check(!tracker.finish(100, false), "completion is consumed once");
    tracker.restart(60);
    std::optional<std::string> pendingWin;
    auto complete = [&] {
        auto result = tracker.finish(100, false);
        if (result) {
            pendingWin.reset();
            if (tg::shouldNotify(*result, step, 0, false, true)) pendingWin = "60-100";
        }
    };
    complete();
    complete();
    check(pendingWin == "60-100", "duplicate completion preserves pending end-screen caption");
    step.levelEnabled = false;
    check(!tg::shouldNotify(*win, step, 0, false, true), "master switch disables all filters");
    step.levelEnabled = true;
    step.stepEnabled = false;
    check(!tg::shouldNotify(*win, step, 0, false, true), "both filters off disables notifications");
    step.stepEnabled = true;
    step.minRunProgress = 0;
    check(qualifies(10, 10), "zero step adds no minimum distance restriction");

    tg::RunFilters legacy { true, true, 40, false, 0, true };
    tracker.restart(0);
    auto death = tracker.finish(42, false);
    check(!tg::shouldNotify(*death, legacy, 50, false, false), "legacy best filter preserved");
    check(tg::shouldNotify(*death, legacy, 50, true, false), "practice not compared to normal best");
    check(tg::shouldNotify(*death, legacy, 41, false, false), "legacy new best accepted");
    legacy.stepEnabled = true;
    legacy.minRunProgress = 90;
    check(tg::shouldNotify(*death, legacy, 41, false, false), "legacy passes even if step fails");
    legacy.legacyMinPercent = 90;
    legacy.minRunProgress = 40;
    check(tg::shouldNotify(*death, legacy, 99, false, false), "step passes even if legacy fails");

    check(tg::percentUnits(45.329, false) == 45, "default vanilla integer truncation");
    check(tg::percentUnits(45.324, true) == 4532, "round fractional percent to hundredths");
    check(tg::percentUnits(45.326, true) == 4533, "round up at hundredths");
    check(tg::percentUnits(-4, true) == 0, "negative percent bounded");
    check(tg::percentUnits(104, true) == 10000, "percent above 100 bounded");
    check(tg::percentUnits(std::numeric_limits<double>::quiet_NaN(), true) == 0,
          "invalid percentage handled");
    check(tg::formatPercent(45, false) == "45", "whole-number text");
    check(tg::formatPercent(4532, true) == "45.32", "two-decimal text");
    check(tg::formatPercent(4502, true) == "45.02", "leading fractional zero");
    check(tg::formatPercent(4530, true) == "45.3", "up to two decimals");
    check(tg::formatPercent(10000, true) == "100", "whole completion percent");
    std::cout << "PASS: " << checks << " run-tracking checks\n";
}
