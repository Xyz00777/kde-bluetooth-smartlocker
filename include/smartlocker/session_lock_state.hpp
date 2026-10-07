#pragma once

namespace smartlocker {

// Tri-state answer to "is the session locked?", where Unknown means logind could not be
// consulted or the session could not be identified.
enum class SessionLockState {
    Unlocked,
    Locked,
    Unknown,
};

// The fail-open rule for verifyLockApplied(), as a pure function so it can be tested without a
// live logind session.
//
// Only a positively observed Locked session counts as proof that the lock applied. Unknown is
// deliberately NOT success: keeping the lock latch asserted on an unverifiable session stops
// every future lock attempt, so a screen that never actually locked stays unlocked forever.
[[nodiscard]] constexpr bool lockIsVerified(const SessionLockState sessionState) {
    return sessionState == SessionLockState::Locked;
}

}