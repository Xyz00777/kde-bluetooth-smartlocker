#!/usr/bin/env bash
set -euo pipefail
root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
installer="$root/scripts/install-appimage.sh"
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
export PATH="$tmp/bin:$PATH"
mkdir -p "$tmp/bin"
cat > "$tmp/bin/systemctl" <<'STUB'
#!/usr/bin/env bash
printf '%s\n' "$*" >> "$STATE/systemctl.log"
case "$*" in
    *'cat kde-bluetooth-smartlocker.service'*) exit "${UNIT_EXISTS:-1}" ;;
    *'is-active --quiet'*) exit "${UNIT_ACTIVE:-1}" ;;
    *'enable kde-bluetooth-smartlocker.service'*) [[ "${FAIL_SYSTEMCTL:-}" != enable ]] ;;
    *'start kde-bluetooth-smartlocker.service'*) [[ "${FAIL_SYSTEMCTL:-}" != start ]] ;;
esac
STUB
cat > "$tmp/bin/dms" <<'STUB'
#!/usr/bin/env bash
printf '%s\n' "$*" >> "$STATE/dms.log"
case "$*" in
    'ipc call plugins list') [[ -f "$STATE/dms-enabled" ]] && printf 'bluetoothSmartLocker enabled\n' ;;
    'ipc call plugins enable bluetoothSmartLocker') [[ "${FAIL_DMS:-}" != enable ]] || exit 1; touch "$STATE/dms-enabled" ;;
    'ipc call plugins disable bluetoothSmartLocker') rm -f "$STATE/dms-enabled" ;;
esac
STUB
cat > "$tmp/bin/pgrep" <<'STUB'
#!/usr/bin/env bash
[[ "$*" == '-xo plasmashell' ]] && { printf '4242\n'; exit 0; }
exit 1
STUB
chmod +x "$tmp/bin/systemctl" "$tmp/bin/dms" "$tmp/bin/pgrep"
prepare() {
    export HOME="$tmp/home"
    export XDG_DATA_HOME="$HOME/.local/share" XDG_CONFIG_HOME="$HOME/.config"
    export STATE="$tmp/state"
    rm -rf "$HOME" "$STATE"; mkdir -p "$HOME" "$STATE"
    export APPIMAGE="$tmp/source.AppImage"
    printf payload > "$APPIMAGE"
    export SMARTLOCKER_APPDIR="$tmp/AppDir"
    mkdir -p "$SMARTLOCKER_APPDIR/usr/share/kde-bluetooth-smartlocker/dms-plugin" \
        "$SMARTLOCKER_APPDIR/usr/share/kde-bluetooth-smartlocker/plasma-qt6_11" \
        "$SMARTLOCKER_APPDIR/usr/share/plasma/plasmoids/org.kde.smartlocker"
    printf qml > "$SMARTLOCKER_APPDIR/usr/share/kde-bluetooth-smartlocker/dms-plugin/plugin.json"
    printf qml > "$SMARTLOCKER_APPDIR/usr/share/kde-bluetooth-smartlocker/plasma-qt6_11/qmldir"
    printf '6.11.1\n' > "$SMARTLOCKER_APPDIR/usr/share/kde-bluetooth-smartlocker/plasma-qt6_11/qt-version"
    printf qml > "$SMARTLOCKER_APPDIR/usr/share/plasma/plasmoids/org.kde.smartlocker/metadata.json"
    unset SMARTLOCKER_FAIL_AT FAIL_SYSTEMCTL FAIL_DMS UNIT_EXISTS UNIT_ACTIVE SMARTLOCKER_PROC_MAPS
}
assert_rolled_back() {
    [[ ! -e "$XDG_DATA_HOME/kde-bluetooth-smartlocker/kde-bluetooth-smartlocker.AppImage" ]]
    [[ ! -e "$XDG_CONFIG_HOME/systemd/user/kde-bluetooth-smartlocker.service" ]]
    [[ ! -e "$XDG_CONFIG_HOME/kde-bluetooth-smartlocker/installer.manifest" ]]
    [[ ! -e "$STATE/dms-enabled" ]]
}
prepare
for point in copy unit-enable unit-start dms-enable; do
    prepare
    SMARTLOCKER_FAIL_AT=$point
    export SMARTLOCKER_FAIL_AT
    if bash "$installer" --install --desktop dms >/dev/null 2>&1; then exit 1; fi
    assert_rolled_back
