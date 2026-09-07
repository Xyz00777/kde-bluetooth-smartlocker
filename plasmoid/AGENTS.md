# plasmoid/ — Plasma 6 UI Package

## OVERVIEW
The Plasma 6 plasmoid package: QML UI plus the native QML plugin module (`org.kde.smartlocker`). Loaded by Plasma, not by the daemon. Talks to the daemon only over session D-Bus via `SmartLockerClient`.

## STRUCTURE
```
plasmoid/
├── metadata.json              # package metadata; "Version" must match 3 other files
└── contents/ui/
    ├── main.qml               # PlasmoidItem root: status, global switch, device list, snooze
    ├── DevicePolicyRow.qml    # per-device: enable switch + RSSI threshold spin box
    └── org/kde/smartlocker/   # QML module dir
        └── qmldir             # declares module + native smartlockerqml plugin
```

## WHERE TO LOOK
| Task | File | Notes |
|------|------|-------|
| Plasmoid root / state | `contents/ui/main.qml` | `PlasmoidItem`, `SmartLockerClient` instance |
| Per-device controls | `contents/ui/DevicePolicyRow.qml` | switch + RSSI spin box, `Connections` to settings |
| QML module registration | `contents/ui/org/kde/smartlocker/qmldir` | native plugin `smartlockerqml` |
| Version | `metadata.json` | must stay in sync with CMake/flake/main.cpp |

## CONVENTIONS
- **Native Plasma controls only** — no custom colors, fonts, borders, shadows, or gradients (see `DESIGN.md`). Styling comes from the active KDE theme.
- **No custom motion/animation** — policy edits call the daemon immediately; no local animation pretends persistence.
- **State-first UI**: state rendered as text; all actions are native controls with `Accessible.name`.
- Long BlueZ paths **elide** (`Text.ElideMiddle`), never overflow.
- UI is a **thin D-Bus client** — never a duplicate in-process runtime.

## ANTI-PATTERNS
- **Never delete declarative devices through the UI** — device lists only extend.
- **Never add a manual lock-screen button.**
- **Never call Plasma-private APIs to lock** — the daemon uses `loginctl`.
- **Never add color-only state** — accessibility requires text/control state.

## UNIQUE STYLES
- `main.qml` uses `compactRepresentation` (state label) + `fullRepresentation` (controls).
- `DevicePolicyRow` re-syncs switch/spin-box from `onSettingsChanged` so external changes reflect.
- `SmartLockerClient` reports `"unavailable"` when the daemon service is not registered.
