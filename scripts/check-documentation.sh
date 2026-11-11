#!/bin/sh
set -eu

root=$(CDPATH='' cd -- "$(dirname -- "$0")/.." && pwd)
binary=${1:-"$root/build/kde-bluetooth-smartlocker"}
docs=${DOCS_FILE:-"$root/docs/options.md"}
errors=0
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT HUP INT TERM

fail() {
    printf 'missing documentation: %s\n' "$1" >&2
    errors=$((errors + 1))
}

need() {
    need_needle=$1
    need_description=$2
    if ! grep -Fq -- "$need_needle" "$docs"; then fail "$need_description ($need_needle)"; fi
}

code() {
    printf "\`%s\`" "$1"
}

check_row() {
    check_key=$1
    check_description=$2
    shift 2
    check_values=$(printf '%s\034' "$@")
    if ! awk -v key="$check_key" -v values="$check_values" '
        index($0, key) {
            count = split(values, expected, "\034")
            found = 1
            for (i = 1; i <= count; i++) {
                if (!index($0, expected[i])) found = 0
            }
            if (found) matched = 1
        }
        END { exit !matched }
    ' "$docs"; then
        fail "$check_description ($check_key)"
    fi
}

[ -x "$binary" ] || { printf 'daemon binary not executable: %s\n' "$binary" >&2; exit 2; }
[ -f "$docs" ] || { printf 'documentation not found: %s\n' "$docs" >&2; exit 2; }

# Qt's generic --help-all is excluded; parser declarations and validators supply
# each product option's default and type/range contract.
help=$("$binary" --help)
printf '%s\n' "$help" | grep -oE -- '--[a-zA-Z0-9-]+' | sort -u > "$tmp/options"
while IFS= read -r option; do
    [ -n "$option" ] || continue
    [ "$option" = --help-all ] && continue
    default=off
    constraint=flag
    case "$option" in
        --help|--version) ;;
        *)
            key=${option#--}
            source_line=$(grep -F "parser.addOption({\"$key\"" "$root/src/main.cpp" || true)
            if [ -z "$source_line" ]; then
                fail "CLI source declaration for $option"
                continue
            fi
            parsed_default=$(printf '%s\n' "$source_line" | sed -nE 's/.*,[[:space:]]*"[^"]*",[[:space:]]*"[^"]*",[[:space:]]*"([^"]+)"\}\);/\1/p')
            if [ -n "$parsed_default" ]; then default=$parsed_default; fi
            validator=''
            case "$option" in
                --device) default=none; constraint='repeatable string' ;;
                --lock-command) constraint='command string' ;;
                --away-seconds|--resume-grace-seconds)
                    constraint='integer, >= 0'
                    validator='requireNonNegative({"away-seconds", "resume-grace-seconds"})'
                    ;;
                --snooze-seconds|--rssi-hysteresis|--rssi-samples|--minimum-present)
                    constraint='integer, > 0'
                    validator='requirePositive({"snooze-seconds", "minimum-present", "rssi-hysteresis", "rssi-samples"})'
                    ;;
                --rssi-threshold)
                    constraint='integer, -100..0'
                    validator='rssiThreshold < -100 || rssiThreshold > 0'
                    ;;
                --prelock-notify|--lock-when-bluetooth-off) constraint=flag ;;
                *) fail "CLI type/range source mapping for $option"; continue ;;
            esac
            if [ -n "$validator" ] && ! grep -Fq "$validator" "$root/src/main.cpp"; then
                fail "CLI validation source for $option ($validator)"
                continue
            fi
            ;;
    esac
    check_row "$option" "CLI option/default/type row for $option" "$(code "$default")" "$constraint"
done < "$tmp/options"

# Keep each NixOS key, type, and default bound to the same table row.
sed -nE '/options\.services\.kdeBluetoothSmartlocker = \{/,$p' "$root/flake.nix" \
    | sed -nE 's/^[[:space:]]*([A-Za-z][A-Za-z0-9]*) = (lib\.mkOption|lib\.mkEnableOption).*/\1/p' \
    | sort -u > "$tmp/nixos-options"
while IFS= read -r key; do
    [ -n "$key" ] || continue
    if [ "$key" = enable ]; then
        check_row "$(code enable)" 'NixOS option/type and default row for enable' "$(code bool)" "$(code false)"
        continue
    fi
    block=$(awk -v key="$key" '$0 ~ "^[[:space:]]*" key " = lib.mkOption" { capture=1 } capture { print } capture && /};/ { exit }' "$root/flake.nix")
    if [ -z "$block" ]; then
        fail "NixOS source declaration $key"
        continue
    fi
    type=$(printf '%s\n' "$block" | sed -nE 's/.*type = (lib\.types\.[^;]+);.*/\1/p' | head -n 1)
    default=$(printf '%s\n' "$block" | sed -nE 's/.*default = ([^;]+);.*/\1/p' | head -n 1)
    if [ -z "$type" ]; then fail "NixOS type source for $key"; continue; fi
    if [ -z "$default" ]; then fail "NixOS default source for $key"; continue; fi
    check_row "$(code "$key")" "NixOS option/type/default row for $key" "$(code "$type")" "$(code "$default")"
