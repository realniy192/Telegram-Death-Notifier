#pragma once

#include <string>

namespace tg {
// Queue a caption for the next completed frame. Safe to call from any thread.
// A bounded queue drops excess requests rather than retaining unlimited images.
void queueScreenshot(std::string caption);
}