done
prepare
export FAIL_SYSTEMCTL=enable
if bash "$installer" --install --desktop dms >/dev/null 2>&1; then exit 1; fi
assert_rolled_back
prepare
bash "$installer" --install --desktop dms
manifest="$XDG_CONFIG_HOME/kde-bluetooth-smartlocker/installer.manifest"
[[ -f "$manifest" && -f "$XDG_DATA_HOME/quickshell/dms-plugins/bluetoothSmartLocker/plugin.json" ]]
cp "$manifest" "$tmp/manifest-before"
bash "$installer" --install --desktop dms
cmp "$tmp/manifest-before" "$manifest"
touch "$XDG_CONFIG_HOME/kde-bluetooth-smartlocker/service.conf"
bash "$installer" --uninstall
[[ -f "$XDG_CONFIG_HOME/kde-bluetooth-smartlocker/service.conf" ]]
[[ ! -e "$XDG_DATA_HOME/kde-bluetooth-smartlocker/kde-bluetooth-smartlocker.AppImage" ]]
prepare
export UNIT_EXISTS=0
if bash "$installer" --install --desktop dms >/dev/null 2>&1; then exit 1; fi
assert_rolled_back
prepare
unset XDG_CURRENT_DESKTOP XDG_SESSION_DESKTOP
if bash "$installer" --install >/dev/null 2>&1; then exit 1; fi
assert_rolled_back
prepare
mkdir -p "$XDG_DATA_HOME/kde-bluetooth-smartlocker"
touch "$XDG_DATA_HOME/kde-bluetooth-smartlocker/kde-bluetooth-smartlocker.AppImage"
if bash "$installer" --install --desktop dms >/dev/null 2>&1; then exit 1; fi
[[ ! -e "$XDG_CONFIG_HOME/systemd/user/kde-bluetooth-smartlocker.service" ]]
prepare
export UNIT_EXISTS=0 UNIT_ACTIVE=0
if bash "$installer" --install --desktop dms >/dev/null 2>&1; then exit 1; fi
assert_rolled_back
prepare
mkdir -p "$tmp/proc"
printf '7f000000-7f001000 r-xp 0 00:00 0 /nix/store/qt-6.12.0/lib/libQt6Core.so.6.12.0\n' > "$tmp/proc/maps"
export SMARTLOCKER_PROC_MAPS="$tmp/proc/maps"
if bash "$installer" --install --desktop plasma >/dev/null 2>&1; then exit 1; fi
assert_rolled_back
sed 's/6\.12\.0/6.11.1/g' "$tmp/proc/maps" > "$tmp/proc/maps-good"
export SMARTLOCKER_PROC_MAPS="$tmp/proc/maps-good"
mkdir -p "$XDG_DATA_HOME/qt6/qml/org/kde"
printf neighbor > "$XDG_DATA_HOME/qt6/qml/org/kde/unmanaged-neighbor"
mkdir -p "$XDG_CONFIG_HOME/kde-bluetooth-smartlocker"
touch "$XDG_CONFIG_HOME/kde-bluetooth-smartlocker/service.conf"
bash "$installer" --install --desktop plasma
qml_module="$XDG_DATA_HOME/qt6/qml/org/kde/smartlocker"
[[ -f "$qml_module/qmldir" && -f "$qml_module/qt-version" ]]
grep -Fq 'restart or log out and back in' "$XDG_CONFIG_HOME/plasma-workspace/env/smartlocker-qml.sh"
bash "$installer" --uninstall
[[ ! -e "$qml_module" ]]
[[ -f "$XDG_DATA_HOME/qt6/qml/org/kde/unmanaged-neighbor" ]]
[[ -f "$XDG_CONFIG_HOME/kde-bluetooth-smartlocker/service.conf" ]]
[[ ! -e "$XDG_CONFIG_HOME/plasma-workspace/env/smartlocker-qml.sh" ]]
prepare
touch "$STATE/dms-enabled"
bash "$installer" --install --desktop dms
bash "$installer" --uninstall
[[ -f "$STATE/dms-enabled" ]]
printf 'AppImage installer fixtures passed.\n'
