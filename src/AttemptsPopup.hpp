#pragma once

class GJGameLevel;

namespace tg {
// Attempt total, linked copies and the manual correction for one level.
// Every action is saved immediately and confirmed with a notification.
void openAttempts(GJGameLevel* level);
}
