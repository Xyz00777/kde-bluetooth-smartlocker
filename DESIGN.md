# KDE Bluetooth SmartLocker Design System

## 0. Research Log

- Existing UI extraction: preserved the current native Qt operational-panel style; no external brand reference or browser research was needed for this small Plasma surface.
- Skipped lanes: browser/Lighthouse research — this is a native QML plasmoid, not a web surface.

## 1. Atmosphere & Identity

Quiet and utilitarian. The panel should feel like a trustworthy system control: compact, legible, and explicit about lock safety. The signature is state-first information with controls placed directly beside the device they affect.

## 2. Color

The plasmoid uses Plasma's native palette and control styling. No application-owned colors are introduced; semantic status colors, focus treatment, and disabled contrast come from the active KDE theme.

## 3. Typography

Native Plasma/system typography. Headings use the platform default bold label treatment; body and caption text use native control sizes. No custom font family or arbitrary font sizes are introduced.

## 4. Spacing & Layout

Spacing derives from a 4 px base unit: 8 px between controls, 12 px around a device policy group, and 16 px between major sections. The full representation is a single vertical stack that reflows naturally in narrow panel popups.

## 5. Components

### DevicePolicyRow
- **Structure**: device path label, per-device enable switch, RSSI threshold spin box.
- **Variants**: enabled, disabled, unavailable device list.
- **Spacing**: 8 px internal spacing; 12 px device-group separation.
- **States**: default, focused, disabled, empty list.
- **Accessibility**: native keyboard navigation; explicit labels for the switch and RSSI editor; no color-only state.
- **Motion**: none; policy changes are immediate and security-relevant.
- **Layout**: vertical stack with a compact nested row; long device paths elide rather than overflow.

### StatusPanel
- **Structure**: title, current state label, global enable switch, device policy list, snooze and refresh actions.
- **Variants**: configured list and empty configuration guidance.
- **Spacing**: 8 px stack gap.
- **States**: normal, disabled, unavailable, empty.
- **Accessibility**: state is rendered as text; all actions are native controls.
- **Motion**: none.
- **Layout**: single vertical stack.

## 6. Motion & Interaction

No custom motion. Native Qt/Plasma focus, pressed, and disabled states remain enabled. Policy edits call the daemon immediately and do not pretend that a local animation confirms persistence.

## 7. Depth & Surface

Native Plasma controls and surfaces only. The plasmoid does not add borders, shadows, gradients, or custom backgrounds.

## 8. Accessibility Constraints & Accepted Debt

### Constraints

- Target native KDE accessibility behavior and keyboard reachability.
- Keep all primary body text at native readable sizes.
- Every setting has a visible text label and a native focus indicator.
- Long BlueZ object paths must elide instead of causing horizontal overflow.

### Accepted Debt

| Item | Location | Why accepted | Owner / Exit |
|------|----------|--------------|--------------|
| Rendered Plasma screenshot QA is unavailable in this environment | `plasmoid/contents/ui` | No plasmoidviewer/QML runtime is installed here | Run visual QA on a Plasma workstation before release |
