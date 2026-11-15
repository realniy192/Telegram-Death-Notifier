#pragma once

#include <string>

namespace tg {

struct ShotRequest {
    std::string caption; // empty: photo without caption
    int delayMs = 0;     // wait before capturing (death screenshots)
};

// Queue a screenshot for a completed frame. Safe to call from any thread.
// A bounded queue drops excess requests rather than retaining unlimited images.
void queueScreenshot(ShotRequest request);
inline void queueScreenshot(std::string caption) { queueScreenshot(ShotRequest{std::move(caption)}); }

// The level restarted or closed: delayed shots are sent with the frame
// captured at the moment they were queued instead of a later screen.
void flushDelayedScreenshots();

// Text-only message (long captions, daily summary).
void sendTelegramText(std::string text);

}
