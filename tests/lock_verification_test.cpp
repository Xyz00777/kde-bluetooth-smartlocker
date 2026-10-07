#include "smartlocker/session_lock_state.hpp"

#include <cstdio>
#include <cstdlib>

using smartlocker::LockVerification;
using smartlocker::MachineState;
using smartlocker::SessionLockState;
using smartlocker::decideLockVerification;

namespace {

int failures = 0;

void check(const bool condition, const char* description) {
    if (!condition) {
        std::printf("FAIL: %s\n", description);
        ++failures;
    }
}

void expect(const LockVerification actual, const LockVerification expected, const char* description) {
    if (actual != expected) {
        std::printf("FAIL: %s\n", description);
        ++failures;
    }
}

} // namespace

// The verification decision is the rule that decides whether a requested lock is treated as
// done. Treating an unverifiable session as success is a fail-open bug: the latch stays
// asserted, no further lock is ever requested, and a screen that never locked stays unlocked.
int main() {
    // Only a positively observed locked session proves the lock applied.
    expect(decideLockVerification(MachineState::Locked, SessionLockState::Locked, false), LockVerification::KeepLatched,
           "a confirmed locked session keeps the latch");
    expect(decideLockVerification(MachineState::Locked, SessionLockState::Unlocked, false), LockVerification::ClearLatch,
           "a confirmed unlocked session clears the latch and retries");
    expect(decideLockVerification(MachineState::Locked, SessionLockState::Unknown, false), LockVerification::ClearLatch,
           "an unverifiable session is not proof of success, so the latch is cleared and a retry happens");

    // A running lock command has not reported an outcome yet; deciding now would race it.
    expect(decideLockVerification(MachineState::Locked, SessionLockState::Unknown, true), LockVerification::Defer,
           "a running lock command defers the decision");
    expect(decideLockVerification(MachineState::Locked, SessionLockState::Locked, true), LockVerification::Defer,
           "a running lock command defers even when the session reads locked");

    // Outside the Locked state there is nothing to verify, and the flag must reset.
    for (const auto state : {MachineState::Starting, MachineState::Monitoring, MachineState::AwaitingAbsence,
                             MachineState::Snoozed, MachineState::Disabled, MachineState::Error}) {
        for (const auto sessionState : {SessionLockState::Unlocked, SessionLockState::Locked, SessionLockState::Unknown}) {
            expect(decideLockVerification(state, sessionState, false), LockVerification::KeepLatched,
                   "a machine that is not Locked leaves the latch untouched");
            expect(decideLockVerification(state, sessionState, true), LockVerification::KeepLatched,
                   "a machine that is not Locked leaves the latch untouched while a command runs");
        }
    }

    // The retry that follows an unverifiable session must be bounded by the away duration
    // rather than firing immediately, which would spam lock commands against a broken logind.
    check(decideLockVerification(MachineState::AwaitingAbsence, SessionLockState::Unknown, false) == LockVerification::KeepLatched,
          "after the latch clears, the machine waits out the away duration before locking again");

    if (failures == 0) {
        std::printf("all lock verification decision checks passed\n");
        return 0;
    }
    std::printf("%d lock verification decision check(s) failed\n", failures);
    return 1;
}