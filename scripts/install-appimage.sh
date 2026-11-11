#!/usr/bin/env bash
set -euo pipefail

die() { printf 'SmartLocker installer: %s\n' "$*" >&2; exit 1; }
usage() { printf 'Usage: %s --install [--desktop auto|dms|plasma] | --uninstall\n' "$0" >&2; }

appdir=${SMARTLOCKER_APPDIR:?missing AppDir}
appimage=${APPIMAGE:-}
desktop=auto
action=
while (($#)); do
    case "$1" in
        --install) [[ -z "$action" ]] || { usage; exit 2; }; action=install ;;
        --uninstall) [[ -z "$action" ]] || { usage; exit 2; }; action=uninstall ;;
        --desktop) (($# >= 2)) || { usage; exit 2; }; desktop=$2; shift ;;
        *) usage; exit 2 ;;
    esac
    shift
done
[[ -n "$action" ]] || { usage; exit 2; }
[[ "$action" != uninstall || "$desktop" == auto ]] || die '--desktop is valid only with --install'

data_home=${XDG_DATA_HOME:-$HOME/.local/share}
config_home=${XDG_CONFIG_HOME:-$HOME/.config}
manifest="$config_home/kde-bluetooth-smartlocker/installer.manifest"
unit="$config_home/systemd/user/kde-bluetooth-smartlocker.service"
app_target="$data_home/kde-bluetooth-smartlocker/kde-bluetooth-smartlocker.AppImage"
dms_target="$data_home/quickshell/dms-plugins/bluetoothSmartLocker"
plasma_target="$data_home/plasma/plasmoids/org.kde.smartlocker"
qml_target="$data_home/qt6/qml/org/kde/smartlocker"
env_target="$config_home/plasma-workspace/env/smartlocker-qml.sh"

if [[ "$action" == uninstall ]]; then
    [[ -f "$manifest" ]] || die 'no ownership manifest; nothing managed to uninstall'
    owned_dirs=()
    # The manifest is generated as shell-safe tab-separated absolute paths; reject malformed input.
    while IFS=$'\t' read -r kind path; do
        [[ "$kind" == desktop ]] && continue
        if [[ "$kind" == dms ]]; then
            [[ "$path" == enabled ]] && dms ipc call plugins disable bluetoothSmartLocker || true
            continue
        fi
        [[ "$path" == "$HOME/"* ]] || die 'invalid ownership manifest path'
        case "$kind" in
            file) rm -f -- "$path" ;;
            tree) rm -rf -- "$path" ;;
            dir) owned_dirs+=("$path") ;;
            unit) systemctl --user disable --now kde-bluetooth-smartlocker.service || true; rm -f -- "$path" ;;
            *) die 'invalid ownership manifest entry' ;;
        esac
    done < "$manifest"
    rm -f -- "$manifest"
    for ((index=${#owned_dirs[@]}-1; index>=0; index--)); do rmdir -- "${owned_dirs[index]}" 2>/dev/null || true; done
    exit 0
fi

case "$desktop" in
    auto)
        if [[ "${XDG_CURRENT_DESKTOP:-}:${XDG_SESSION_DESKTOP:-}" =~ (DMS|DankMaterialShell) ]]; then desktop=dms
        elif [[ "${XDG_CURRENT_DESKTOP:-}:${XDG_SESSION_DESKTOP:-}" =~ KDE|Plasma ]]; then desktop=plasma
        elif pgrep -x plasmashell >/dev/null 2>&1; then desktop=plasma
        elif pgrep -f 'quickshell.*dms' >/dev/null 2>&1; then desktop=dms
        else die 'unable to identify a supported desktop environment'; fi ;;
    dms|plasma) ;;
    *) die 'desktop must be auto, dms, or plasma' ;;
esac

[[ -n "$appimage" && -f "$appimage" ]] || die 'APPIMAGE does not identify the running AppImage'
[[ -x "$(command -v systemctl || true)" ]] || die 'systemctl is required'
if [[ -f "$manifest" ]]; then
    if grep -Fqx $'desktop\t'"$desktop" "$manifest" && [[ -f "$app_target" && -f "$unit" ]]; then
        printf 'SmartLocker is already installed for %s.\n' "$desktop"
        exit 0
    fi
    die 'existing ownership manifest does not match this installation; use --uninstall first'
fi

# Refuse NixOS or any other competing unit, regardless of active state.
if systemctl --user is-active --quiet kde-bluetooth-smartlocker.service 2>/dev/null; then die 'a competing SmartLocker service is active'; fi
if systemctl --user cat kde-bluetooth-smartlocker.service >/dev/null 2>&1; then die 'a pre-existing SmartLocker unit is unmanaged'; fi
[[ ! -e "$unit" ]] || die 'unmanaged user service unit exists'
[[ ! -e "$app_target" ]] || die "unmanaged destination exists: $app_target"
if [[ "$desktop" == dms ]]; then
    [[ ! -e "$dms_target" ]] || die "unmanaged DMS plugin path exists: $dms_target"
    command -v dms >/dev/null || die 'DMS command unavailable'
elif [[ "$desktop" == plasma ]]; then
    [[ ! -e "$plasma_target" && ! -e "$qml_target" && ! -e "$env_target" ]] || die 'unmanaged Plasma integration path exists'
    pid=$(pgrep -xo plasmashell) || die 'plasmashell is not running'
    qt_maps=${SMARTLOCKER_PROC_MAPS:-/proc/$pid/maps}
    qtcore=$(awk '/libQt6Core\.so/ { print $NF; exit }' "$qt_maps")
    [[ -n "$qtcore" ]] || die 'cannot determine host Plasma Qt library'
    qtminor=$(sed -nE 's/.*libQt6Core\.so\.([0-9]+\.[0-9]+\.[0-9]+).*/\1/p' <<<"$qtcore")
    [[ "$qtminor" == 6.11.1 ]] || die "host Plasma Qt must be 6.11.1 (found ${qtminor:-unknown})"
