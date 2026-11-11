#!/usr/bin/env bash
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
checker="$root/scripts/check-appimage-elf.sh"
fixture="$(mktemp -d)"
trap 'rm -rf "$fixture"' EXIT
mkdir -p "$fixture/nix-rpath/usr/bin" "$fixture/missing-needed/usr/bin"
elf="$(command -v env)"
cp "$elf" "$fixture/nix-rpath/usr/bin/elf-fixture"
cp "$elf" "$fixture/missing-needed/usr/bin/elf-fixture"
chmod u+w "$fixture/nix-rpath/usr/bin/elf-fixture" "$fixture/missing-needed/usr/bin/elf-fixture"

patchelf --set-rpath '/nix/store/example/lib' "$fixture/nix-rpath/usr/bin/elf-fixture"
if bash "$checker" "$fixture/nix-rpath" >"$fixture/nix-rpath.log" 2>&1; then
    printf 'Expected the checker to reject fixture Nix RPATH\n' >&2
    exit 1
fi
if ! grep -Fq 'ELF_NIX_RPATH:' "$fixture/nix-rpath.log"; then
    printf 'Nix RPATH fixture failed for an unexpected reason:\n' >&2
    cat "$fixture/nix-rpath.log" >&2
    exit 1
fi

missing_binary="$fixture/missing-needed/usr/bin/elf-fixture"
patchelf --remove-rpath "$missing_binary"
patchelf --add-needed libsmartlocker-fixture-missing.so "$missing_binary"
if bash "$checker" "$fixture/missing-needed" >"$fixture/missing-needed.log" 2>&1; then
    printf 'Expected the checker to reject fixture unresolved DT_NEEDED\n' >&2
    exit 1
fi
if ! grep -Fq 'ELF_UNRESOLVED_DEPENDENCY: ' "$fixture/missing-needed.log" \
    || ! grep -Fq 'libsmartlocker-fixture-missing.so' "$fixture/missing-needed.log"; then
    printf 'Unresolved dependency fixture failed for an unexpected reason:\n' >&2
    cat "$fixture/missing-needed.log" >&2
    exit 1
fi

host_appdir="$fixture/host-plugin"
host_plugin="$host_appdir/usr/share/kde-bluetooth-smartlocker/plasma-qt6_11"
mkdir -p "$host_plugin"
cp "$elf" "$host_plugin/libsmartlockerqml.so"
chmod u+w "$host_plugin/libsmartlockerqml.so"
printf '6.11.1\n' > "$host_plugin/qt-version"
patchelf --remove-rpath "$host_plugin/libsmartlockerqml.so"
patchelf --add-needed libqt6-host-only-fixture.so "$host_plugin/libsmartlockerqml.so"
bash "$checker" "$host_appdir"

printf 'AppDir ELF fixtures passed (bad RPATH rejected, unresolved dependency rejected, host plugin excluded from runtime ldd)\n'