done < "$tmp/nixos-options"

# Derive DMS keys, control kinds, defaults, and slider bounds from QML declarations.
awk '
    /ToggleSetting[[:space:]]*\{/ { kind="bool"; minimum=""; maximum="" }
    /SliderSetting[[:space:]]*\{/ { kind="integer seconds"; minimum=""; maximum="" }
    /settingKey:/ { key=$0; sub(/.*settingKey: "/, "", key); sub(/".*/, "", key) }
    /defaultValue:/ { value=$0; sub(/.*defaultValue: /, "", value); defval=value }
    /minimum:/ { minimum=$0; sub(/.*minimum: /, "", minimum) }
    /maximum:/ { maximum=$0; sub(/.*maximum: /, "", maximum) }
    /}/ && key != "" && defval != "" {
        printf "%s|%s|%s|%s|%s\n", key, defval, kind, minimum, maximum
        key=""; defval=""
    }
' "$root/dms-plugin/SmartLockerSettings.qml" > "$tmp/dms-settings"
while IFS='|' read -r key default setting_type minimum maximum; do
    [ -n "$key" ] || continue
    if [ "$setting_type" = bool ]; then
        check_row "$(code "$key")" "DMS preference/default/type row for $key" "$(code "$default")" bool
    else
        check_row "$(code "$key")" "DMS preference/default/type/range row for $key" "$(code "$default")" "$setting_type" "minimum $(code "$minimum")" "maximum $(code "$maximum")"
    fi
done < "$tmp/dms-settings"

# Check both public invokables and properties from the native bridge.
sed -nE 's/.*Q_INVOKABLE [^ (]+ ([a-zA-Z][a-zA-Z0-9]*)\(.*/\1/p' "$root/src/qml_plugin.cpp" | sort -u > "$tmp/qml-methods"
while IFS= read -r method; do
    [ -n "$method" ] || continue
    if ! grep -Fq "\`$method(" "$docs"; then fail "QML client method ($method)"; fi
done < "$tmp/qml-methods"
sed -nE 's/.*Q_PROPERTY\([^ ]+ ([a-zA-Z][a-zA-Z0-9]*) READ.*/\1/p' "$root/src/qml_plugin.cpp" | sort -u > "$tmp/qml-properties"
while IFS= read -r property; do
    [ -n "$property" ] || continue
    if ! grep -Fq "\`$property\`" "$docs"; then fail "QML client property ($property)"; fi
done < "$tmp/qml-properties"

# Every directive in the packaged unit template must appear in documentation.
sed -nE '/^\[/d; /^[[:space:]]*#/d; /^[[:space:]]*$/d; s/^([^=]+)=.*/\1/p' "$root/systemd/kde-bluetooth-smartlocker.service.in" > "$tmp/service-directives"
while IFS= read -r directive; do
    [ -n "$directive" ] || continue
    if ! grep -Fq "$directive" "$docs"; then fail "systemd directive ($directive)"; fi
done < "$tmp/service-directives"

need 'SMARTLOCKER_DEVICES' 'environment variable'
need '%h/.config/kde-bluetooth-smartlocker/service.conf' 'service config path'
need 'XDG_CONFIG_HOME' 'QSettings XDG configuration path'
need 'QSettings' 'QSettings persistence description'

# Individual requirements make omissions diagnosable rather than keyword-only.
need 'session D-Bus' 'AppImage host session D-Bus requirement'
need 'BlueZ on the system D-Bus' 'AppImage host BlueZ requirement'
need 'logind support' 'AppImage host logind requirement'
need 'loginctl lock-session' 'AppImage host lock command requirement'
need 'graphical session/lock screen' 'AppImage host graphical-session requirement'
need "host's Bluetooth controller" 'AppImage host Bluetooth controller requirement'
need 'D-Bus daemon' 'AppImage host D-Bus daemon requirement'
need 'Plasma session' 'AppImage host Plasma requirement'
need 'DMS runtime' 'AppImage host DMS requirement'
need 'Qt 6.11.1' 'host Plasma Qt 6.11.1 support'
need 'Qt 6.12 runtime' 'separate AppImage Qt 6.12 runtime'
need '--install [--desktop auto|dms|plasma]' 'AppImage install command and desktop choices'
need '--uninstall' 'AppImage uninstall command'
need 'CI / required-checks' 'required branch-protection check name'
need 'branch protection' 'branch-protection setup instructions'
need 'strictly higher SemVer' 'release version policy'
need 'CHANGELOG.md' 'release changelog policy'
need 'Documentation-only and CI-only changes' 'docs and CI release exemption'
need 'restart Plasma or log out' 'Plasma QML path restart requirement'
need 'pure QML' 'DMS pure-QML runtime distinction'
need 'busctl' 'DMS host busctl requirement'

if [ "$errors" -gt 0 ]; then
    printf 'documentation check failed: %d missing item(s)\n' "$errors" >&2
    exit 1
fi
printf 'documentation check passed: compiled CLI, NixOS options, DMS preferences, QML API, service directives, config paths, and AppImage host/Qt requirements are documented\n'
