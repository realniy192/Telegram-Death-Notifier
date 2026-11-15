#pragma once

#include "RunTracking.hpp"
#include <algorithm>
#include <string>
#include <vector>

namespace tg {

enum class StreakMode { Separate = 0, Replace = 1, Combine = 2 };

struct StreakEntry {
    int start = 0;      // percent units, same as RunResult
    int end = 0;
    int decimalPlaces = 0;
    bool from0 = false; // normal mode death or completion from 0%
    bool operator==(StreakEntry const& other) const {
        return start == other.start && end == other.end && from0 == other.from0;
    }
};

// Qualifying progresses in a row, with no plain death in between.
struct StreakTracker {
    static constexpr size_t kMaxEntries = 50;
    std::vector<StreakEntry> entries; // the most recent kMaxEntries
    size_t total = 0;                 // full streak length

    void reset() {
        entries.clear();
        total = 0;
    }

    size_t push(StreakEntry entry) {
        if (entries.size() >= kMaxEntries) entries.erase(entries.begin());
        entries.push_back(entry);
        return ++total;
    }

    bool allSame() const {
        return !entries.empty() && std::all_of(entries.begin(), entries.end(),
            [&](StreakEntry const& entry) { return entry == entries.front(); });
    }

    // "58-67" for a run, "58" (or "58%") for a progress from 0%.
    static std::string label(StreakEntry const& entry, bool percentSign) {
        auto end = formatPercent(entry.end, entry.decimalPlaces);
        if (entry.from0) return percentSign ? end + "%" : end;
        return formatPercent(entry.start, entry.decimalPlaces) + "-" + end;
    }

    // "24% and 25%" for two, "54-70, 54-73, 54-83" for more.
    std::string list() const {
        std::string out;
        for (size_t i = 0; i < entries.size(); ++i) {
            if (i) out += entries.size() == 2 ? " and " : ", ";
            out += label(entries[i], true);
        }
        return out;
    }
};

}
