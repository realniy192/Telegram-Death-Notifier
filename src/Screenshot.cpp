#include "Screenshot.hpp"
#include "ShotCommon.hpp"
#include "Upload.hpp"

#include <Geode/Geode.hpp>
#include <Geode/modify/CCDirector.hpp>
#include <Geode/modify/CCEGLView.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

// GL capture lives in ShotCapture.cpp / ShotCaptureAndroid.cpp and the
// Telegram upload queue in Upload.cpp. This file captures, encodes and queues.

using namespace geode::prelude;
using tg::shot::Frame;

namespace {
constexpr size_t kMaxCaptions = 8;
constexpr size_t kMaxCaptionBytes = 4096;
constexpr size_t kMaxPNGBytes = 10 * 1024 * 1024;
std::mutex s_queueMutex;
std::atomic_bool s_busy = false;

// Held only through capture/encoding. Upload backpressure is independent, so
// an in-flight HTTP request does not suppress the next attempt's screenshot.
struct BatchLease {
    ~BatchLease() { s_busy.store(false); }
};

struct TemporaryPNG {
    std::filesystem::path path;
    ~TemporaryPNG() {
        std::error_code ec;
        std::filesystem::remove(path, ec);
        if (ec) log::warn("Could not remove temporary screenshot PNG");
    }
};

// Read on the main thread only; the encoder thread never touches settings.
struct EncodeOptions {
    float skew = 0.f;
    bool noCaption = false;
};

EncodeOptions currentEncodeOptions() {
    auto mod = Mod::get();
    EncodeOptions options;
    if (mod->getSettingValue<bool>("skewed-screenshot")) {
        options.skew = std::clamp((int)mod->getSettingValue<int64_t>("skew-strength"), 10, 400) / 100.f;
    }
    options.noCaption = mod->getSettingValue<bool>("no-caption");
    return options;
}

// Joke mode: recreates the old row-pitch bug. Each row is rotated left a bit
// more than the row above it, so the picture shears diagonally and wraps
// around the edge. Whole pixels move, so colors stay intact.
void applySkew(Frame& frame, float pixelsPerRow) {
    size_t row = size_t(frame.width) * 4;
    for (int y = 0; y < frame.height; ++y) {
        auto shift = size_t(std::lround(y * pixelsPerRow)) % size_t(frame.width);
        if (!shift) continue;
        auto line = frame.pixels.begin() + size_t(y) * row;
        std::rotate(line, line + shift * 4, line + row);
    }
}

void encodeFailed(std::vector<tg::ShotText> const& captions, char const* reason) {
    log::warn("{}", reason);
    for (auto const& text : captions) if (text.ticket) tg::upload::forgetTicket(text.ticket);
    tg::upload::notifyError("Screenshot failed, nothing was posted");
}

void encodeAndSend(Frame frame, std::vector<tg::ShotText> captions, std::shared_ptr<BatchLease> lease,
                   EncodeOptions options) {
    static uint64_t counter = 0; // render thread only
    auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    auto path = Mod::get()->getSaveDir() / fmt::format("shot-{}-{}.png", stamp, counter++);
    // CCObject construction/destruction touch Cocos bookkeeping. Only image
    // pixel initialization and file encoding run on the worker, never GL or a
    // scene/node. Use no autorelease objects on that thread.
    auto image = std::shared_ptr<CCImage>(new CCImage(), [](CCImage* p) {
        geode::queueInMainThread([p] { p->release(); });
    });
    std::thread([frame = std::move(frame), path = std::move(path), image = std::move(image),
        captions = std::move(captions), lease = std::move(lease), options]() mutable {
        try {
            TemporaryPNG temporary{path};
            size_t row = size_t(frame.width) * 4;
            for (int y = 0; y < frame.height / 2; ++y) {
                auto first = frame.pixels.begin() + size_t(y) * row;
                auto opposite = frame.pixels.begin() + size_t(frame.height - 1 - y) * row;
                std::swap_ranges(first, first + row, opposite);
            }
            if (options.skew > 0.f) applySkew(frame, options.skew);
            bool ok = image->initWithImageData(frame.pixels.data(), static_cast<int>(frame.pixels.size()),
                CCImage::kFmtRawData, frame.width, frame.height, 8);
            // Drop the readback copy before PNG encoding allocates its buffers.
            std::vector<uint8_t>().swap(frame.pixels);
            if (!ok || !image->saveToFile(path.string().c_str(), true)) {
                encodeFailed(captions, "Screenshot PNG encoding failed");
                return;
            }
            std::error_code ec;
            auto size = std::filesystem::file_size(path, ec);
            if (ec || !size || size > kMaxPNGBytes) {
                encodeFailed(captions, "Screenshot PNG missing or exceeds the 10 MiB upload limit");
                return;
            }
            auto data = file::readBinary(path);
            if (!data) {
                encodeFailed(captions, "Could not read encoded screenshot PNG");
                return;
            }
            // Reserve queue memory before releasing the encoding slot.
            tg::upload::enqueuePhoto(std::move(data).unwrap(), std::move(captions), options.noCaption);
            image.reset();
            lease.reset();
        }
        catch (std::exception const&) {
            log::warn("Screenshot encoding failed; frame dropped");
            tg::upload::notifyError("Screenshot failed, nothing was posted");
        }
    }).detach();
}

// ======================= pending screenshots =======================
constexpr size_t kMaxFallbackFrames = 2;

struct PendingShot {
    uint64_t id = 0;
    tg::ShotText text;
    std::chrono::steady_clock::time_point due;
    bool wantFallback = false; // delayed: also keep the frame of the moment it was queued
    bool flushed = false;      // level restarted before the delay passed
    std::optional<Frame> fallback;
    std::function<void()> onCaptured;
};

std::vector<PendingShot> s_pending; // guarded by s_queueMutex
uint64_t s_nextShotId = 0;

void encode(Frame frame, std::vector<tg::ShotText> captions) {
    s_busy.store(true);
    std::shared_ptr<BatchLease> lease;
    try { lease = std::make_shared<BatchLease>(); }
    catch (...) { s_busy.store(false); throw; }
    encodeAndSend(std::move(frame), std::move(captions), std::move(lease), currentEncodeOptions());
}

void storeFallbacks(std::vector<uint64_t> const& ids, Frame const& frame) {
    std::lock_guard lock(s_queueMutex);
    size_t stored = std::count_if(s_pending.begin(), s_pending.end(),
        [](PendingShot const& shot) { return shot.fallback.has_value(); });
    for (auto& shot : s_pending) {
        if (stored >= kMaxFallbackFrames) break;
        if (shot.fallback || std::find(ids.begin(), ids.end(), shot.id) == ids.end()) continue;
        shot.fallback = frame;
        ++stored;
    }
}

void runCallbacks(std::vector<std::function<void()>>& callbacks) {
    for (auto& callback : callbacks) {
        if (callback) geode::queueInMainThread(std::move(callback));
    }
}

void capturePending() {
    // The previous frame is still encoding: keep the queue and retry next frame.
    if (s_busy.load()) return;
    auto now = std::chrono::steady_clock::now();
    std::optional<Frame> restartFrame;
    std::vector<tg::ShotText> captions;
    std::vector<uint64_t> fallbackIds;
    std::vector<std::function<void()>> callbacks;
    {
        std::lock_guard lock(s_queueMutex);
        if (s_pending.empty()) return;
        auto flushed = std::find_if(s_pending.begin(), s_pending.end(),
            [](PendingShot const& shot) { return shot.flushed && shot.fallback; });
        if (flushed != s_pending.end()) {
            restartFrame = std::move(flushed->fallback);
            captions.push_back(std::move(flushed->text));
            callbacks.push_back(std::move(flushed->onCaptured));
            s_pending.erase(flushed);
        }
        else {
            for (auto it = s_pending.begin(); it != s_pending.end();) {
                if (it->due <= now) {
                    captions.push_back(std::move(it->text));
                    callbacks.push_back(std::move(it->onCaptured));
                    it = s_pending.erase(it);
                    continue;
                }
                if (it->wantFallback && !it->fallback) {
                    fallbackIds.push_back(it->id);
                    it->wantFallback = false; // one attempt per shot
                }
                ++it;
            }
        }
    }
    std::vector<uint64_t> tickets;
    for (auto const& text : captions) if (text.ticket) tickets.push_back(text.ticket);
    bool captured = false;
    try {
        if (restartFrame) {
            encode(std::move(*restartFrame), std::move(captions));
            captured = true;
        }
        else if (!captions.empty() || !fallbackIds.empty()) {
            Frame frame;
            if (tg::shot::captureFrame(frame)) {
                captured = true;
                if (!fallbackIds.empty()) storeFallbacks(fallbackIds, frame);
                if (!captions.empty()) encode(std::move(frame), std::move(captions));
            }
            // Only report a lost post, not a failed fallback-only capture.
            else if (!captions.empty()) tg::upload::notifyError("Screenshot failed, nothing was posted");
        }
    }
    catch (std::exception const&) {
        captured = false;
        log::warn("Screenshot capture could not be allocated or scheduled; frame dropped");
        tg::upload::notifyError("Screenshot failed, nothing was posted");
    }
    // A failed deferred shot will never get a photo: drop its caption slot.
    if (!captured) for (auto ticket : tickets) tg::upload::forgetTicket(ticket);
    runCallbacks(callbacks);
}
} // namespace

