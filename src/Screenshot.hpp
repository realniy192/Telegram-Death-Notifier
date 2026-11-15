#pragma once

#include <cstdint>
#include <functional>
#include <string>

namespace tg {

struct ShotText {
    std::string caption;  // empty: photo without caption
    std::string followUp; // optional text message sent right after the photo
    uint64_t ticket = 0;  // internal: deferred shot waiting for its caption
};

struct ShotRequest {
    std::string caption;  // empty: photo without caption
    int delayMs = 0;      // wait before capturing (death screenshots)
    std::string followUp; // optional text message sent right after the photo
    // Capture now, caption later via resolveDeferredScreenshot (rating panel).
    bool deferred = false;
    // Main thread, right after the frame was captured (or the capture failed),
    // so UI shown from here is never in the screenshot.
    std::function<void()> onCaptured;
};

// Queue a screenshot for a completed frame. Safe to call from any thread.
// Returns the shot's ticket, 0 when it was dropped (bounded queue).
uint64_t queueScreenshot(ShotRequest request);
inline uint64_t queueScreenshot(std::string caption) { return queueScreenshot(ShotRequest{std::move(caption)}); }

// Final caption for a deferred shot. Unknown or failed tickets are ignored.
void resolveDeferredScreenshot(uint64_t ticket, ShotText text);

// The level restarted or closed: delayed shots are sent with the frame
// captured at the moment they were queued instead of a later screen.
void flushDelayedScreenshots();

// Text-only message (long captions, summaries).
void sendTelegramText(std::string text);

}
