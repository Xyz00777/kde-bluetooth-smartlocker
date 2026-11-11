#!/usr/bin/env bash
set -euo pipefail

root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
checker="$root/scripts/check-documentation.sh"
binary=${1:-"$root/build/kde-bluetooth-smartlocker"}
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

run_missing_fixture() {
    local name=$1 needle=$2 expected=$3
    cp "$root/docs/options.md" "$tmp/$name.md"
    python3 - "$tmp/$name.md" "$needle" <<'PY'
from pathlib import Path
import sys

path = Path(sys.argv[1])
needle = sys.argv[2]
text = path.read_text(encoding="utf-8")
if needle not in text:
    raise SystemExit(f"fixture source does not contain omission target: {needle}")
path.write_text(text.replace(needle, ""), encoding="utf-8")
PY
    if DOCS_FILE="$tmp/$name.md" "$checker" "$binary" >"$tmp/$name.out" 2>&1; then
        printf 'fixture unexpectedly passed: %s\n' "$name" >&2
        exit 1
    fi
    if ! grep -Fq "$expected" "$tmp/$name.out"; then
        printf 'fixture %s failed without expected diagnostic: %s\n' "$name" "$expected" >&2
        python3 - "$tmp/$name.out" <<'PY' >&2
from pathlib import Path
import sys
print(Path(sys.argv[1]).read_text(), end="")
PY
        exit 1
    fi
    printf 'negative fixture passed: %s (%s)\n' "$name" "$expected"
}

run_missing_fixture cli-option "\`--lock-when-bluetooth-off\`" 'CLI option/default/type row for --lock-when-bluetooth-off'
run_missing_fixture cli-default "\`30\` | integer, >= 0" 'CLI option/default/type row for --away-seconds'
run_missing_fixture cli-type 'integer, >= 0' 'CLI option/default/type row for --away-seconds'
run_missing_fixture cli-range 'integer, -100..0' 'CLI option/default/type row for --rssi-threshold'
run_missing_fixture environment-variable 'SMARTLOCKER_DEVICES' 'environment variable'
run_missing_fixture systemd-directive 'ProtectSystem' 'systemd directive (ProtectSystem)'
run_missing_fixture nixos-option "\`awaySeconds\`" 'NixOS option/type/default row for awaySeconds'
run_missing_fixture nixos-type "\`lib.types.ints.unsigned\`" 'NixOS option/type/default row for awaySeconds'
run_missing_fixture nixos-default "\`30\` | Absence duration before lock request." 'NixOS option/type/default row for awaySeconds'
run_missing_fixture dms-preference "\`showLabel\`" 'DMS preference/default/type row for showLabel'
run_missing_fixture dms-default "\`true\` | Show the short state label" 'DMS preference/default/type row for showLabel'
run_missing_fixture qml-api '`refreshDevices(' 'QML client method (refreshDevices)'
run_missing_fixture service-config-path '%h/.config/kde-bluetooth-smartlocker/service.conf' 'service config path'
run_missing_fixture appimage-session-dbus 'session D-Bus' 'AppImage host session D-Bus requirement'
run_missing_fixture appimage-bluez 'BlueZ on the system D-Bus' 'AppImage host BlueZ requirement'
run_missing_fixture appimage-logind 'logind support' 'AppImage host logind requirement'
run_missing_fixture appimage-lock-command 'loginctl lock-session' 'AppImage host lock command requirement'
run_missing_fixture appimage-graphical-session 'graphical session/lock screen' 'AppImage host graphical-session requirement'
run_missing_fixture appimage-controller "host's Bluetooth controller" 'AppImage host Bluetooth controller requirement'
run_missing_fixture appimage-dbus-daemon 'D-Bus daemon' 'AppImage host D-Bus daemon requirement'
run_missing_fixture appimage-plasma 'Plasma session' 'AppImage host Plasma requirement'
run_missing_fixture appimage-dms 'DMS runtime' 'AppImage host DMS requirement'
run_missing_fixture appimage-host-qt 'Qt 6.11.1' 'host Plasma Qt 6.11.1 support'
run_missing_fixture appimage-runtime-qt 'Qt 6.12 runtime' 'separate AppImage Qt 6.12 runtime'
run_missing_fixture appimage-install '--install [--desktop auto|dms|plasma]' 'AppImage install command and desktop choices'
run_missing_fixture appimage-uninstall '--uninstall' 'AppImage uninstall command'
run_missing_fixture required-check 'CI / required-checks' 'required branch-protection check name'
run_missing_fixture branch-protection 'branch protection' 'branch-protection setup instructions'
run_missing_fixture release-semver 'strictly higher SemVer' 'release version policy'
run_missing_fixture release-changelog 'CHANGELOG.md' 'release changelog policy'
run_missing_fixture release-exemption 'Documentation-only and CI-only changes' 'docs and CI release exemption'
run_missing_fixture plasma-restart 'restart Plasma or log out' 'Plasma QML path restart requirement'
run_missing_fixture appimage-dms-runtime 'pure QML' 'DMS pure-QML runtime distinction'
run_missing_fixture appimage-busctl 'busctl' 'DMS host busctl requirement'
printf 'all documentation omission fixtures passed\n'