fi

plugin_enabled=0
if [[ "$desktop" == dms ]] && dms ipc call plugins list 2>/dev/null | grep -q 'bluetoothSmartLocker.*enabled'; then plugin_enabled=1; fi
tmp_manifest=
created=()
unit_installed=0
unit_enabled=0
dms_enabled=0
rollback() {
    status=$?
    trap - EXIT
    if ((status != 0)); then
        if ((unit_enabled)); then systemctl --user disable --now kde-bluetooth-smartlocker.service >/dev/null 2>&1 || true
        elif ((unit_installed)); then systemctl --user stop kde-bluetooth-smartlocker.service >/dev/null 2>&1 || true; fi
        if ((dms_enabled && !plugin_enabled)); then dms ipc call plugins disable bluetoothSmartLocker >/dev/null 2>&1 || true; fi
        for path in "${created[@]}"; do rm -rf -- "$path"; done
        [[ -z "$tmp_manifest" ]] || rm -f -- "$tmp_manifest"
        for path in "${created_dirs[@]:-}"; do rmdir -- "$path" 2>/dev/null || true; done
    fi
    exit "$status"
}
created_dirs=()
trap rollback EXIT
own_file() { created+=("$1"); printf 'file\t%s\n' "$1" >> "$tmp_manifest"; }
mkdir_owned() {
    local cursor=$1 index
    local -a missing=()
    while [[ ! -d "$cursor" ]]; do missing+=("$cursor"); cursor=$(dirname "$cursor"); done
    for ((index=${#missing[@]}-1; index>=0; index--)); do
        mkdir -- "${missing[index]}"
        created_dirs+=("${missing[index]}")
        printf 'dir\t%s\n' "${missing[index]}" >> "$tmp_manifest"
    done
}

manifest_dir=$(dirname "$manifest")
manifest_cursor=$manifest_dir
manifest_missing=()
while [[ ! -d "$manifest_cursor" ]]; do manifest_missing+=("$manifest_cursor"); manifest_cursor=$(dirname "$manifest_cursor"); done
for ((index=${#manifest_missing[@]}-1; index>=0; index--)); do created_dirs+=("${manifest_missing[index]}"); done
mkdir -p "$manifest_dir"
tmp_manifest=$(mktemp "${manifest}.XXXXXX")
for ((index=${#manifest_missing[@]}-1; index>=0; index--)); do
    printf 'dir\t%s\n' "${manifest_missing[index]}" >> "$tmp_manifest"
done
mkdir_owned "$(dirname "$app_target")"
printf 'desktop\t%s\n' "$desktop" >> "$tmp_manifest"
cp -- "$appimage" "$app_target"; own_file "$app_target"
[[ "${SMARTLOCKER_FAIL_AT:-}" != copy ]] || die 'injected failure after AppImage copy'
mkdir_owned "$(dirname "$unit")"
cat > "$unit" <<UNIT
[Unit]
Description=KDE Bluetooth SmartLocker (AppImage)
After=graphical-session.target
PartOf=graphical-session.target
[Service]
Type=simple
ExecStart=$app_target
EnvironmentFile=-%h/.config/kde-bluetooth-smartlocker/service.conf
Restart=on-failure
RestartSec=2
[Install]
WantedBy=graphical-session.target
UNIT
created+=("$unit"); printf 'unit\t%s\n' "$unit" >> "$tmp_manifest"; unit_installed=1
systemctl --user daemon-reload
systemctl --user enable kde-bluetooth-smartlocker.service; unit_enabled=1
[[ "${SMARTLOCKER_FAIL_AT:-}" != unit-enable ]] || die 'injected failure after unit enable'
systemctl --user start kde-bluetooth-smartlocker.service
[[ "${SMARTLOCKER_FAIL_AT:-}" != unit-start ]] || die 'injected failure after unit start'

if [[ "$desktop" == dms ]]; then
    mkdir_owned "$(dirname "$dms_target")"
    cp -a "$appdir/usr/share/kde-bluetooth-smartlocker/dms-plugin" "$dms_target"
    created+=("$dms_target")
    printf 'tree\t%s\n' "$dms_target" >> "$tmp_manifest"
    dms ipc call plugins enable bluetoothSmartLocker
    ((plugin_enabled)) || { dms_enabled=1; printf 'dms\tenabled\n' >> "$tmp_manifest"; }
    [[ "${SMARTLOCKER_FAIL_AT:-}" != dms-enable ]] || die 'injected failure after DMS enable'
else
    mkdir_owned "$(dirname "$plasma_target")"
    cp -a "$appdir/usr/share/plasma/plasmoids/org.kde.smartlocker" "$plasma_target"
    created+=("$plasma_target")
    printf 'tree\t%s\n' "$plasma_target" >> "$tmp_manifest"
    mkdir_owned "$qml_target"
    cp -a "$appdir/usr/share/kde-bluetooth-smartlocker/plasma-qt6_11/." "$qml_target/"
    for file in "$qml_target"/*; do [[ ! -f "$file" ]] || own_file "$file"; done
    mkdir_owned "$(dirname "$env_target")"
    printf "export QML_IMPORT_PATH=\"%s/qt6/qml\${QML_IMPORT_PATH:+:\$QML_IMPORT_PATH}\"\n" "$data_home" > "$env_target"
    own_file "$env_target"
    printf '%s\n' 'Plasma must restart or log out and back in for this QML import path to take effect.' >> "$env_target"
fi
mv -- "$tmp_manifest" "$manifest"
trap - EXIT
printf 'Installed SmartLocker for %s.\n' "$desktop"
