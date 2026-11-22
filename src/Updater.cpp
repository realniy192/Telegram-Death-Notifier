#include "Updater.hpp"

#include <Geode/Geode.hpp>
#include <Geode/ui/Notification.hpp>
#include <Geode/ui/Popup.hpp>
#include <Geode/utils/async.hpp>
#include <Geode/utils/web.hpp>

#include <filesystem>
#include <optional>

using namespace geode::prelude;

namespace {
// Releases must attach the built .geode file (ideally named <mod id>.geode).
// Tags like "v1.3.0" and "v.1.3.0" both work.
constexpr char kLatestRelease[] = "https://api.github.com/repos/realniy192/Telegram-Death-Notifier/releases/latest";
bool s_busy = false;

void toast(std::string const& text, NotificationIcon icon) {
    Notification::create(text, icon)->show();
}

void alert(char const* title, std::string const& text) {
    FLAlertLayer::create(title, text, "OK")->show();
}

std::optional<VersionInfo> parseTag(std::string const& tag) {
    auto start = tag.find_first_of("0123456789");
    if (start == std::string::npos) return std::nullopt;
    auto parsed = VersionInfo::parse(tag.substr(start));
    if (!parsed) return std::nullopt;
    return parsed.unwrap();
}

std::string text(matjson::Value const& value, char const* key) {
    if (!value.contains(key)) return "";
    return value[key].asString().unwrapOrDefault();
}

// Download URL of the release's .geode asset; prefers "<mod id>.geode".
std::optional<std::string> findAsset(matjson::Value const& release) {
    if (!release.contains("assets") || !release["assets"].isArray()) return std::nullopt;
    auto wanted = std::string(Mod::get()->getID().view()) + ".geode";
    std::optional<std::string> fallback;
    for (auto const& asset : release["assets"]) {
        auto name = text(asset, "name");
        auto url = text(asset, "browser_download_url");
        if (url.empty() || !name.ends_with(".geode")) continue;
        if (name == wanted) return url;
        if (!fallback) fallback = url;
    }
    return fallback;
}

std::filesystem::path installPath() {
    auto path = Mod::get()->getPackagePath();
    if (!path.empty()) return path;
    return dirs::getModsDir() / (std::string(Mod::get()->getID().view()) + ".geode");
}

void install(std::vector<uint8_t> const& data, std::string const& version) {
    // A .geode file is a zip archive.
    if (data.size() < 4 || data[0] != 'P' || data[1] != 'K') {
        alert("Update failed", "The downloaded file is not a valid .geode file. Nothing was changed.");
        return;
    }
    auto target = installPath();
    auto temp = target;
    temp += ".download";
    if (auto written = file::writeBinary(temp, data); !written) {
        alert("Update failed", "Could not save the downloaded file. Nothing was changed.");
        return;
    }
    std::error_code ec;
    std::filesystem::rename(temp, target, ec);
    if (ec) {
        ec.clear();
        std::filesystem::copy_file(temp, target, std::filesystem::copy_options::overwrite_existing, ec);
        std::error_code ignored;
        std::filesystem::remove(temp, ignored);
        if (ec) {
            alert("Update failed", fmt::format("Could not replace the mod file ({}). "
                "Download the .geode file from GitHub and put it in your geode/mods folder.", ec.message()));
            return;
        }
    }
    createQuickPopup("Update installed",
        fmt::format("Version <cg>{}</c> is installed.\nRestart Geometry Dash to finish the update.", version),
        "Later", "Restart",
        [](FLAlertLayer*, bool restart) {
            if (restart) game::restart(true); // SDK 5.11.2 has no default argument
        });
}

void download(std::string const& url, std::string const& version) {
    if (s_busy) return;
    s_busy = true;
    toast(fmt::format("Downloading {}...", version), NotificationIcon::Loading);
    auto req = web::WebRequest();
    req.header("User-Agent", "tg-death-notifier-updater");
    req.timeout(std::chrono::seconds(180));
    async::spawn(req.get(url), [version](web::WebResponse res) {
        s_busy = false;
        if (!res.ok()) {
            alert("Update failed", res.code() <= 0
                ? std::string("No connection, or the download timed out. Nothing was changed.")
                : fmt::format("The download failed (HTTP {}). Nothing was changed.", res.code()));
            return;
        }
        install(res.data(), version);
    });
}

void handleRelease(web::WebResponse& res) {
    if (res.code() == 404) {
        alert("No releases", "There are no published releases on GitHub yet.");
        return;
    }
    if (!res.ok()) {
        alert("Update check failed", res.code() <= 0
            ? std::string("No connection, or GitHub did not answer in time.")
            : fmt::format("GitHub answered with HTTP {}. Try again later.", res.code()));
        return;
    }
    auto json = res.json();
    if (!json) {
        alert("Update check failed", "GitHub sent a response that could not be read.");
        return;
    }
    auto release = json.unwrap();
    auto tag = text(release, "tag_name");
    auto latest = parseTag(tag);
    if (!latest) {
        alert("Update check failed", fmt::format("The latest release tag \"{}\" is not a version number.", tag));
        return;
    }
    auto current = Mod::get()->getVersion();
    if (!(*latest > current)) {
        toast(fmt::format("You're up to date ({})", current.toVString()), NotificationIcon::Success);
        return;
    }
    auto version = latest->toVString();
    auto url = findAsset(release);
    if (!url) {
        alert("Update available", fmt::format("{} is out, but the release has no .geode file attached. "
            "Download it from the GitHub releases page.", version));
        return;
    }
    createQuickPopup("Update available",
        fmt::format("<cg>{}</c> is available. You have <cy>{}</c>.\n\nInstall it now?", version, current.toVString()),
        "Skip", "Install",
        [url = *url, version](FLAlertLayer*, bool installNow) {
            if (installNow) download(url, version);
        });
}
}

void tg::updater::checkForUpdates() {
    if (s_busy) {
        toast("Already checking for updates", NotificationIcon::Info);
        return;
    }
    s_busy = true;
    toast("Checking for updates...", NotificationIcon::Loading);
    auto req = web::WebRequest();
    req.header("User-Agent", "tg-death-notifier-updater");
    req.header("Accept", "application/vnd.github+json");
    req.timeout(std::chrono::seconds(20));
    async::spawn(req.get(kLatestRelease), [](web::WebResponse res) {
        s_busy = false;
        handleRelease(res);
    });
}
