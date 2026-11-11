#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 1 || ! -d "$1" ]]; then
    printf 'Usage: %s APPDIR\n' "$0" >&2
    exit 2
fi

appdir="$(cd "$1" && pwd)"
host_plugin="$appdir/usr/share/kde-bluetooth-smartlocker/plasma-qt6_11"
if [[ -d "$host_plugin" && "$host_plugin" == "$appdir/usr/qml/"* ]]; then
    printf 'ELF_HOST_PLUGIN_IN_RUNTIME_TREE: %s\n' "$host_plugin" >&2
    exit 1
fi

runtime_library_path="$appdir/usr/lib:$appdir/usr/lib/glibc:$appdir/usr/lib/x86_64-linux-gnu:$appdir/usr/plugins${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"

while IFS= read -r -d '' elf; do
    file -b "$elf" | grep -Eq '^ELF .* (shared object|pie executable|executable)' || continue

    rpath="$(patchelf --print-rpath "$elf")"
    if grep -Fq '/nix/store/' <<<"$rpath"; then
        printf 'ELF_NIX_RPATH: %s has RPATH/RUNPATH %s\n' "$elf" "$rpath" >&2
        exit 1
    fi

    needed="$(patchelf --print-needed "$elf")"
    if grep -Fq '/nix/store/' <<<"$needed"; then
        printf 'ELF_NIX_NEEDED: %s has DT_NEEDED entry %s\n' "$elf" "$needed" >&2
        exit 1
    fi

    case "$elf" in
        "$host_plugin/"*) continue ;;
    esac

    if ! dependencies="$(LD_LIBRARY_PATH="$runtime_library_path" ldd "$elf" 2>&1)"; then
        printf 'ELF_LDD_FAILED: %s\n%s\n' "$elf" "$dependencies" >&2
        exit 1
    fi
    if grep -Eq 'not found|version .* not found' <<<"$dependencies"; then
        printf 'ELF_UNRESOLVED_DEPENDENCY: %s\n%s\n' "$elf" "$dependencies" >&2
        exit 1
    fi
done < <(find "$appdir" -type f -print0)
