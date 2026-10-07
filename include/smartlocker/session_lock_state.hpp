#pragma once

#include "smartlocker/state_machine.hpp"

namespace smartlocker {

enum class SessionLockState {
    Unlocked,
    Locked,
    Unknown,
};

enum class LockVerification {
    // The lock is proven applied, or the session is not ours to judge; leave the latch alone.
    KeepLatched,
    // The lock did not take effect, or could not be confirmed; drop the latch so the away
    // countdown can run again and another lock can be requested.
    ClearLatch,
    // The lock command is still running; deciding now would race the command's own outcome.
    Defer,
};

// Pure decision for verifyLockApplied(). Exists so the fail-open rule is directly testable
// without a live logind session.
//
// Only a positively observed Locked session counts as proof. Unknown is deliberately NOT
// treated as success: keeping the latch asserted on an unverifiable session would stop every
// future lock attempt, so a screen that never actually locked would stay unlocked forever.
[[nodiscard]] constexpr LockVerification decideLockVerification(const MachineState machineState,
                                                               const SessionLockState sessionState,
                                                               const bool lockCommandRunning) {
    if (machineState != MachineState::Locked) {
        return LockVerification::KeepLatched;
    }
    if (lockCommandRunning) {
        return LockVerification::Defer;
    }
    return sessionState == SessionLockState::Locked ? LockVerification::KeepLatched : LockVerification::ClearLatch;
}

}