uint64_t tg::queueScreenshot(ShotRequest request) {
    if (request.caption.size() > kMaxCaptionBytes || request.followUp.size() > kMaxCaptionBytes) {
        log::warn("Screenshot caption exceeds queue size limit; dropped");
        upload::notifyError("Post dropped: the message is too long");
        return 0;
    }
    int delay = std::clamp(request.delayMs, 0, 5000);
    std::lock_guard lock(s_queueMutex);
    if (s_pending.size() >= kMaxCaptions) {
        log::warn("Screenshot queue is full; dropped");
        upload::notifyError("Post dropped: too many screenshots are waiting");
        return 0;
    }
    try {
        PendingShot shot;
        shot.id = ++s_nextShotId;
        shot.text = {std::move(request.caption), std::move(request.followUp), request.deferred ? shot.id : 0};
        shot.due = std::chrono::steady_clock::now() + std::chrono::milliseconds(delay);
        shot.wantFallback = delay > 0;
        shot.onCaptured = std::move(request.onCaptured);
        auto id = shot.id;
        if (request.deferred) upload::expectCaption(id);
        s_pending.push_back(std::move(shot));
        return id;
    }
    catch (std::exception const&) {
        log::warn("Screenshot could not be queued; dropped");
        return 0;
    }
}

void tg::resolveDeferredScreenshot(uint64_t ticket, ShotText text) {
    upload::resolve(ticket, std::move(text));
}

void tg::flushDelayedScreenshots() {
    std::lock_guard lock(s_queueMutex);
    auto now = std::chrono::steady_clock::now();
    for (auto& shot : s_pending) {
        if (shot.due <= now) continue;
        if (shot.fallback) shot.flushed = true;
        // Death and restart in the same frame: no earlier frame exists, so
        // capture as soon as possible instead of losing the message.
        else shot.due = now;
    }
}

void tg::sendTelegramText(std::string text) {
    if (text.empty()) return;
    if (text.size() > kMaxCaptionBytes) text.resize(kMaxCaptionBytes);
    upload::sendText(std::move(text));
}

#ifdef GEODE_IS_ANDROID
// Android's actual EGL swap happens in Java after native drawScene returns.
class $modify(TGScreenshotDirector, CCDirector) {
    void drawScene() {
        CCDirector::drawScene();
        capturePending();
    }
};
#else
class $modify(TGScreenshotView, CCEGLView) {
    void swapBuffers() {
        capturePending();
        CCEGLView::swapBuffers();
    }
};
#endif
