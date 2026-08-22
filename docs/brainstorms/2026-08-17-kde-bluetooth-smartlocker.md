# KDE Plasma 6 Bluetooth Smart-Lock: Brainstorm / Discovery Notes
Date: 2026-08-17 · Goal: Stress-test and lock down the design decisions for the KDE Plasma 6 port of gnome-bluetooth-smartlock, resolving every open question into a durable decision record.

## Summary / key decisions
- **Q1 (CORE LANGUAGE):** C++ (Qt). The shared backend is a C++/Qt implementation. This means the upstream JS state machine + D-Bus layer are NOT vendored — they are reimplemented in C++ (QDBus), using upstream as a behavioral reference only. This is a full rewrite of the core, not a port.
- **Q7-INPUT (PACKAGING CONSTRAINT):** Must be easily installable **natively with KDE** AND **with NixOS**. Hard distribution requirement. Implication: build/packaging must be reproducible and declarative (Nix-friendly); KDE-native path must be clean (plasmoid via kpackagetool5 and/or distro package). This constrains the architecture choice (Q7) and makes a Nix package mandatory (Q19); Flatpak likely out.
- **Q7 (ARCHITECTURE):** Standalone C++/Qt daemon as a systemd user service, with a thin Plasma/QML client communicating over D-Bus. One daemon runtime is preferred; no duplicate in-process mode for v1. This is the best fit for NixOS declarative service configuration and KDE widget restart resilience.
- **Q4 (COMPATIBILITY):** Target Plasma 6 broadly (minimum Plasma 6.0 where the required APIs permit), validate directly on this system's Plasma 6.7.4 / Qt 6.11.1, and avoid APIs introduced after the declared minimum. Raise the minimum only when a concrete API or packaging requirement makes it necessary.
- **Q8 (PRESENCE MODEL):** Use a layered hybrid: BlueZ D-Bus events as the primary signal, bounded active presence probing as fallback, and RSSI as additional proximity evidence with hysteresis. RSSI must remain non-fatal when unavailable.
- **Q9 (RSSI SCOPE):** Implement RSSI directly in v1, including threshold configuration, hysteresis, and averaging. RSSI remains an optional runtime capability: basic locking must still work when RSSI is unavailable.
- **Q10 (MULTI-DEVICE POLICY):** Make the policy configurable in the application and expose the same setting through a NixOS deployment option. The user can choose the policy rather than being locked to any-of.
- **Q11 (SNOOZE):** Snooze is configurable, defaults to 30 seconds, and the configured duration is its effective cap. It is available in the compact Plasma client and must work with complex multi-device configurations.
- **Q11-MAX (SNOOZE MAXIMUM):** There is no separate maximum setting. The configured snooze duration itself is the cap; the default is 30 seconds and the duration is configurable per profile.
- **Q28 (SNOOZE SEMANTICS):** No separate maximum; configured duration is the effective bound.
- **Q26 (MULTI-DEVICE CONFIGURATION):** Default mode uses simple any-of/all-of policy. An explicitly enabled advanced mode adds named device groups combined with nested boolean expressions. Advanced configuration must be representable in both the Plasma UI and NixOS options.
- **Q12 (PRE-LOCK NOTIFICATION):** Configurable lead time and fully disableable; default is disabled. Informational by default, with optional configurable Snooze action.
- **Q12-ACTION (NOTIFICATION ACTION):** When enabled, notifications are informational by default. Users can configure an actionable Snooze button.
- **Q13 (SUSPEND/RESUME):** After resume, reinitialize Bluetooth monitoring and apply a separate configurable post-resume grace period, default 30 seconds; allow disabling it for immediate evaluation.
- **Q14 (STARTUP):** Startup absence behavior is configurable, with the safe default of waiting until the device policy has been seen once before absence-locking begins.
- **Q15 (CONFIGURATION):** Use one daemon-owned typed declarative configuration with two layers: system/default configuration (including NixOS-generated settings) and user configuration/overrides. User settings extend or override the baseline according to explicit merge rules.
- **Q27 (CONFIGURATION HIERARCHY):** Support both modes: normal mode allows user overrides over the system baseline; strict mode makes system/NixOS configuration authoritative and prevents user overrides for enforced fields.
- **Q27-ROLES (ADVANCED POLICY):** Advanced mode uses named device groups combined with AND/OR expressions. It does not add separate required/optional/fallback role semantics.
- **Q16 (DEVICE SELECTION):** Provide both a graphical paired-device picker and manual address/adapter configuration for advanced users and NixOS.
- **Q17 (NOTIFICATIONS):** Use native Plasma notifications over D-Bus. No `notify-send` dependency for the primary path; notifications remain disabled by default.
- **Q18 (LOGGING):** Use structured Qt logging routed to journald, with configurable verbosity. No application-managed file logging by default.
- **Q19 (PACKAGING):** Target native CMake packaging, a Nix flake/package plus NixOS module, and a KDE Store plasmoid package.
- **Q20 (AUTO-UNLOCK):** Never implement automatic unlock. The project is permanently lock-only; Bluetooth presence cannot unlock a session.
- **Q21 (SESSION SCOPE):** Lock only the active Plasma session associated with the daemon user. Do not affect other users' sessions by default.
- **Q22 (STARTUP FAILURE):** If BlueZ/controller support is unavailable at startup, wait and retry without locking. This avoids false locks during boot or controller initialization.
- **Q22-ERROR UX:** When startup Bluetooth support is unavailable, expose a clear error state/message while continuing to retry; do not silently wait.
- **Q22-RUNTIME FAILURE:** If Bluetooth fails after a previously healthy session while enabled, report the error and use the normal configured locking/away duration before locking.
- **Q22-DEACTIVATION:** When the application or a device is disabled, monitoring pauses and must not lock. When enabled, runtime Bluetooth failure uses the normal configured locking/away duration rather than a separate failure-grace default.
- **Q12-LEAD TIME:** When enabled, the pre-lock notification defaults to the start of the normal configured locking/away countdown.
- **Q29 (SIGNAL PRECEDENCE):** RSSI is dominant when available. RSSI below threshold may initiate locking even while connected; when RSSI is unavailable, fall back to D-Bus connection/probing behavior.
- **Q30 (INVALID POLICY):** Reject empty/invalid advanced policies, show a warning, let the user retry, and keep the last valid configuration active.
- **Q31 (CONFIG MERGE):** Device lists always extend across system/NixOS and user layers. Devices declared by NixOS remain in the effective configuration, but the UI may disable them for triggering as a runtime activation state without deleting or overriding their declarative definition.
- **Q35 (UI SNOOZE LOCATION):** Determine whether Snooze appears only in the full configuration overview or also in the compact Plasma client; the notification action is configurable.
- **Q31-PERSISTENCE:** UI disabling of a NixOS-defined device persists until the next NixOS configuration activation, which resets runtime overrides.
- **Q32 (NIXOS RESET DETECTION):** Use a NixOS generation/configuration marker. The daemon clears runtime overrides when the marker changes.
- **Q33 (MULTI-DEVICE DEFAULT):** Default simple policy is any-of (1-of-M), with configurable N-of-M threshold policies also supported in simple mode.
- **Q34 (RSSI CONFIGURATION):** RSSI threshold, hysteresis, and averaging settings are configured per device, allowing different phone/watch behavior.
- **Q23 (PLASMA CONTROLS):** Do not expose a manual lock-screen button. The Plasma client must allow enabling/disabling individual devices, enabling/disabling the application globally, and provide a button that opens the full configuration overview.
- **Q24 (TEST HARDWARE):** Hardware-in-loop testing will use a smartphone plus a smartwatch/wearable, validating multi-device and advanced policy behavior.
- **Q25 (ISSUE #13):** Leave upstream issue #13 untouched because it belongs to another project. Use it only as a reference for design ideas; no upstream issue action is part of this project.
- **Q5 (PUBLICATION):** Create a new standalone project and publish the Plasma frontend as its own KDE Store plasmoid. Do not base the project on or merge it into `kde-bluetooth-lock`.
- **Q6 (AUTO-UNLOCK REVIEW):** No auto-unlock security review is needed because automatic unlock is permanently out of scope.
- **Q36 (PROJECT IDENTITY):** Create a new GitHub repository/project named `kde-bluetooth-smartlocker`.

## Question Backlog (Pending)
- [x] Q1 — Core language: plain JS (QML-friendly), C++ (Qt), or TypeScript→JS? (PLAN §17.1) → **C++ (Qt)**
- [x] Q2 — Fork upstream and vendor the core, or reimplement from the audit? (PLAN §17.2) → **Reimplement in C++ (QDBus); upstream JS = behavioral reference only, not vendored.**
- [x] Q3 — Is `foreignmeloman/kde-bluetooth-lock` acceptable as the base instead of a new build? (PLAN §17.3) → **No — it's Python, not a code base for a C++ core; only a UX/operational reference.**
- [x] Q4 — Minimum Plasma 6 version to support? **Plasma 6 broadly; target 6.0+ where feasible, validated on installed 6.7.4 / Qt 6.11.1; raise only for a concrete requirement.**
- [x] Q5 — Publication: **New standalone project with its own KDE Store plasmoid.**
- [x] Q6 — Auto-unlock security review: **Not applicable; automatic unlock is permanently out of scope.**
- [x] Q7 — Architecture: in-process C++ library vs. standalone daemon vs. both? **Standalone C++/Qt daemon + thin Plasma/QML D-Bus client; no duplicate in-process runtime for v1.**
- [x] Q8 — Which presence signals are authoritative for v1? **Hybrid: D-Bus events primary + bounded active probing fallback + RSSI as additional evidence/hysteresis; RSSI unavailable must not break basic locking.**
- [x] Q9 — Is RSSI proximity locking (P1) in scope for v1? **Yes, implement directly in v1; RSSI remains optional at runtime and is not required for basic locking.**
- [x] Q10 — Multi-device policy: **Configurable in the application and as a NixOS option; support at least any-of and all-of policies.**
- [x] Q11 — Snooze: **Configurable, 30-second default; configured duration is the effective cap and must operate correctly with complex multi-device policies.**
- [x] Q12 — Pre-lock notification: **Configurable and completely disableable; default disabled.** When enabled, default lead time is the start of the normal configured locking/away countdown; informational by default with optional Snooze action.
- [x] Q13 — Suspend/resume: **Reinitialize monitoring after resume and use a separate configurable post-resume grace period; default 30 seconds, disableable.**
- [x] Q14 — Startup behavior: **Configurable; default waits until the device has been seen once before absence-locking.** Strict immediate startup locking remains available as an option.
- [x] Q15 — Config storage: **Daemon-owned typed declarative config with hierarchical system/default and user layers.** NixOS generates the baseline; the user layer can extend/override it.
- [x] Q16 — Device selection UX: **Both** graphical paired-device picker and manual address/adapter configuration; automatic adapter selection by default, explicit selection for advanced/NixOS use.
- [x] Q17 — Notification mechanism: **Native Plasma notifications over D-Bus.** Notifications remain disabled by default; no `notify-send` primary dependency.
- [x] Q18 — Logging: **Structured Qt logging to journald**, configurable verbosity; no file logging by default.
- [x] Q19 — Packaging: **Native CMake package + Nix flake/package and NixOS module + KDE Store plasmoid package.**
- [x] Q20 — Auto-unlock: **Never implement.** Bluetooth presence is never an unlock mechanism.
- [x] Q21 — Multi-seat/multi-user: **Lock only the active Plasma session associated with the daemon user.**
- [x] Q22 — Failure mode: **At startup, wait/retry without locking; after a previously healthy session fails while enabled, use the normal configured locking/away duration and report the error. Disabled application/devices pause without locking.**
- [x] Q23 — Plasma controls: **No manual lock button.** Provide per-device enable/disable, global application enable/disable, and a settings button opening the configuration overview.
- [x] Q24 — Testing: **Hardware-in-loop is feasible with a smartphone plus smartwatch/wearable.**
- [x] Q25 — Issue #13 upstream action: **Leave untouched; it belongs to another project and is reference material only.**
- [x] Q26 — Multi-device policy language: **Default simple any-of/all-of; advanced opt-in supports named device groups with nested boolean expressions.** Same model must map to UI and NixOS options.
- [x] Q27 — Configuration hierarchy semantics: **Support normal system-baseline + user overrides and strict system-wins mode, configurable per config/profile.** Strict mode prevents user overrides for enforced fields in that config.
- [x] Q28 — Snooze maximum: **No separate maximum setting; the configured snooze duration is the cap.** Default 30 seconds, configurable per profile.
- [x] Q27-ROLES — Advanced policy roles: **Named device groups combined with nested AND/OR expressions; no separate required/optional/fallback roles.**
- [x] Q29 — Signal precedence: **RSSI dominant when available; below-threshold RSSI can initiate locking despite connection; unavailable RSSI falls back to D-Bus/probing.**
- [x] Q30 — Invalid advanced policy: **Reject with warning, allow retry, and retain the last valid configuration.**
- [x] Q31 — Configuration merge: **Device lists always extend; NixOS-defined devices can be disabled in the UI for triggering via runtime state, without removing their declarative definition.**
- [x] Q31-PERSISTENCE — Runtime device disable: **Persist until the next NixOS configuration activation, then reset.**
- [x] Q32 — NixOS reset detection: **Clear runtime overrides when a NixOS generation/configuration marker changes.**
- [x] Q33 — Multi-device default: **Any-of by default (1-of-M), plus configurable N-of-M threshold policies.**
- [x] Q34 — RSSI configuration: **Per-device RSSI threshold, hysteresis, and averaging settings.**
- [x] Q35 — Snooze UI location: **Compact Plasma client.**
- [x] Q36 — Project identity and hosting: **New GitHub project named `kde-bluetooth-smartlocker`.**

## Q&A log
### Q1 — Core language
- Asked: Which language for the shared backend core (JS / C++ / TS→JS)?
- Captured: **C++ (Qt)**. User chose C++/Qt. This is a full rewrite of the core (state machine + D-Bus layer reimplemented with QDBus), not a port of upstream JS. Upstream = behavioral reference only.
- Flags: none

### Q2 — Fork/vendor vs reimplement (resolved as consequence of Q1)
- Asked: (auto-resolved) Fork upstream and vendor, or reimplement from audit?
- Captured: **Reimplement in C++ (QDBus)**. Upstream JS is not vendored; it is a behavioral reference. The Phase 0 audit still applies (map upstream behavior → C++ spec).
- Flags: none

### Q3 — kde-bluetooth-lock as base (resolved as consequence of Q1)
- Asked: (auto-resolved) Is kde-bluetooth-lock acceptable as the base?
- Captured: **No.** It is Python; not a code base for a C++ core. It remains a UX/operational reference only (grace periods, notifications, snooze, multi-device any-of).
- Flags: none

### Q7-INPUT — Packaging constraint (given while answering Q7)
- Asked: (user volunteered) What distribution targets matter?
- Captured: **Must be easily installable natively with KDE AND with NixOS.** Hard requirement. Implication: reproducible/declarative build (Nix-friendly), clean KDE-native path (plasmoid via kpackagetool5 and/or distro package). Nix package mandatory; Flatpak likely out. This informs Q7 (architecture) and Q19 (packaging).
- Flags: none

### Q7 — Architecture
- Asked: Which architecture best satisfies KDE-native and NixOS installation: in-process library + plasmoid, standalone daemon + thin client, or both runtime modes?
- Captured: **Standalone C++/Qt daemon as a systemd user service + thin Plasma/QML client over D-Bus.** Do not ship a duplicate in-process runtime for v1. The daemon is declaratively packageable/configurable on NixOS and survives plasmoid restarts; the client remains KDE-native.
- Flags: none

### Q4 — Minimum Plasma version
- Asked: What minimum Plasma 6 version should be supported?
- Captured: User wants it to run on their system and has no objection to a higher minimum. System inspection found **Plasma 6.7.4**, **KDE Frameworks 6.29.0**, and **Qt 6.11.1**. Decision: target **Plasma 6 broadly, minimum 6.0 where required APIs permit**, validate on 6.7.4, and raise the minimum only for a documented API or packaging need.
- Flags: exact minimum must be confirmed during implementation against the chosen Plasma/QML APIs.

### Q8 — Presence detection
- Asked: Should v1 use connection state only, the full fallback cascade, RSSI, or a combination?
- Captured: **Combination of fallback cascade + RSSI.** The intended priority is D-Bus events first, bounded active probing when events/connection state are insufficient, and RSSI as additional proximity evidence with hysteresis. RSSI is not allowed to be a hard dependency for basic lock-on-absence behavior.
- Flags: implementation must define how conflicting signals are resolved and must test RSSI-unavailable hardware.

### Q9 — RSSI scope
- Asked: Should RSSI be fully enabled in v1, backend-only for later UI, or deferred?
- Captured: **Fully implement RSSI in v1.** Include RSSI monitoring, threshold configuration, hysteresis, and averaging immediately. Treat RSSI as an optional runtime capability so devices without usable RSSI still receive basic lock protection.
- Flags: define signal precedence/conflict resolution during implementation; test both RSSI-capable and RSSI-unavailable devices.

### Q10 — Multiple trusted devices
- Asked: Should multiple-device presence use any-of, all-of, or a single active device?
- Captured: **Configurable in both interfaces.** The application UI and NixOS deployment options must expose the policy; support at least any-of (one present keeps the session active) and all-of (all required). The user should not be locked into one hard-coded policy.
- Flags: behavior for an empty device list is an implementation validation rule; default policy and complex policy structure are resolved.

### Q11 — Snooze behavior
- Asked: What snooze behavior and default should be used?
- Captured: **Configurable snooze with a 30-second default.** The configured snooze duration itself is the effective cap; snooze must work correctly over multiple devices and complex presence policies.
- Flags: none; compact-client placement is resolved by Q35.

### Q11-MAX — Snooze maximum
- Asked: Should the snooze maximum be fixed or configurable?
- Captured: **No separate maximum setting.** The configured snooze duration itself is the cap; it is configurable per configuration/profile and defaults to 30 seconds.
- Flags: none; the configured snooze duration is the effective bound.

### Q28 — Snooze maximum clarification
- Asked: Does "no maximum" mean indefinite snooze, an explicitly configured cap, or no separate cap using the normal duration?
- Captured: **No separate maximum setting.** The configured snooze duration itself is the cap; default 30 seconds, configurable per profile.
- Flags: none.

### Q27-ROLES — Advanced policy groups
- Asked: Should advanced mode use required/optional/fallback roles, named groups, or both?
- Captured: **Named groups only.** Advanced mode combines named device groups with nested AND/OR expressions. Separate required/optional/fallback roles are not required.
- Flags: group validation and empty-group behavior remain implementation details.

### Q26 — Multi-device policy model
- Asked: Should complex configurations use flat any/all, boolean groups, roles, or another model?
- Captured: **Two-tier configuration.** Default mode is simple any-of/all-of. An explicitly enabled advanced mode supports named device groups combined with nested boolean expression groups. The model must be available in the Plasma UI and declaratively in NixOS options.
- Flags: group validation and error UX remain implementation details.

### Q15 — Configuration storage and hierarchy
- Asked: Should configuration be KConfig-only, daemon config, or split—and can system defaults coexist with user settings?
- Captured: **One daemon-owned typed declarative format with two layers.** A system/default layer is generated or managed by NixOS; a user layer can add or override settings. Plasma edits the user layer through the daemon's validated configuration API.
- Flags: exact file format, precedence, list/policy merge behavior, and administrator-enforced fields are tracked in Q27.

### Q27 — Configuration hierarchy semantics
- Asked: Should system and user settings use baseline+override, system-wins, or user-wins semantics?
- Captured: **Support both normal and strict modes per config/profile.** Normal mode uses a system baseline plus user overrides. Strict mode makes system/NixOS settings authoritative and blocks user overrides for fields marked enforced in that config. The effective configuration must be validated after merging.
- Flags: UI presentation of enforced fields remains an implementation detail; device-list extension and policy replacement rules are resolved.

### Q16 — Device selection and adapters
- Asked: Should devices be selected graphically, entered manually, or both?
- Captured: **Both.** Normal users get a graphical paired-device picker; advanced users and NixOS can enter stable addresses and select adapters explicitly. Automatic adapter selection is the default.
- Flags: device address privacy/display and behavior when a selected adapter disappears remain implementation details.

### Q17 — Notification mechanism
- Asked: Should warnings use native Plasma/D-Bus notifications, `notify-send`, or both?
- Captured: **Native Plasma notifications over D-Bus (option 1).** Keep the feature disabled by default; do not make `notify-send` the primary dependency.
- Flags: none; action behavior is captured in Q12-ACTION.

### Q18 — Logging
- Asked: Should diagnostics use journald, Qt logging, files, or a combination?
- Captured: **Journald**, with structured Qt logging and configurable verbosity. Do not manage application log files by default.
- Flags: exact logging categories and sensitive-data redaction policy remain implementation details.

### Q19 — Packaging and distribution
- Asked: Should distribution target native CMake + NixOS initially, or also KDE Store?
- Captured: **Target all three:** native CMake package, Nix flake/package plus NixOS module, and KDE Store plasmoid package. The NixOS module generates the system configuration layer and enables the daemon declaratively.
- Flags: KDE Store submission requirements and distro-specific native package formats remain implementation details.

### Q20 — Automatic unlock
- Asked: Should the application implement automatic unlock now, later, or never?
- Captured: **Never implement automatic unlock.** The product is permanently lock-only. Bluetooth presence may influence locking policy but must never bypass or unlock Plasma's authentication screen.
- Flags: documentation should state this as a deliberate security boundary.

### Q21 — Multi-seat and multi-user scope
- Asked: Should the daemon lock only the active session, all sessions for the user, or be configurable?
- Captured: **Active-session-only.** The daemon locks only the active Plasma session associated with its user and does not affect other users' sessions by default.
- Flags: session identity must be revalidated immediately before locking to avoid stale-session races.

### Q22 — Bluetooth unavailable at startup
- Asked: Should BlueZ/controller failure at startup fail closed or wait?
- Captured: **Wait and retry without locking, with an explicit error message/state.** Startup/controller initialization failure must not cause an immediate or automatic lock or fail silently.
- Flags: error presentation must work even when optional notifications are disabled.

### Q22-RUNTIME — Runtime Bluetooth failure
- Asked: What should happen if Bluetooth fails after the daemon was already healthy?
- Captured: **Use the normal configured locking/away duration** while the application/device is enabled, while reporting the error. If the application or device is disabled, monitoring pauses and must not lock.
- Flags: interaction with post-resume grace remains an implementation detail.

### Q22-DEACTIVATION — Disabled behavior
- Asked: What should deactivation do during a Bluetooth failure or normal monitoring?
- Captured: **Pause monitoring and do not lock** when the application or individual device is disabled. Re-enabling resumes policy evaluation.
- Flags: UI should clearly distinguish disabled/paused from error/unavailable states.

### Q12-LEAD TIME — Notification timing
- Asked: What default lead time should apply when notifications are enabled?
- Captured: **Start of the normal configured locking/away countdown** (option 3). The lead time remains configurable.
- Flags: none.

### Q29 — Signal precedence
- Asked: How should D-Bus connection state, active probing, and RSSI resolve conflicts?
- Captured: **RSSI-dominant when available.** RSSI below threshold may initiate locking even if the device remains connected. If RSSI is unavailable, use D-Bus and active probing as fallback signals.
- Flags: the UI must clearly indicate RSSI-dominant mode and distinguish RSSI-unavailable fallback from actual out-of-range state.

### Q30 — Invalid advanced policy
- Asked: What should happen for empty groups, missing devices, or invalid Boolean expressions?
- Captured: **Reject the configuration with a warning, allow the user to retry, and keep the last valid configuration active.** Invalid policy must not silently weaken or disable protection.
- Flags: exact warning text and field-level validation UX remain implementation details.

### Q31 — Layered device-list behavior
- Asked: Should layered device lists replace, deep-merge, or explicitly choose per-field merge behavior?
- Captured: **Always extend device lists.** NixOS/system devices remain present in the effective configuration. The UI may disable a device for triggering as runtime state, but cannot delete or declaratively override it.
- Flags: strict mode may still permit runtime device disable; generation change resets that state, as specified.

### Q31-PERSISTENCE — Runtime disable lifetime
- Asked: How long should a UI disable of a NixOS-defined device persist?
- Captured: **Until the next NixOS configuration activation**, which resets runtime overrides.
- Flags: marker path/format and permissions remain implementation details of the NixOS module.

### Q32 — NixOS activation detection
- Asked: How should the daemon detect that a NixOS configuration activation occurred?
- Captured: **Use a NixOS generation/configuration marker.** When the marker changes, the daemon clears runtime device-disable overrides.
- Flags: marker path/format and permissions remain implementation details of the NixOS module.

### Q33 — Multi-device default and thresholds
- Asked: Which simple policy should be the default, and should threshold policies be supported?
- Captured: **Any-of by default**, represented as 1-of-M. Also support configurable **N-of-M** policies, such as at least 2 of 3 devices present.
- Flags: behavior for N=0, N>M, and disabled devices must be validated and rejected clearly.

### Q34 — Per-device RSSI settings
- Asked: Should RSSI thresholds be global, per-device, or per-policy/group?
- Captured: **Per-device.** Each trusted device may define its own RSSI threshold, hysteresis, and averaging settings.
- Flags: behavior for devices without RSSI remains the established D-Bus/probing fallback.

### Q23 — Plasma client controls
- Asked: Which manual controls should appear in the Plasma client/tray menu?
- Captured: **No manual lock-screen action.** The client should expose per-device enable/disable, global application enable/disable, and a button for the full configuration overview.
- Flags: exact status presentation remains an implementation detail; compact-client Snooze placement is resolved.

### Q24 — Testing hardware
- Asked: What hardware is available for real Bluetooth testing?
- Captured: **A smartphone plus a smartwatch/wearable.** This supports testing independent devices, any/all policies, advanced boolean/role configurations, RSSI, disconnects, and return-to-range behavior.
- Flags: exact phone/watch models and Bluetooth capabilities can be recorded when implementation testing begins.

### Q25 — Upstream issue #13
- Asked: Should this project close, retarget, or leave upstream issue #13 untouched?
- Captured: **Leave it untouched.** It belongs to another project and is useful only as reference material for event-driven D-Bus design ideas.
- Flags: none.

### Q5 — Project publication
- Asked: Should this become a new project, contribute to kde-bluetooth-lock, or develop standalone first?
- Captured: **New standalone project** with its own KDE Store plasmoid. It is not based on or merged into the Python `kde-bluetooth-lock` project.
- Flags: project name/branding and repository hosting remain open.

### Q6 — Auto-unlock security review
- Asked: Who owns the security review for auto-unlock, and is auto-unlock wanted?
- Captured: **Not applicable.** Automatic unlock is permanently out of scope, so no auto-unlock implementation or review gate is needed.
- Flags: documentation must retain the explicit lock-only security boundary.

### Q36 — Project identity and hosting
- Asked: Should the standalone project use GitHub, remain private/local, or defer naming/hosting?
- Captured: **New GitHub project named `kde-bluetooth-smartlocker`.**
- Flags: GitHub owner/organization remains unspecified.

### Q12 — Pre-lock notification
- Asked: Should the warning be early, shortly before lock, both, and what default?
- Captured: **Configurable lead time, fully disableable, default disabled.** The user does not want notifications enabled by default.
- Flags: none; action behavior is captured in Q12-ACTION.

### Q12-ACTION — Notification action
- Asked: Should enabled notifications include Snooze, be information-only, or expose multiple actions?
- Captured: **Information-only by default.** The user may configure the notification to include an actionable Snooze button.
- Flags: none.

### Q13 — Suspend/resume
- Asked: After resume, should the session re-lock if the device is absent, or trust pre-suspend state?
- Captured: Add a separate **post-resume grace-period option**, configurable and defaulting to **30 seconds**. It can be disabled when immediate evaluation is wanted. Resume should reinitialize Bluetooth monitoring before applying the policy.
- Flags: implementation must avoid treating Bluetooth controller startup delay as genuine absence; test suspend/resume with and without the grace period.

### Q14 — Startup behavior
- Asked: Should an absent device at daemon startup lock immediately, wait for first presence, or be configurable?
- Captured: **Configurable with default safe behavior.** Default mode waits until the configured device policy has been observed once, then tracks subsequent absence. A strict mode may begin absence-locking immediately.
- Flags: strict-mode warning/documentation should explain the risk of startup false positives.

### Q35 — Snooze UI location
- Asked: Should Snooze be available in the compact Plasma client, full configuration overview, or both?
- Captured: **Compact Plasma client.** Snooze should be quickly accessible without opening the full configuration overview.
- Flags: none.

## Open flags (pending input)
- GitHub owner/organization for `kde-bluetooth-smartlocker` -> project owner; does not block architecture or implementation.
