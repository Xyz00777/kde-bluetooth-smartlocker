# KDE Plasma 6 Smart-Lock — Planning Document

**Project:** A new standalone GitHub project, `kde-bluetooth-smartlocker`, reimplementing the behavior of the upstream GNOME Shell extension [ba0f3/gnome-bluetooth-smartlock](https://github.com/ba0f3/gnome-bluetooth-smartlock) for KDE Plasma 6. No GitHub owner/organization has been selected yet.

**Status:** Implemented lock-only v1 candidate. Remaining release checks are real Plasma-session locking, rendered Plasma visual QA, and KDE Store publication packaging.

**Audience:** A developer who has not seen the originating conversation. This document is self-contained and actionable.

---

## 1. Executive Summary

The upstream application is a **GNOME Shell extension**. A GNOME Shell extension cannot run inside Plasma merely by installing its metadata — GNOME Shell extensions are loaded by the GNOME Shell process (a JavaScript engine inside Mutter), which Plasma does not run. Plasma support therefore requires a native implementation.

The owner has finalized the architecture and scope (see §17, Owner Decisions):

- **Architecture:** a standalone **C++/Qt daemon** running as a **systemd user service**, plus a **thin Plasma/QML client** that talks to the daemon over **D-Bus**. There is no duplicate in-process runtime for v1.
- **Language:** the core and the BlueZ layer are **reimplemented in C++/Qt (QDBus)**. The upstream JavaScript/GJS is a **behavioral reference only**, not vendored code.
- **Presence:** BlueZ D-Bus events are the primary signal, bounded active probing is the fallback, and **RSSI is implemented directly in v1** as additional proximity evidence. RSSI is dominant when available and falls back to D-Bus/probing when unavailable.
- **Scope:** the product is **permanently lock-only**. Automatic unlock is never implemented. RSSI proximity locking is in v1, not deferred.
- **Packaging:** native CMake, a Nix flake/package plus NixOS module, and a KDE Store plasmoid package.

The plan is phased so that a usable **lock-only** Plasma solution ships early, with RSSI and multi-device policies included in v1.

---

## 2. What We Know About Upstream (Evidence Base)

All statements below are grounded in the supplied upstream evidence. Nothing in this document claims behavior beyond what was provided.

### 2.1 Upstream repository layout (as supplied)

| Component | File | Role in upstream |
|---|---|---|
| Extension lifecycle | `extension.js` | GNOME Shell lifecycle; panel status-area indicator; session-mode handling; SmartLock startup/teardown |
| Core logic | `smartlock.js` | Connect/disconnect state machine; away-duration lock timer; reconnect polling/page attempts; optional RSSI proximity lock; optional auto-unlock |
| Bluetooth D-Bus layer | `bluetooth/dbus.js` | BlueZ ObjectManager enumeration via `org.freedesktop.DBus.ObjectManager.GetManagedObjects`; subscription to `PropertiesChanged` and `InterfacesRemoved` signals; reconnect calls (`ConnectProfile`/`Connect`) with a 2.5 s page-abort; optional RSSI service integration |
| Configuration | `settings.js`, `settings.ui`, GSettings schema | Preferences storage and preference UI |
| Packaging | `Makefile` | GNOME-extension packaging/install (`gnome-extensions pack` / `gnome-extensions install`) |
| Optional RSSI service | `services/` | Optional RSSI systemd service plus D-Bus policy/service files, currently GNOME-namespaced (`org.gnome.BluetoothRSSI`) |

### 2.2 Issue #13 and PR #14 (evidence)

- **Issue #13** — *"Use DBus instead of polling"* — is **open**, created **2025-07-11**, and proposes:
  - subscribing to BlueZ `org.freedesktop.DBus.Properties.PropertiesChanged`, and
  - enumerating devices with `org.freedesktop.DBus.ObjectManager.GetManagedObjects`.
  - It has **1 heart reaction and 0 comments**.
- **PR #14** — *"Use dbus subscription instead of polling"* — is **closed** in the issue listing.
- The **current upstream `bluetooth/dbus.js` already contains** the D-Bus signal subscriptions (`PropertiesChanged`, `InterfacesRemoved`) and `GetManagedObjects` enumeration that issue #13 proposes.

**Consequence for this plan:** issue #13 belongs to another project and is **untouched reference material only** (owner decision Q25, §17). This project does not close, retarget, or modify it. The event-driven D-Bus subscription design it proposes is adopted as a design reference for the C++/Qt BlueZ layer, but no upstream issue action is part of this project.

**Remaining timer/polling reality:** even with D-Bus event subscriptions in place, upstream still performs **active reconnect attempts/timers for presence detection** (reconnect polling/page attempts with a 2.5 s page-abort). Event-driven state notifications therefore **do not eliminate every polling/timer concern** — presence detection of a device that is powered off or out of range still requires periodic probing. The plan below reflects that.

### 2.3 References

- Upstream repository: <https://github.com/ba0f3/gnome-bluetooth-smartlock>
- Issue #13: <https://github.com/ba0f3/gnome-bluetooth-smartlock/issues/13>
- PR #14: <https://github.com/ba0f3/gnome-bluetooth-smartlock/pull/14>
- KDE-native alternative: <https://github.com/foreignmeloman/kde-bluetooth-lock>

---

## 3. Reusable vs. Replace (Porting Analysis)

The owner decision (Q1/Q2, §17) is that the core and BlueZ layer are **reimplemented in C++/Qt (QDBus)**. The upstream JavaScript/GJS is a **behavioral reference only**; it is not vendored. This is a full rewrite of the core, not a port. The table below therefore describes what is *reused as reference* versus what is *replaced*.

### 3.1 Reusable from upstream (as behavioral reference)

| Upstream asset | Reusable? | Notes |
|---|---|---|
| `smartlock.js` state machine logic (connect/disconnect states, away-duration timer, reconnect strategy) | **Yes — as behavioral reference** | The *logic* is desktop-independent (it reasons about BlueZ device states and elapsed time). It is reimplemented in C++/Qt, not vendored. |
| `bluetooth/dbus.js` BlueZ D-Bus interaction (GetManagedObjects, PropertiesChanged/InterfacesRemoved, ConnectProfile/Connect with 2.5 s page-abort) | **Yes — as behavioral reference** | BlueZ is desktop-independent. The D-Bus layer is reimplemented with QDBus in C++. The event-driven subscription design from issue #13 is adopted as a design reference (§2.2). |
| Away-duration default and lock threshold semantics | **Yes** | Settings semantics carry over directly. |
| Optional RSSI proximity logic | **Yes — implemented in v1** | The *algorithm* is portable and is implemented directly in v1 (owner decision Q9, §17). The upstream delivery (systemd service + `org.gnome.BluetoothRSSI` D-Bus names, `services/` files) is GNOME-namespaced and is replaced by a C++/Qt RSSI implementation. |
| Optional auto-unlock logic | **No — permanently out of scope** | The product is permanently lock-only (Q20, §17). Automatic unlock is never implemented. |
| GSettings schema + `settings.ui` | **Replace** | GSettings is a GLib/GNOME mechanism. Configuration is a daemon-owned typed declarative format with system/NixOS and user layers (Q15/Q27, §17). The preference UI becomes a QML settings page in the Plasma client. |
| `extension.js` lifecycle, panel indicator, session-mode handling | **Replace** | GNOME Shell extension APIs (`imports.gi`, `St`, `Main.panel`, session-mode callbacks) do not exist in Plasma. Plasma analog: a **thin Plasma/QML D-Bus client** (plasmoid) that talks to the daemon. |
| `Makefile` (gnome-extensions pack/install) | **Replace** | Plasma packaging: native CMake, a Nix flake/package plus NixOS module, and a KDE Store plasmoid package (Q19, §17). See §13. |

### 3.2 Hard truth to state plainly

> **A GNOME Shell extension cannot run inside Plasma merely by changing metadata.** GNOME Shell extensions are interpreted by the GNOME Shell process, which is specific to the GNOME/Mutter desktop. Plasma provides no GNOME Shell compatibility layer. Plasma support requires a **native frontend** (plasmoid / KDE service) and/or a **desktop-independent service** that the Plasma frontend consumes.

### 3.3 Consequence for architecture

The largest reusable asset is the **BlueZ-facing logic** (`bluetooth/dbus.js` + the state machine in `smartlock.js`), reused as a behavioral reference and reimplemented in C++/Qt (QDBus). The largest replace surface is **everything GNOME Shell and GLib/GSettings** (`extension.js`, settings schema/UI, packaging). This maps cleanly onto a standalone daemon + thin client architecture (§4).

---

## 4. Proposed Architecture

### 4.1 Target architecture: standalone C++/Qt daemon + thin Plasma/QML D-Bus client

The owner decision (Q7, §17) is a **standalone C++/Qt daemon as a systemd user service**, with a **thin Plasma/QML client** communicating over D-Bus. There is **no duplicate in-process runtime for v1**.

```
                    ┌────────────────────────────────────────────┐
                    │            BlueZ (system D-Bus)            │
                    └────────────────────────────────────────────┘
                                      ▲
                                      │ GetManagedObjects / PropertiesChanged /
                                      │ InterfacesRemoved / Connect / ConnectProfile
                    ┌────────────────────────────────────────────┐
                    │   Daemon: "kde-bluetooth-smartlocker"      │
                    │   (C++/Qt, systemd user service)           │
                    │   - BlueZ monitor (QDBus subscriptions)    │
                    │   - state machine (away-duration, connect/ │
                    │     disconnect, reconnect policy)          │
                    │   - RSSI proximity (v1, per-device)        │
                    │   - lock trigger: loginctl lock-session    │
                    │   - typed declarative config (system/NixOS │
                    │     baseline + user layer)                 │
                    └──────────────┬─────────────────────────────┘
                                   │ D-Bus (org.kde.smartlocker.*)
                    ┌──────────────▼─────────────────────────────┐
                    │   Thin Plasma/QML client (plasmoid)        │
                    │   - status indicator                       │
                    │   - per-device enable/disable              │
                    │   - global enable/disable                  │
                    │   - compact Snooze                         │
                    │   - configuration overview button          │
                    └────────────────────────────────────────────┘
```

**Why this architecture (owner decision Q7):**
- A standalone daemon is **declaratively packageable and configurable on NixOS** (the NixOS module generates the system config layer and enables the daemon).
- The daemon **survives plasmoid restarts**, so locking is not tied to the widget being alive.
- The client stays **KDE-native** (a Plasma/QML plasmoid).
- One daemon runtime is preferred; a duplicate in-process mode is **not** shipped for v1.

**IPC:** the client and daemon communicate over **D-Bus** (Qt D-Bus). The daemon owns the BlueZ subscriptions, the state machine, RSSI, and the lock action; the client is a thin status/settings surface.

### 4.2 Why not "fork upstream and change metadata"?

Because it cannot work (see §3.2). A fork is still useful as a **source of behavioral reference** (§3.1), but the Plasma product is a new C++/Qt daemon plus a thin client.

### 4.3 KDE-native alternative

[foreignmeloman/kde-bluetooth-lock](https://github.com/foreignmeloman/kde-bluetooth-lock) was evaluated and is **not** used as a code base (owner decision Q3, §17): it is Python, not a base for a C++ core. It remains a **UX/operational reference** only (grace periods, notifications, snooze, multi-device any-of). This project is a new standalone build, not a contribution to that project.

---

## 5. Feature List

### 5.1 Core (must-have for a usable v1)

| ID | Feature | Source behavior (upstream) |
|---|---|---|
| F1 | Lock screen when the paired device is absent/disconnected past the away-duration threshold | `smartlock.js` disconnect handling + away-duration timer |
| F2 | Reconnect/presence detection: periodically probe the device (page attempts with 2.5 s page-abort via `ConnectProfile`/`Connect`) | `bluetooth/dbus.js` reconnect calls |
| F3 | Event-driven state refresh via BlueZ D-Bus (`PropertiesChanged`, `InterfacesRemoved`, `GetManagedObjects`) | `bluetooth/dbus.js` subscriptions |
| F4 | Configurable away-duration (seconds) before locking | settings semantics |
| F5 | Configurable target device selection (graphical paired-device picker + manual address/adapter for advanced/NixOS) | settings UI |
| F6 | Status indicator in Plasma (tray/plasmoid icon showing connected/away) | analog of panel status-area indicator in `extension.js` |
| F7 | Per-device enable/disable and global enable/disable in the client (no manual lock button) | owner decision Q23, §17 |
| F8 | Settings UI (daemon-owned typed config): device, away-duration, enable/disable, snooze, RSSI, policy | settings semantics → QML settings page |

### 5.2 Parity (matches upstream features; ship for full parity)

| ID | Feature |
|---|---|
| P1 | RSSI proximity lock — lock when signal strength crosses a per-device threshold. **Implemented directly in v1** (owner decision Q9, §17), including per-device threshold, hysteresis, and averaging (Q34). RSSI is an optional runtime capability: basic locking still works when RSSI is unavailable. |
| P2 | Session-mode awareness (lock on session switch/lid close when armed) — via Plasma/KDE session signals rather than GNOME session-mode callbacks |
| P3 | Connect/disconnect notification/toasts from the indicator (native Plasma D-Bus notifications, disabled by default) |
| P4 | Pause/resume (snooze) the lock watcher for a user-set duration — bounded, 30-second default, configured duration is the cap (Q11/Q28) |

### 5.3 Optional (nice-to-have, gated)

| ID | Feature | Gate |
|---|---|---|
| O1 | Advanced multi-device policy mode: named device groups combined with nested AND/OR expressions | **Opt-in** (owner decision Q26, §17). Default mode is simple any-of/all-of; advanced mode is explicitly enabled. |
| O2 | Per-device profiles (home vs. work) | Later |
| O3 | KDE Store listing + discovery metadata | Packaging milestone |
| O4 | Strict configuration mode (system/NixOS authoritative for enforced fields) | Configurable per config/profile (Q27, §17) |

### 5.4 Explicitly out-of-scope / security-sensitive

| ID | Exclusion | Why |
|---|---|---|
| X1 | Phone/SMS unlock, "find my phone" | Unrelated feature; must not be added |
| X2 | Network presence (Wi-Fi/ethernet) as a lock trigger | Unrelated; out of scope |
| X3 | Biometric authentication integration | Out of scope; lock/unlock is via the desktop session, not biometrics |
| X4 | Remote administration/control | Out of scope |
| X5 | Automatic unlock, in any form | **Permanently out of scope** (owner decision Q20, §17). The product is lock-only forever; Bluetooth presence never unlocks a session. |
| X6 | Bypassing Plasma's lock screen / writing directly to the session manager | Lock must go through `loginctl lock-session` (or equivalent KDE session API); no private unlock backdoors |
| X7 | Manual lock-screen button in the client | Owner decision Q23, §17: the client exposes per-device and global enable/disable, compact Snooze, and a configuration-overview button, but no manual lock action |

### 5.5 Adopted from cross-project research (source-marked)

These features come from the validated source-level review of three related projects (§18). Each is marked with its source project (A = blueproximity, B = ble-lock-session, C = kde-bluetooth-lock). Per the finalized scope, all adopted features (F9–F20), including RSSI (F11), pre-lock notification (F18), bounded snooze (F19), and multi-device policy (F20), land in Phase 1 (daemon core) as v1 features.

| ID | Feature | Source | Phase |
|---|---|---|---|
| F9 | Hysteresis: separate lock/unlock thresholds to prevent flapping at the boundary | A | 1 |
| F10 | Startup-transition suppression (`ignoreFirstTransition`): no lock/unlock action on startup, prevents spurious boot lock | A | 1 |
| F11 | Ring-buffer RSSI averaging (noise filter) | A | 1 (with P1, RSSI in v1) |
| F12 | Command re-entrancy guard: skip if the previous lock command is still running | A | 1 |
| F13 | Context-aware failure policy: startup BlueZ/controller unavailable reports an explicit error and waits/retries without locking; runtime failure while enabled uses the normal away/locking duration then locks; disabled app/device pauses without locking; unexpected code bugs do not convert blindly into an absence decision | B | 1 |
| F14 | Multi-signal presence cascade: held-channel → connection state → Classic page → BLE scan, not RSSI alone | B | 1 |
| F15 | Transient-failure debounce: consecutive misses required before lock | B | 1 |
| F16 | Suspend/resume handling via logind `PrepareForSleep` | B-gap | 1 |
| F17 | Already-locked suppression via `LockedHint`: avoid repeated lock attempts and unnecessary probing | C | 1 |
| F18 | Pre-lock threshold notification: warn the user before locking | C | 1 (v1, configurable lead time, default disabled) |
| F19 | Bounded snooze: explicit, bounded snooze duration in the backend state machine (not indefinite retry-reset) | C (adapts P4) | 1 (v1, 30-second default, configured duration is the cap) |
| F20 | Explicit multi-device any-of policy: one reachable device = present | C (adapts O2) | 1 (v1, default 1-of-M, configurable N-of-M) |

### 5.6 Rejected ideas from cross-project research

These were considered and rejected, with reasons. Recording them here so the decisions are not re-litigated.

| Idea | Source | Why rejected |
|---|---|---|
| `hcitool`/RFCOMM transport | A | Obsolete; `hcitool` removed in modern BlueZ |
| `gnome-screensaver-command` locking | A | No Wayland/loginctl support; `loginctl lock-session` is the correct primitive (§6.1) |
| Polling-only detection | A, B | Replaced by D-Bus events (issue #13 design, §7) |
| Auto-unlock default-on | A, B | Security-sensitive; all three projects treat it as risky (§6.3) |
| `os.popen`/shell-string command execution | A, B | Injection surface; use structured commands |
| `unlock_cmd` via `loginctl unlock-session` | B | Often needs polkit and fails silently |
| `bluetoothctl` text parsing | B | Fragile |
| Single-device model | B | Conflicts with the multi-device any-of policy (F20) |
| `l2ping` as authoritative presence signal | C | Active reachability probe, not a BlueZ event; optional diagnostic/fallback only |
| Root-run polling as core architecture | C | Lock is a session-level action; a user service suffices |
| Hard-coded `seat0` + implicit first-session selection | C | Multi-seat unsafe |
| Indefinite postpone-by-retry-reset | C | Replaced by bounded snooze (F19) |
| Un-hardened systemd unit (no Restart/sandboxing) | C | Use `Restart=on-failure` and sandboxing (§13.1) |
| Minimal config validation (no schema) | C | Config validation against a spec (A) is adopted instead |
| Ad-hoc root installer with no release packaging | C | Use standard packaging (§13) |

---

## 6. Security & Trust Model

### 6.1 Lock primitive

- **Lock:** `loginctl lock-session` (systemd-logind) is the correct, portable lock primitive for Plasma. It triggers the KDE lock screen like a user-initiated lock.
- **Do not** attempt to lock via Plasma-private APIs as a first choice; `loginctl` is the supported interface.

### 6.2 Threat model for lock-only mode

- The device MAC/alias is a *soft* presence credential. Lock-on-absence is a convenience feature, not a security boundary.
- An attacker who spoofs the device (Bluetooth MAC spoofing / relay) can *prevent* locking, but **cannot unlock** a locked session in lock-only mode. That is the key security property of lock-only.

### 6.3 Automatic unlock — permanently out of scope

The owner decision (Q20, §17) is that **automatic unlock is never implemented**. The product is permanently lock-only; Bluetooth presence may influence locking policy but must never bypass or unlock Plasma's authentication screen. This is a deliberate, documented security boundary, not a deferred feature.

The reasons that would make auto-unlock risky are recorded here for reference, even though no auto-unlock will be built:

1. **Spoofing/relay:** a cloned MAC or a relay (BT forwarding device) can make the system believe the user's phone returned, unlocking the machine for an attacker.
2. **Absence ≠ attacker absence:** the user may be gone while the device returns (e.g., device left behind, delivered by someone else).
3. **Session ambiguity:** auto-unlock on one session may not be safe on a multi-seat/multi-user machine.

Because automatic unlock is permanently out of scope, **no auto-unlock security review gate is required** (Q6, §17). The lock-only boundary is enforced by design: there is no unlock code path.

---

## 7. Issue #13 Assessment (evidence-based recommendation)

### 7.1 What the evidence says

| Fact | Evidence |
|---|---|
| #13 is **open** | Issue listing state (created 2025-07-11) |
| #13 proposes D-Bus subscription (`PropertiesChanged`) + enumeration (`GetManagedObjects`) | Issue body |
| #13 has 1 heart, 0 comments | Issue activity |
| PR #14 "Use dbus subscription instead of polling" is **closed** | Issue listing state |
| Current `bluetooth/dbus.js` already subscribes to `PropertiesChanged`/`InterfacesRemoved` and calls `GetManagedObjects` | Supplied upstream component description |

### 7.2 Interpretation

- **"Issue #13 as a design idea" is substantially addressed upstream.** The event-driven D-Bus subscription approach it proposes already exists in `dbus.js`. This project adopts that design as a reference for the C++/Qt BlueZ layer.
- **Issue #13 is untouched reference material only** (owner decision Q25, §17). It belongs to another project. This project does not close, retarget, or modify it, and no acceptance criterion requires action on it.
- **Event-driven ≠ timer-free.** Upstream still runs reconnect attempts/timers for presence detection (2.5 s page-abort `ConnectProfile`/`Connect` retries). Presence of a powered-off/out-of-range device cannot be learned from events alone; you must probe. So the event-driven design **does not eliminate every polling/timer concern** — it reduces reliance on polling for *state transitions* while retaining timers for *reconnect/presence probing*. The C++/Qt daemon reflects this: D-Bus events primary, bounded active probing fallback (Q8, §17).

### 7.3 Recommendation

1. **Adopt the event-driven D-Bus design as a reference** for the C++/Qt BlueZ layer: a daemon is only viable if it is event-driven at its core. The plan below assumes the event-driven core.
2. **Do not take any upstream action on issue #13.** It is reference material only (Q25, §17). No code audit is required to close/retarget it, and no acceptance criterion depends on it.
3. **Plasma-specific note:** while implementing, treat the event-driven D-Bus subscription design as the reference for the C++/Qt BlueZ layer, and keep bounded active probing for presence detection of powered-off/out-of-range devices.

### 7.4 Verdict

> **Adopt the event-driven design (yes), but leave upstream issue #13 untouched (yes).** It is reference material for the C++/Qt BlueZ layer; no upstream issue action is part of this project. The event-driven core is a prerequisite for the daemon.

---

## 8. Phased Implementation Plan

Dependencies: Phase 0 (spec) → Phase 1 (daemon core) → Phase 2 (Plasma client) → Phase 3 (packaging/hardening). Each phase has acceptance criteria (§9).

### Phase 0 — Derive the C++/Qt spec from upstream (no product code)
- Read `bluetooth/dbus.js` and `smartlock.js` line-by-line and map every D-Bus call/signal and state transition to a C++/Qt (QDBus) specification. Upstream is a behavioral reference, not vendored code (Q1/Q2, §17).
- Inventory every timer/setInterval/setTimeout in `smartlock.js` and classify: state-transition polling (replaceable by events) vs. reconnect/presence probing (must stay).
- Record the event-driven D-Bus subscription design from issue #13 as a reference for the C++/Qt BlueZ layer. **No upstream issue action** (Q25, §17).
- Confirm the target Plasma/QML APIs against the declared minimum (Plasma 6 broadly, 6.0 where feasible; validated on 6.7.4 / Qt 6.11.1) (Q4, §17).

### Phase 1 — C++/Qt daemon core (systemd user service)
- Implement the BlueZ monitor in C++/Qt (QDBus): `GetManagedObjects` enumeration, `PropertiesChanged` + `InterfacesRemoved` subscriptions, reconnect policy (ConnectProfile/Connect, 2.5 s page-abort).
- Implement the state machine: connect/disconnect states, away-duration timer, lock trigger.
- **RSSI in v1 (Q9/Q34, §17):** per-device RSSI threshold, hysteresis, and averaging; RSSI dominant when available, falls back to D-Bus/probing when unavailable (Q29).
- **Multi-device policy (Q26/Q33, §17):** default simple any-of (1-of-M), configurable N-of-M; advanced opt-in named groups with nested AND/OR.
- **Adopted core features (F9–F20):** hysteresis (F9), startup-transition suppression (F10), RSSI averaging (F11), command re-entrancy guard (F12), fail-closed/fail-open (F13), multi-signal presence cascade (F14), transient-failure debounce (F15), suspend/resume via logind `PrepareForSleep` (F16), already-locked suppression via `LockedHint` (F17), pre-lock notification (F18), bounded snooze (F19), multi-device any-of (F20).
- Config module: daemon-owned typed declarative config with system/NixOS baseline + user layer; normal override mode or strict system-wins per profile; device lists always extend (Q15/Q27/Q31, §17).
- Lock action: `loginctl lock-session` (lock-only, permanent).
- Failure modes (Q22, §17): startup Bluetooth failure waits/retries with an explicit error state; runtime failure while enabled uses the normal away/locking duration; disabled app/device pauses without locking.
- Logging: structured Qt logging to journald (Q18, §17).
- Deliverable: a systemd user service with a documented D-Bus interface (start/stop, device selection, config, state-change signals).

### Phase 2 — Thin Plasma/QML client (v1 product)
- Plasmoid scaffold (`metadata.json`, `main.qml`, `config/config.qml`).
- Status indicator binding to daemon state (connected/away/disconnected) over D-Bus.
- Settings UI wired to the daemon's validated config API (device picker, away-duration, enable toggles, snooze, RSSI, policy).
- **No manual lock button** (Q23, §17). Client exposes per-device enable/disable, global enable/disable, compact Snooze, and a configuration-overview button.
- Native Plasma D-Bus notifications, disabled by default, informational by default, optional Snooze action (Q12/Q17, §17).
- **Milestone 1 target — see §16.**

### Phase 3 — Packaging, distribution, hardening
- Package as native CMake, a Nix flake/package plus NixOS module, and a KDE Store plasmoid package (Q19, §17). The NixOS module generates the system config layer and enables the daemon declaratively.
- NixOS reset marker: the daemon clears runtime device-disable overrides when the NixOS generation/configuration marker changes (Q31-PERSISTENCE/Q32, §17).
- Error paths: BlueZ absent, device unpaired, session locked externally, client restart, daemon crash.
- Logging and diagnostics (structured Qt logging to journald) for support.

---

## 9. Acceptance Criteria

### Phase 0
- [ ] C++/Qt (QDBus) spec derived from upstream `dbus.js`/`smartlock.js` as behavioral reference; no upstream code vendored.
- [ ] Event-driven D-Bus design from issue #13 recorded as reference; **no upstream issue action taken** (Q25, §17).
- [ ] Target Plasma/QML APIs confirmed against the declared minimum (Plasma 6 broadly, 6.0 where feasible).

### Phase 1 (daemon core)
- [ ] BlueZ monitor observes connect/disconnect events with no polling for *state transitions* (timers remain only for reconnect/presence probing, per §7.2).
- [ ] `loginctl lock-session` invoked only when the effective configured presence policy evaluates absence/away — including RSSI-dominant threshold behavior (below-threshold RSSI can initiate locking even while connected, Q29), the away duration, and the snooze/disabled state.
- [ ] RSSI implemented in v1 with per-device threshold, hysteresis, and averaging (Q9/Q34); RSSI dominant when available, falls back to D-Bus/probing when unavailable (Q29).
- [ ] Multi-device policy: default any-of (1-of-M), configurable N-of-M; advanced opt-in named groups with nested AND/OR (Q26/Q33).
- [ ] Hysteresis (F9) prevents flapping at the lock/unlock threshold boundary.
- [ ] Startup-transition suppression (F10) produces no lock/unlock action on startup.
- [ ] Context-aware failure policy (F13) verified: startup BlueZ/controller unavailable reports an explicit error and waits/retries without locking; runtime failure while enabled uses the normal away/locking duration then locks; disabled app/device pauses without locking; unexpected code bugs do not convert blindly into an absence decision.
- [ ] Transient-failure debounce (F15) requires consecutive misses before locking.
- [ ] Suspend/resume (F16) handled via logind `PrepareForSleep`; post-resume grace default 30 seconds and disableable (Q13).
- [ ] Already-locked suppression (F17) avoids repeated lock attempts when the session is already locked.
- [ ] Startup Bluetooth failure waits/retries with an explicit error state; runtime failure while enabled uses the normal away/locking duration; disabled app/device pauses without locking (Q22).
- [ ] Unit tests: state machine transitions, away-duration math, reconnect policy, hysteresis, debounce, fail-closed/fail-open, RSSI precedence, multi-device policy (see §11).

### Phase 2 (Plasma v1)
- [ ] Plasmoid installs via `kpackagetool5 --install` on a Plasma 6 session and appears in the panel.
- [ ] Indicator reflects daemon state in real time over D-Bus.
- [ ] Settings UI reads/writes the daemon's validated config API; changes apply live.
- [ ] No manual lock button; per-device enable/disable, global enable/disable, compact Snooze, and configuration-overview button present (Q23).
- [ ] Lock-on-absence works end-to-end with a real device (manual test matrix, §11).

### Phase 3
- [ ] Each packaging target (native CMake, Nix flake/package + NixOS module, KDE Store plasmoid) installs and uninstalls cleanly.
- [ ] NixOS reset marker: runtime device-disable overrides clear when the NixOS generation/configuration marker changes (Q31-PERSISTENCE/Q32).
- [ ] Handles BlueZ restart, client reload, and daemon crash without stuck state.
- [ ] Store listing metadata valid (if published).

---

## 10. Test Strategy

| Layer | Approach |
|---|---|
| Daemon state machine | Unit tests (C++/Qt test harness): transitions, timers, reconnect policy, absence threshold math, multi-device policy, RSSI precedence |
| D-Bus layer | Mock/`faketime` style tests: fake BlueZ ObjectManager emitting `PropertiesChanged`/`InterfacesRemoved`; assert subscription lifecycle |
| Integration (hardware-in-loop) | Real paired devices: **smartphone plus smartwatch/wearable** (owner decision Q24, §17). Out-of-range walks, device power-off, device re-connect; assert lock timing vs. away-duration. Cover RSSI conflicts, missing RSSI, advanced policies, NixOS generation reset, suspend/resume, disabled devices, and invalid config. |
| Plasma UI | Manual + optional Qt Quick Test for the plasmoid; visual check of indicator states |
| Packaging | Install/remove cycle on a clean Plasma 6 session; `kpackagetool5 --list` sanity; Nix flake/package + NixOS module build and activation |
| Security (lock-only) | Verify no unlock code path exists; verify lock-only boundary is enforced by design |

**Not in scope:** full CI automation in milestone 1 (note as future work); hardware matrix limited to the smartphone + smartwatch/wearable available to the maintainer plus user-reported results.

---

## 11. Risks

| Risk | Likelihood | Impact | Mitigation |
|---|---|---|---|
| GNOME/GLib assumptions leak into the C++/Qt daemon | High | Medium | Phase 0 derives a C++/Qt spec from upstream; code review gate; no GSettings/GNOME APIs |
| Bluetooth stack quirks (BlueZ version differences, unreliable event delivery) | Medium | High | Keep reconnect/presence probing (timers stay); add diagnostic logging; user-test matrix |
| KDE Store / kpackagetool differences across Plasma 6 point releases | Medium | Low | Target Plasma 6 broadly, minimum 6.0 where APIs permit; validate on 6.7.4 / Qt 6.11.1; document install paths |
| RSSI data availability (hardware/driver dependent) | High | Medium | RSSI is an optional runtime capability; basic locking falls back to D-Bus/probing when RSSI is unavailable (Q29) |
| Multi-device policy complexity (advanced AND/OR groups) | Medium | Medium | Default simple any-of (1-of-M); advanced mode opt-in; reject invalid policies with warning and retain last valid config (Q30) |
| Upstream moves on (new commits change `dbus.js` semantics) | Medium | Low | Pin a reference commit in the Phase 0 spec; re-run the delta check before each major phase |
| Multi-seat/multi-user lock semantics via loginctl | Low | Medium | Lock only the active Plasma session for the daemon user (Q21); revalidate session identity before locking |
| Suspend/resume correctness (spurious lock or missed lock across sleep) | Medium | High | Adopt logind `PrepareForSleep` handling (F16); post-resume grace default 30 seconds, disableable (Q13); test suspend/resume in the hardware matrix |
| Flapping at the presence boundary (device flickering in/out of range) | Medium | Medium | Hysteresis thresholds (F9) + transient-failure debounce (F15) + ring-buffer averaging (F11) |
| Lock command re-entrancy / overlapping lock actions | Low | Medium | Command re-entrancy guard (F12) |
| Fail-open on expected errors (adapter down/timeout) leaving the session unlocked | Medium | High | Context-aware failure policy (F13): startup waits/retries with an explicit error; runtime failure while enabled uses the normal away/locking duration then locks; disabled app/device pauses without locking |
| Already-locked session causing repeated lock attempts / probing | Low | Low | Already-locked suppression via `LockedHint` (F17) |
| Snooze/postpone resetting indefinitely, defeating the lock | Medium | Medium | Bounded snooze (F19) with an explicit duration (30-second default, configured duration is the cap), not retry-reset |
| Startup Bluetooth failure causing a false lock during boot | Medium | High | Startup failure waits/retries with an explicit error state, no lock (Q22) |

---

## 12. Security Review Checklist (lock-only verification)

Automatic unlock is permanently out of scope (Q20, §17), so there is **no auto-unlock review gate**. Instead, the checklist verifies the lock-only boundary is enforced by design:

- [ ] No unlock code path exists or is reachable; Bluetooth presence never unlocks a session.
- [ ] Lock goes through `loginctl lock-session` (or equivalent KDE session API); no private unlock backdoors.
- [ ] Lock only the active Plasma session associated with the daemon user (Q21); session identity revalidated immediately before locking.
- [ ] Context-aware failure policy (F13): startup BlueZ/controller unavailable reports an explicit error and waits/retries without locking; runtime failure while enabled uses the normal away/locking duration then locks; disabled app/device pauses without locking; unexpected code bugs do not convert blindly into an absence decision.
- [ ] Startup Bluetooth failure waits/retries with an explicit error state and does not lock (Q22).
- [ ] Reviewed by a second person; findings recorded in the repo.

---

## 13. Packaging / Distribution Considerations (Plasma 6)

The owner decision (Q19, §17) is to target **all three** distribution paths: native CMake, a Nix flake/package plus NixOS module, and a KDE Store plasmoid package.

### 13.1 Native CMake package
- The daemon and client build via a standard CMake project (Qt 6, QDBus, QML).
- Install the daemon as a **systemd user service** (`kde-bluetooth-smartlocker.service`, `--user`), activated at session login.
- Install the plasmoid via `kpackagetool5 --install` (and/or `kpackagetool6` where the distro provides it); uninstall: `kpackagetool5 --remove`.
- Structure: `metadata.json` (Plasma 6 metadata format), `contents/ui/main.qml`, `contents/config/config.qml`, `contents/config/main.xml` (config definitions), icons.

### 13.2 Nix flake/package + NixOS module
- Provide a Nix flake and package so the daemon and client build reproducibly and declaratively.
- Provide a **NixOS module** that generates the system configuration layer and enables the daemon declaratively.
- **NixOS reset marker (Q31-PERSISTENCE/Q32, §17):** the NixOS module writes a generation/configuration marker. The daemon clears runtime device-disable overrides when the marker changes. The marker path/format and permissions are implementation details of the NixOS module.
- Config in the daemon-owned typed declarative format; the NixOS module generates the system/default layer, and the user layer extends/overrides it (Q15/Q27, §17).

### 13.3 KDE Store plasmoid package
- Publish the Plasma client as its own KDE Store plasmoid (owner decision Q5, §17). This is a new standalone project, not a contribution to `kde-bluetooth-lock`.
- Provide a `plasmapkg`/manual tarball fallback.

### 13.4 Dependencies
- Plasma 6 (runtime), Qt 6 (QML, QDBus), `bluez` (D-Bus service), systemd-logind (lock action), systemd (user service).
- No GSettings, no GNOME Shell runtime, no `gnome-extensions`, no `notify-send` primary dependency (native Plasma D-Bus notifications instead, Q17).

---

## 14. Risks Recheck / Out-of-Scope Guardrails

- **No** phone unlock, network presence, biometrics, or remote admin (see §5.4).
- **No** claim that Plasma loads GNOME Shell extensions (it cannot — §3.2).
- **No** upstream action on issue #13; it is reference material only (Q25, §17).
- **No** automatic unlock, in any form (Q20, §17).
- **No** manual lock-screen button in the client (Q23, §17).
- **No** `notify-send`, `hcitool`, or `l2ping` as authoritative presence; no shell-string commands; no root-run polling (see §5.6, §18).

---

## 15. Success Criteria

1. A Plasma 6 user can install the daemon + client, pick their Bluetooth device(s), and the screen locks automatically when the device is absent past the configured away-duration.
2. The lock action uses `loginctl lock-session`; the product is **permanently lock-only** — no automatic unlock exists or is reachable.
3. RSSI proximity locking works in v1 with per-device thresholds, hysteresis, and averaging, and falls back to D-Bus/probing when RSSI is unavailable.
4. Multi-device policy is configurable (default any-of 1-of-M, N-of-M, and advanced opt-in named groups) and is representable in both the Plasma UI and NixOS options.
5. The daemon is a C++/Qt systemd user service with a thin Plasma/QML D-Bus client; no duplicate in-process runtime for v1.
6. Packaging covers native CMake, a Nix flake/package plus NixOS module, and a KDE Store plasmoid; the NixOS reset marker clears runtime device-disable overrides on generation change.
7. The port is documented well enough that a developer unfamiliar with this conversation can build, test, and package it.

---

## 16. Recommended First Milestone & Definition of Done

### Recommended first milestone (M1)
> **"Lock-only C++/Qt daemon + thin Plasma client."**
> Scope: Phase 0 (derive C++/Qt spec from upstream) + Phase 1 (C++/Qt daemon core: BlueZ event-driven monitor, away-duration state machine, RSSI in v1, multi-device policy, `loginctl lock-session`, plus the adopted core features F9–F20) + Phase 2 (thin Plasma/QML client with indicator, per-device/global enable, compact Snooze, configuration overview). RSSI and multi-device policy are **in v1**, not deferred. No auto-unlock, no duplicate in-process runtime.

### Definition of Done (M1)
- [ ] Phase 0 C++/Qt spec derived from upstream as behavioral reference; no upstream code vendored; no upstream issue action taken.
- [ ] Daemon passes its unit tests and the hardware-in-loop test (real smartphone + smartwatch/wearable: out-of-range → lock; back in range → no lock; away-duration respected; RSSI conflicts and missing-RSSI fallback covered).
- [ ] Daemon runs as a systemd user service; client installs via `kpackagetool5 --install`, shows correct indicator states, and locks via `loginctl lock-session`.
- [ ] Settings UI (device, away-duration, enable toggles, snooze, RSSI, policy) persists in the daemon's typed config and applies live.
- [ ] Lock-only is permanent; no auto-unlock code path exists or is reachable.
- [ ] README/user doc covers install, configuration, and the "extension ≠ plasmoid" migration note.

---

## 17. Owner Decisions

The following decisions were finalized by the owner (source of truth: `docs/brainstorms/2026-08-17-kde-bluetooth-smartlocker.md`). They are authoritative and override any earlier plan text. Implementation details are distinguished from owner decisions throughout this document.

| # | Decision | Owner decision |
|---|---|---|
| Q1 | Core language | **C++ (Qt).** The core and BlueZ layer are reimplemented in C++/Qt (QDBus). Upstream JS/GJS is a behavioral reference only, not vendored. |
| Q2 | Fork/vendor vs reimplement | **Reimplement in C++ (QDBus).** Upstream JS is not vendored; it is a behavioral reference. |
| Q3 | `kde-bluetooth-lock` as base | **No.** It is Python, not a base for a C++ core; UX/operational reference only. |
| Q4 | Minimum Plasma version | **Plasma 6 broadly; target 6.0 where APIs permit.** Validate on installed Plasma 6.7.4 / Qt 6.11.1. Raise the minimum only for a concrete API/packaging requirement. Exact minimum confirmed during implementation. |
| Q5 | Publication | **New standalone project** with its own KDE Store plasmoid; not based on or merged into `kde-bluetooth-lock`. |
| Q6 | Auto-unlock security review | **Not applicable.** Automatic unlock is permanently out of scope. |
| Q7 | Architecture | **Standalone C++/Qt daemon as a systemd user service + thin Plasma/QML D-Bus client.** No duplicate in-process runtime for v1. |
| Q8 | Presence signals | **Hybrid:** BlueZ D-Bus events primary, bounded active probing fallback, RSSI as additional proximity evidence with hysteresis. RSSI unavailable must not break basic locking. |
| Q9 | RSSI scope | **Implement directly in v1**, including threshold, hysteresis, and averaging. RSSI remains an optional runtime capability. |
| Q10 | Multi-device policy | **Configurable** in the app and as a NixOS option; support at least any-of and all-of. |
| Q11/Q28 | Snooze | **Configurable, 30-second default; configured duration is the effective cap.** Works with complex multi-device policies. |
| Q12 | Pre-lock notification | **Configurable lead time, fully disableable, default disabled.** Default lead time is the start of the normal away countdown; informational by default with optional Snooze action. |
| Q13 | Suspend/resume | **Reinitialize monitoring after resume; separate configurable post-resume grace, default 30 seconds, disableable.** |
| Q14 | Startup behavior | **Configurable; safe default waits until the device policy has been observed once** before absence-locking. |
| Q15 | Config storage | **Daemon-owned typed declarative config** with system/default and user layers. NixOS generates the baseline; user layer extends/overrides. |
| Q16 | Device selection | **Both** graphical paired-device picker and manual address/adapter config; automatic adapter selection by default. |
| Q17 | Notifications | **Native Plasma D-Bus notifications**, disabled by default; no `notify-send` primary dependency. |
| Q18 | Logging | **Structured Qt logging to journald**, configurable verbosity; no file logging by default. |
| Q19 | Packaging | **Native CMake + Nix flake/package + NixOS module + KDE Store plasmoid package.** |
| Q20 | Auto-unlock | **Never implement.** Bluetooth presence is never an unlock mechanism. |
| Q21 | Session scope | **Lock only the active Plasma session for the daemon user.** |
| Q22 | Failure mode | **Startup:** wait/retry without locking, with an explicit error state. **Runtime while enabled:** use the normal away/locking duration and report the error. **Disabled app/device:** pause without locking. |
| Q23 | Plasma controls | **No manual lock button.** Per-device enable/disable, global enable/disable, compact Snooze, and a configuration-overview button. |
| Q24 | Test hardware | **Smartphone + smartwatch/wearable** hardware-in-loop. |
| Q25 | Issue #13 | **Leave untouched.** Reference material only; no upstream issue action. |
| Q26 | Multi-device model | **Default simple any-of/all-of; advanced opt-in named groups with nested AND/OR.** Same model in Plasma UI and NixOS options. |
| Q27 | Config hierarchy | **Normal mode (user overrides over system baseline) or strict mode (system/NixOS authoritative for enforced fields), configurable per profile.** |
| Q29 | Signal precedence | **RSSI dominant when available.** Below-threshold RSSI can initiate locking despite connection; unavailable RSSI falls back to D-Bus/probing. |
| Q30 | Invalid policy | **Reject with warning, allow retry, retain last valid config.** |
| Q31 | Config merge | **Device lists always extend.** NixOS devices can be runtime-disabled in the UI without deleting their declarative definition. |
| Q31-P | Runtime disable lifetime | **Persist until the next NixOS configuration activation, then reset.** |
| Q32 | NixOS reset detection | **Clear runtime overrides when a NixOS generation/configuration marker changes.** |
| Q33 | Multi-device default | **Any-of by default (1-of-M), plus configurable N-of-M threshold policies.** |
| Q34 | RSSI config | **Per-device RSSI threshold, hysteresis, and averaging.** |
| Q35 | Snooze UI location | **Compact Plasma client.** |
| Q36 | Project identity | **New GitHub project named `kde-bluetooth-smartlocker`.** GitHub owner/organization remains unspecified. |

**Open flag (does not block architecture or implementation):** the GitHub owner/organization for `kde-bluetooth-smartlocker`.

---

## 18. Cross-Project Comparison

This section records the validated, source-level review of three related Bluetooth smart-lock projects. It is the evidence base for the adopted features in §5.5 and the rejected ideas in §5.6. Each project is summarized, then given an adopt/adapt/reject table a developer can act on.

### 18.1 Project A — tiktaalik-dev/blueproximity

Python3/GTK3 fork of the 2007 BlueProximity. Detection uses `hcitool rssi` shell commands plus an RFCOMM socket; no D-Bus, no logind, no systemd, no suspend/resume. Locking runs an arbitrary shell command, defaulting to `gnome-screensaver-command` (GNOME/X11-era).

| Decision | Item | Notes |
|---|---|---|
| **Adopt (Core)** | Hysteresis via separate lock/unlock thresholds | Prevents flapping at the boundary → F9 |
| **Adopt (Core)** | Duration/grace counters before transition | Grace period before lock |
| **Adopt (Core)** | `ignoreFirstTransition` | No lock/unlock action on startup; prevents spurious boot lock → F10 |
| **Adopt (Parity)** | Ring-buffer RSSI averaging | Noise filter → F11 |
| **Adopt (Parity)** | Command re-entrancy guard | Skip if the previous command is still running → F12 |
| **Adopt (Parity)** | Structured state-change logging (syslog/file) | Support/diagnostics |
| **Adopt (Parity)** | Config validation against a spec | Adopted for the config module |
| **Adopt (Optional)** | Multiple device configs (one watcher each) | Feeds the multi-device any-of policy (F20) |
| **Adopt (Optional)** | Pause mode | Feeds bounded snooze (F19) |
| **Adopt (Optional)** | Simulation mode while configuring | Safe tuning without locking yourself out |
| **Reject** | `hcitool`/RFCOMM transport | Obsolete; `hcitool` removed in modern BlueZ |
| **Reject** | `gnome-screensaver-command` locking | No Wayland/loginctl support |
| **Reject** | Polling-only detection | Replaced by D-Bus events |
| **Reject** | Auto-unlock default-on | Security-sensitive (§6.3) |
| **Reject** | `os.popen` shell-string command execution | Injection surface |

### 18.2 Project B — azratul/ble-lock-session

Single-file, zero-dependency Python CLI. Detection is a polling loop plus `bluetoothctl` and a raw L2CAP socket; desktop-independent, defaulting to `loginctl`. No D-Bus subscriptions.

| Decision | Item | Notes |
|---|---|---|
| **Adopt (Core)** | Context-aware failure policy (startup waits/retries with an explicit error; runtime failure while enabled uses the normal away/locking duration; disabled app/device pauses without locking; unexpected bugs do not convert blindly into an absence decision) | Adapted from their fail-closed/fail-open split → F13 |
| **Adopt (Core)** | Fail-open on unexpected bugs (no lock on a code exception) | → F13 |
| **Adopt (Core)** | Multi-signal presence cascade (held-channel → connection state → Classic page → BLE scan) | Not RSSI alone → F14 |
| **Adopt (Core)** | Transient-failure debounce (consecutive misses required before lock) | → F15 |
| **Adopt (Core)** | `loginctl lock-session` as desktop-independent default | Confirms our choice; GNOME's `gnome-screensaver-command` is dead |
| **Adopt (Core)** | Config auto-creation with validated defaults | |
| **Adopt (Core)** | systemd user service with `Restart=on-failure` | Adopted for the daemon service (§13.1) |
| **Adopt (Optional)** | Held-connection keepalive to avoid per-check connect/disconnect notification spam | Only if Classic support is added |
| **Adopt (Optional)** | Release tag ↔ version CI consistency check | |
| **Reject** | Polling loop | Replace with D-Bus events (issue #13 design) |
| **Reject** | Auto-unlock by default | Security-sensitive (§6.3) |
| **Reject** | `unlock_cmd` via `loginctl unlock-session` | Often needs polkit, fails silently |
| **Reject** | Shell-string command execution | Use structured commands |
| **Reject** | Single-device model | Conflicts with multi-device any-of policy (F20) |
| **Reject** | `bluetoothctl` text parsing | Fragile |
| **Gap to fill** | Explicit suspend/resume handling via logind `PrepareForSleep` | This project has none; a correctness gap our port must not inherit → F16 |

### 18.3 Project C — foreignmeloman/kde-bluetooth-lock

Python 3.8+ systemd service. Detection uses `l2ping` polling; locks the active KDE session via `loginctl lock-session`. Root-installed system service.

| Decision | Item | Notes |
|---|---|---|
| **Adopt** | Lock-only v1 behavior (no auto-unlock) | Aligns with our security boundary (§6.3) |
| **Adopt** | Already-locked suppression via `LockedHint` | Avoid repeated lock attempts + unnecessary probing → F17 |
| **Adopt** | Configurable retry budget/interval (grace period before lock) | |
| **Adopt** | Pre-lock threshold notification | Warn the user before locking → F18 |
| **Adopt** | Explicit multi-device policy (any-of: one reachable device = present) | → F20 |
| **Adapt** | Postpone/snooze | Implement as a bounded, explicit snooze duration in the backend state machine, not indefinite retry-reset → F19 |
| **Adapt** | Notification | Use a native Plasma/D-Bus notification adapter, not fragile `notify-send` as target UID |
| **Adapt** | Controller/adapter selection | Via BlueZ D-Bus object paths, not a configurable HCI index |
| **Adapt** | Version-aware session enumeration | Hide behind a typed systemd adapter, not version parsing in core |
| **Reject** | `l2ping` as authoritative presence signal | Active reachability probe, not a BlueZ event; optional diagnostic/fallback only |
| **Reject** | Root-run polling as core architecture | Lock is a session-level action; a user service suffices |
| **Reject** | Hard-coded `seat0` + implicit first-session selection | Multi-seat unsafe |
| **Reject** | Indefinite postpone-by-retry-reset | Replaced by bounded snooze (F19) |
| **Reject** | Un-hardened systemd unit (no Restart/sandboxing) | Use `Restart=on-failure` and sandboxing |
| **Reject** | Minimal config validation (no schema) | Config validation against a spec (A) is adopted instead |
| **Reject** | Ad-hoc root installer with no release packaging | Use standard packaging (§13) |

### 18.4 Cross-cutting synthesis

- All three projects validate the core architecture: a BlueZ D-Bus event-driven core plus a Plasma frontend, `loginctl lock-session` for locking, and lock-only as the default. This project implements the core as a C++/Qt daemon (Q1/Q7, §17).
- The adopted features are folded into the feature list (§5.5) and the phased plan (§8). Core features (F9–F20) land in Phase 1, including RSSI (F11), pre-lock notification (F18), bounded snooze (F19), and multi-device any-of (F20), which are now v1 features per the finalized scope.
- Auto-unlock is permanently out of scope (Q20, §17). All three projects treat it as risky; B and C both document spoofing concerns (§6.3).
- The recommended first milestone (§16) includes RSSI and multi-device policy in v1; the adopted core features fold into the daemon core phase and do not balloon the milestone.

---

*End of plan. This document contains no implementation claims beyond the supplied upstream evidence and no fabricated upstream facts. All upstream statements trace to §2. All cross-project statements trace to §18.*
