#include "smartlocker/session_lock_state.hpp"

#include <cstdio>
#include <cstdlib>

using smartlocker::SessionLockState;
using smartlocker::lockIsVerified;

namespace {

int failures = 0;

void check(const bool condition, const char* description) {
    if (!condition) {
        std::printf("FAIL: %s\n", description);
        ++failures;
    }
}

} // namespace

// This is the rule that decides whether a requested lock is treated as having taken effect.
// Treating an unverifiable session as success is fail-open: the latch stays asserted, no further
// lock is ever requested, and a screen that never actually locked stays unlocked forever.
int main() {
    // Only a positively observed locked session is accepted as proof the lock applied.
    check(lockIsVerified(SessionLockState::Locked), "a confirmed locked session is accepted as proof");

    // A session known to be unlocked is not proof, so the latch must be dropped and the lock
    // retried after the away duration.
    check(!lockIsVerified(SessionLockState::Unlocked), "a confirmed unlocked session is not proof");

    // The regression this exists to prevent: an unverifiable session must not be accepted as
    // proof, because doing so permanently latches the lock and stops all future attempts.
    check(!lockIsVerified(SessionLockState::Unknown), "an unverifiable session is not proof of success");

    if (failures == 0) {
        std::printf("all lock verification decision checks passed\n");
        return 0;
    }
    std::printf("%d lock verification decision check(s) failed\n", failures);
    return 1;
}