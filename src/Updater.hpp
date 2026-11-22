#pragma once

// Self-updater for builds that are not on the Geode Index: checks the latest
// GitHub release, offers Install / Skip, downloads the .geode file, replaces
// the installed mod and asks for a restart. Main thread only.
namespace tg::updater {

void checkForUpdates();

}
