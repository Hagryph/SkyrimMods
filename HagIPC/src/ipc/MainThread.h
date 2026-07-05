#pragma once
#include "PCH.h"
#include "SKSE_Min.h"
#include <functional>

// Marshal work onto the game's main thread. The socket thread must NOT call game functions,
// write game memory, or run the console directly; it enqueues a task via the SKSE Task
// interface (drained once per frame on the main thread) and blocks until it finishes.
namespace hag::mt {

void SetTaskInterface(skse::TaskInterface* t);
bool Available();

// Run 'fn' on the game main thread. Adaptive: when the SKSE task queue is known-idle (the main
// menu), run inline immediately (no wait); once a queued task is seen draining (in-game), route
// to the main thread and wait up to 'timeoutMs'. So menu commands are instant and in-game
// commands are main-thread-safe, with one probe command per menu<->game transition.
// Always completes before returning. Returns true if it ran on the main thread, false if inline.
// Capture state by shared_ptr, not stack reference.
bool Run(std::function<void()> fn, unsigned timeoutMs = 500);

// Run 'fn' ONLY on the game main thread. Unlike Run(), this NEVER falls back to inline: if the
// main thread doesn't pick up the task within 'timeoutMs' (main menu, or the game is unfocused so
// its update thread is throttled) it atomically abandons the task -- guaranteeing 'fn' runs neither
// off-thread now nor later -- and returns false. Use for work that is unsafe off the main thread
// (console execution, form creation). Capture state by shared_ptr, not stack reference.
bool RunOnMain(std::function<void()> fn, unsigned timeoutMs = 1000);

}  // namespace hag::mt
