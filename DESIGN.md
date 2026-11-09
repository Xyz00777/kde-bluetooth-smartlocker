# KDE Bluetooth SmartLocker Design System

## 0. Research Log

- Existing UI extraction: the previous panel used QtQuick.Controls with hard-coded colors, a text-only compact representation, and no empty/error treatment. It was replaced with Plasma 6 native components and semantic theme colors.
- References: Plasma 6 KDE Connect applet (compact icon + MouseArea, `PlasmaExtras.Representation`, `PlaceholderMessage`), Plasma HIG, and the installed PlasmaComponents3/Kirigami APIs.
- Skipped lanes: browser/Lighthouse research — this is a native QML plasmoid, not a web surface.

## 1. Atmosphere & Identity

Quiet and utilitarian. The panel shows a single Bluetooth glyph with a small semantic state dot; the popup reads as a trustworthy system control: current state first, then the global monitoring switch, then the devices the policy actually watches, then the actions. The signature is "state-first, controls next to the thing they affect."

## 2. Color

The applet introduces no application-owned colors. State is expressed with the active Plasma/Kirigami palette:

- Monitoring: `Kirigami.Theme.positiveTextColor`
- Away / Snoozed: `Kirigami.Theme.neutralTextColor`
- Locked: `Kirigami.Theme.highlightColor`
- Error / Unavailable: `Kirigami.Theme.negativeTextColor`
- Disabled / Starting: `Kirigami.Theme.disabledTextColor`
- Count badge: `Kirigami.Theme.alternateBackgroundColor` with `textColor`
- Compact state dot border: `Kirigami.Theme.backgroundColor` so it reads on any panel background

State is always also rendered as text; color is never the only signal.

## 3. Typography

Native Plasma/system typography. `PlasmaExtras.Heading` (levels 3/4) for the title and section heading, `PlasmaExtras.DescriptiveLabel` for body/caption copy, `PlasmaComponents3.Label` for control labels, and `Kirigami.Theme.smallFont` for the MAC address and the threshold caption. No custom font family or arbitrary point sizes are introduced.

## 4. Spacing & Layout

Spacing comes from `Kirigami.Units`: `smallSpacing` inside a row, `largeSpacing` between sections, and `gridUnit` for the popup width (`18`–`20` grid units). The full representation is one vertical stack:

1. Header: Bluetooth glyph, title, state label, one-sentence state description.
2. Error banner (only when the daemon reports one).
3. Global monitoring switch.
4. "Watched devices" heading with a count badge.
5. Empty placeholder or a scrollable device list capped at 320 px.
6. Snooze / Refresh actions and the lock-only footer note.

Long device names elide right; addresses elide middle. The layout reflows at narrow panel-popup widths.

## 5. Components

### CompactRepresentation
- **Structure**: `Kirigami.Icon` (`bluetooth`) with a small semantic state dot at the bottom-right, plus a keyboard-focus ring.
- **Interaction**: a full-size `MouseArea` toggles `root.expanded`; Space/Return/Enter do the same. `Accessible.name` and `Accessible.description` carry the state and device count.
- **Panel status**: `Plasmoid.status` is Passive while monitoring, Active while away/snoozed, and NeedsAttention on error/unavailable.

### StatusPanel
- **Structure**: header row (icon, title, state label, descriptive subtitle), optional `Kirigami.InlineMessage` error with a Dismiss action, global switch, device section.
- **Variants**: starting, monitoring, away, snoozed, locked, disabled, error, unavailable, and empty device list.
- **Accessibility**: state is text; all controls have `Accessible.name`/`Accessible.description`.

### DevicePolicyRow
- **Structure**: `PlasmaComponents3.Frame` card with a bold display name (or "Unknown device"), an elided MAC address, a Watch switch, independent BLE and connection scan switches, and separate BLE and connection RSSI threshold sliders from -100 to 0 dBm. Each track carries its source's live RSSI marker; slider handles remain their respective configured thresholds.
- **Signal state**: show each source's latest fresh RSSI value next to its threshold; hide that source's marker and show "No recent signal" when no fresh sample is available. Markers use the slider highlight color, and adjacent readings use semantic text color for threshold status.
- **States**: watched/unwatched and BLE-on/BLE-off (the threshold block dims), daemon unavailable, long name/MAC.
- **Motion**: the marker for the matching source retargets over 150 ms with OutCubic easing on fresh RSSI updates; neither threshold handle moves. A 300 ms timer coalesces each threshold slider independently.
- **Sync**: `Component.onCompleted` and `onSettingsChanged` re-read the client's cached name, scan switches, Watch setting, both thresholds, and source-specific RSSI values; an in-progress drag or pending commit is not overwritten.

### EmptyState
- **Structure**: `PlasmaExtras.PlaceholderMessage` with the Bluetooth icon, "No paired or trusted Bluetooth devices", and an explanation of the auto-watch behavior.

### Footer
- **Structure**: Snooze (`chronometer`) and Refresh (`view-refresh`) buttons; the snooze button is disabled while disabled/unavailable/already snoozed.
- **Note**: a centered "Lock-only · Bluetooth presence never unlocks." line.

## 6. Motion & Interaction

Native Plasma focus, pressed, hover, and disabled states remain enabled. Policy edits call the daemon immediately; threshold slider commits are coalesced to avoid a D-Bus call per pixel. The independent RSSI marker retargets over 150 ms with an OutCubic easing as fresh samples arrive; it never moves the threshold handle. Presence is accepted from either enabled source. Disabling connection scanning makes the BLE threshold the active range boundary.

## 7. Depth & Surface

Native Plasma surfaces only: `PlasmaExtras.Representation` for the popup body, `PlasmaComponents3.Frame` for device cards, and `Kirigami.InlineMessage` for errors. The applet does not add borders, shadows, gradients, or custom backgrounds. `Plasmoid.backgroundHints` is `NoBackground` in a panel and `DefaultBackground` elsewhere.

## 8. Accessibility Constraints & Accepted Debt

### Constraints

- Target native KDE accessibility and keyboard reachability.
- Every setting has a visible text label and a native focus indicator.
- Compact representation is keyboard-activatable and exposes the state through `Accessible.name`.
- Long device identifiers elide instead of causing horizontal overflow.
- Empty, unavailable, locked, snoozed, disabled, error, and many-device states are designed, not accidental.

### Accepted Debt

| Item | Location | Why accepted | Owner / Exit |
|------|----------|--------------|--------------|
| Rendered QA of the real panel popup and native D-Bus client | `plasmoid/contents/ui` | The native QML plugin in the test container is built against a different Qt minor than the available plasmoidviewer; layout QA used plasmoidviewer with a mocked `SmartLockerClient`. | Run the applet in a real Plasma 6 session before release. |
