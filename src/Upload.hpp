#pragma once

#include "Screenshot.hpp"

#include <cstdint>
#include <string>
#include <vector>

// Telegram upload queue (moved out of Screenshot.cpp). One HTTP request at a
// time, on the main thread. Every failure shows an in-game error notification.
namespace tg::upload {

// One encoded PNG, one job per caption. Safe from any thread.
void enqueuePhoto(std::vector<uint8_t> bytes, std::vector<ShotText> captions, bool noCaption);

// Deferred captions (rating panel): the photo is captured first.
void expectCaption(uint64_t ticket);
void forgetTicket(uint64_t ticket);
void resolve(uint64_t ticket, ShotText text);

void sendText(std::string text);

// Shows an error notification in game. Safe from any thread; the same
// message is shown at most once every few seconds.
void notifyError(std::string message);

}
