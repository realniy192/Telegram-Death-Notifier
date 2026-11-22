#include "Upload.hpp"
#include "SecureTokenSetting.hpp"

#include <Geode/Geode.hpp>
#include <Geode/ui/Notification.hpp>
#include <Geode/utils/async.hpp>
#include <Geode/utils/web.hpp>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <set>

using namespace geode::prelude;

namespace {
constexpr size_t kMaxUploadJobs = 12;
constexpr size_t kPhotoCaptionLimit = 1024; // Telegram sendPhoto caption limit
constexpr size_t kMaxQueuedPNGBytes = 24 * 1024 * 1024;

std::atomic<size_t> s_queuedPNGBytes = 0;

// Counts its own bytes for as long as any job still references it.
struct QueuedPNG {
    std::vector<uint8_t> data;
    explicit QueuedPNG(std::vector<uint8_t> bytes) : data(std::move(bytes)) { s_queuedPNGBytes += data.size(); }
    ~QueuedPNG() { s_queuedPNGBytes -= data.size(); }
};

struct UploadJob {
    std::shared_ptr<QueuedPNG const> png; // null: text message
    std::string text;
};

std::mutex s_uploadMutex;
std::deque<UploadJob> s_jobs;
bool s_uploadActive = false;

size_t freePNGBudget() {
    return kMaxQueuedPNGBytes - std::min(s_queuedPNGBytes.load(), kMaxQueuedPNGBytes);
}

void sendToTelegram();

void uploadFinished() {
    {
        std::lock_guard lock(s_uploadMutex);
        s_uploadActive = false;
    }
    sendToTelegram();
}

bool contains(std::string const& text, char const* part) {
    return text.find(part) != std::string::npos;
}

// Plain-language reason for a failed request. Never includes the token or URL.
std::string failureReason(web::WebResponse& res) {
    int code = res.code();
    if (code <= 0) return "no connection, or the upload timed out";
    if (code == 401 || code == 404) return "the bot token is invalid";
    if (code == 429) return "Telegram rate limit, wait a moment";
    std::string description;
    if (auto json = res.json()) {
        auto value = json.unwrap();
        if (value.contains("description")) description = value["description"].asString().unwrapOrDefault();
    }
    std::string lower = description;
    std::transform(lower.begin(), lower.end(), lower.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (contains(lower, "chat not found")) return "channel not found, check it in the Telegram tab";
    if (contains(lower, "not enough rights") || contains(lower, "not a member") || contains(lower, "kicked")) {
        return "the bot is not an admin of the channel";
    }
    if (contains(lower, "too big") || contains(lower, "too large")) return "the screenshot is too large";
    if (!description.empty()) {
        if (description.size() > 100) description = description.substr(0, 100) + "...";
        return description;
    }
    return fmt::format("HTTP error {}", code);
}

// One HTTP request at a time, always on the main thread.
void sendToTelegram() {
    UploadJob job;
    {
        std::lock_guard lock(s_uploadMutex);
        if (s_uploadActive || s_jobs.empty()) return;
        s_uploadActive = true;
        job = std::move(s_jobs.front());
        s_jobs.pop_front();
    }
    try {
        auto mod = Mod::get();
        auto token = std::string(utils::string::trim(tg::botToken()));
        auto chat = std::string(utils::string::trim(mod->getSettingValue<std::string>("chat-id")));
        if (token.starts_with("bot")) token = token.substr(3);
        if (token.empty() || chat.empty()) {
            tg::upload::notifyError(token.empty()
                ? "Telegram post skipped: add your bot token in the Telegram tab"
                : "Telegram post skipped: add your channel in the Telegram tab");
            uploadFinished();
            return;
        }
        auto timeout = std::clamp<int64_t>(mod->getSettingValue<int64_t>("upload-timeout"), 5, 120);
        web::MultipartForm form;
        form.param("chat_id", chat);
        char const* method = "sendMessage";
        if (job.png) {
            method = "sendPhoto";
            if (!job.text.empty()) form.param("caption", job.text);
            form.file("photo", job.png->data, "shot.png", "image/png");
        }
        else form.param("text", job.text);
        auto req = web::WebRequest();
        req.bodyMultipart(form);
        req.timeout(std::chrono::seconds(timeout));
        bool photo = job.png != nullptr;
        async::spawn(req.post(fmt::format("https://api.telegram.org/bot{}/{}", token, method)),
            [png = job.png, photo](web::WebResponse res) {
                try {
                    if (!res.ok()) {
                        // Do not log a URL/body that might contain a bot token.
                        log::warn("Telegram upload failed (HTTP {})", res.code());
                        tg::upload::notifyError(fmt::format("Telegram {} failed: {}",
                            photo ? "screenshot" : "message", failureReason(res)));
                    }
                    else {
                        auto json = res.json();
                        if (!json || !json.unwrap()["ok"].asBool().unwrapOr(false)) {
                            log::warn("Telegram rejected the message or returned an invalid response");
                            tg::upload::notifyError("Telegram rejected the post");
                        }
                    }
                }
                catch (std::exception const&) {
                    log::warn("Could not inspect Telegram response");
                }
                uploadFinished();
            });
    }
    catch (std::exception const&) {
        log::warn("Telegram upload could not be started; job dropped");
        tg::upload::notifyError("Telegram post failed: the upload could not start");
        uploadFinished();
    }
}

bool enqueueJobs(std::vector<UploadJob> jobs) {
    {
        std::lock_guard lock(s_uploadMutex);
        if (s_jobs.size() + jobs.size() > kMaxUploadJobs) {
            log::warn("Telegram upload queue is full; dropped");
            tg::upload::notifyError("Telegram post dropped: too many posts are waiting");
            return false;
        }
        for (auto& job : jobs) s_jobs.push_back(std::move(job));
    }
    geode::queueInMainThread([] { sendToTelegram(); });
    return true;
}

// ---------- deferred captions ----------
// A deferred shot is captured first and gets its caption later (rating panel).
constexpr size_t kMaxParked = 4;
std::mutex s_deferMutex;
std::set<uint64_t> s_waiting;                                     // still expect a caption
std::map<uint64_t, std::shared_ptr<QueuedPNG const>> s_parked;    // encoded, caption pending
std::map<uint64_t, tg::ShotText> s_resolved;                      // caption came before the PNG

void appendJobs(std::vector<UploadJob>& jobs, std::shared_ptr<QueuedPNG const> const& png, tg::ShotText text) {
    if (text.caption.size() <= kPhotoCaptionLimit) jobs.push_back({png, std::move(text.caption)});
    else {
        jobs.push_back({png, ""});
        jobs.push_back({nullptr, std::move(text.caption)});
    }
    // Follows its own photo, so the order in the channel is always right.
    if (!text.followUp.empty()) jobs.push_back({nullptr, std::move(text.followUp)});
}
}

void tg::upload::notifyError(std::string message) {
    geode::queueInMainThread([message = std::move(message)] {
        static std::string lastMessage;
        static std::chrono::steady_clock::time_point lastTime{};
        auto now = std::chrono::steady_clock::now();
        if (message == lastMessage && now - lastTime < std::chrono::seconds(5)) return;
        lastMessage = message;
        lastTime = now;
        Notification::create(message, NotificationIcon::Error)->show();
    });
}

void tg::upload::expectCaption(uint64_t ticket) {
    std::lock_guard lock(s_deferMutex);
    s_waiting.insert(ticket);
}

void tg::upload::forgetTicket(uint64_t ticket) {
    std::lock_guard lock(s_deferMutex);
    s_waiting.erase(ticket);
    s_parked.erase(ticket);
    s_resolved.erase(ticket);
}

// One PNG, one job per caption. Captions over the photo limit are sent as a
// clean photo followed by a text message, so long stats are never rejected.
void tg::upload::enqueuePhoto(std::vector<uint8_t> bytes, std::vector<ShotText> captions, bool noCaption) {
    if (bytes.size() > freePNGBudget()) {
        log::warn("Telegram upload queue is full; encoded frame dropped");
        notifyError("Telegram post dropped: too many screenshots are waiting");
        for (auto const& text : captions) if (text.ticket) forgetTicket(text.ticket);
        return;
    }
    auto png = std::make_shared<QueuedPNG const>(std::move(bytes));
    std::vector<UploadJob> jobs;
    if (noCaption || captions.empty()) {
        jobs.push_back({png, ""});
        for (auto const& text : captions) if (text.ticket) forgetTicket(text.ticket);
    }
    else for (auto& text : captions) {
        if (text.ticket) {
            std::lock_guard lock(s_deferMutex);
            if (auto it = s_resolved.find(text.ticket); it != s_resolved.end()) {
                auto resolved = std::move(it->second);
                s_resolved.erase(it);
                s_waiting.erase(text.ticket);
                appendJobs(jobs, png, std::move(resolved));
            }
            else if (s_waiting.contains(text.ticket) && s_parked.size() < kMaxParked) {
                s_parked[text.ticket] = png;
            }
            else s_waiting.erase(text.ticket);
            continue;
        }
        appendJobs(jobs, png, std::move(text));
    }
    if (!jobs.empty()) enqueueJobs(std::move(jobs));
}

void tg::upload::resolve(uint64_t ticket, ShotText text) {
    if (!ticket) return;
    text.ticket = 0;
    std::vector<UploadJob> jobs;
    {
        std::lock_guard lock(s_deferMutex);
        if (!s_waiting.contains(ticket)) return;
        auto parked = s_parked.find(ticket);
        if (parked == s_parked.end()) {
            s_resolved[ticket] = std::move(text);
            return;
        }
        appendJobs(jobs, parked->second, std::move(text));
        s_parked.erase(parked);
        s_waiting.erase(ticket);
    }
    enqueueJobs(std::move(jobs));
}

void tg::upload::sendText(std::string text) {
    if (text.empty()) return;
    std::vector<UploadJob> jobs;
    jobs.push_back({nullptr, std::move(text)});
    enqueueJobs(std::move(jobs));
}
