#!/usr/bin/env bash
set -euo pipefail

runner="$(dirname "$0")/qml-plugin-smoke.sh"
tmpdir="$(mktemp -d)"
cleanup() {
    rm -rf -- "$tmpdir"
}
trap cleanup EXIT

create_qml() {
    local executable="$1"
    local version="$2"
    mkdir -p "$(dirname "$executable")"
    printf '#!%s\n' "$BASH" > "$executable"
    cat >> "$executable" <<'EOF'
if [[ "${1-}" == "--version" ]]; then
    if [[ "${QT_QPA_PLATFORM-}" != "offscreen" ]]; then
        printf 'qml version probe missing offscreen QPA\n' >&2
        exit 134
    fi
    printf 'qml: Qt %s\n' "$(<"$BASH_SOURCE.version")"
    exit 0
fi
printf 'IMPORT_PATH=%s\n' "${QML2_IMPORT_PATH-}"
EOF
    printf '%s\n' "$version" > "$executable.version"
    chmod +x "$executable"
}

qt_qml_root="$tmpdir/qtdeclarative/lib/qt-6/qml"
mkdir -p "$qt_qml_root/QtQml"
qml_610="$tmpdir/qtdeclarative/bin/qml-6.10"
qml_611="$tmpdir/qtdeclarative/bin/qml-6.11"
qml_612="$tmpdir/qtdeclarative/bin/qml-6.12"
create_qml "$qml_610" 6.10.0
create_qml "$qml_611" 6.11.1
create_qml "$qml_612" 6.12.0

plugin_root="$tmpdir/plugins"
plugin_dir="$plugin_root/org/kde/smartlocker"
mkdir -p "$plugin_dir"
touch "$plugin_dir/qmldir" "$plugin_dir/libsmartlockerqml.so"
printf '6.11.1\n' > "$plugin_dir/qt-version"

if ! env -u QT_QPA_PLATFORM QML2_IMPORT_PATH=/fallback bash "$runner" 6.11.1 "$plugin_dir" "$qml_611" >"$tmpdir/output" 2>"$tmpdir/error"; then
    echo "matching Qt smoke selector failed before loading the QML source" >&2
    if [[ -s "$tmpdir/error" ]]; then
        < "$tmpdir/error" read -r message
        printf '%s\n' "$message" >&2
    fi
    exit 1
fi

expected_import_path="$plugin_root:$qt_qml_root:/fallback"
if ! grep -Fqx "IMPORT_PATH=$expected_import_path" "$tmpdir/output"; then
    echo "selector did not prefer the plugin and Qt 6.11.1 QML roots" >&2
    exit 1
fi

for unsupported in 6.10.0 6.12.0 6.11.2 unknown; do
    if bash "$runner" "$unsupported" "$tmpdir/does-not-exist" /bin/false 2>/dev/null; then
        echo "selector unexpectedly accepted $unsupported" >&2
        exit 1
    fi
done

for mismatched_qml in "$qml_610" "$qml_612"; do
    if bash "$runner" 6.11.1 "$plugin_dir" "$mismatched_qml" 2>"$tmpdir/error"; then
        echo "selector unexpectedly accepted a non-6.11.1 runtime" >&2
        exit 1
    fi

    if ! grep -q 'does not match selected host Qt 6.11.1' "$tmpdir/error"; then
        echo "wrong Qt runtime was not rejected before QML loading" >&2
        exit 1
    fi
done

printf '6.12.0\n' > "$plugin_dir/qt-version"
if bash "$runner" 6.11.1 "$plugin_dir" "$qml_611" 2>"$tmpdir/error"; then
    echo "selector unexpectedly accepted a Qt 6.12 plugin for a Qt 6.11.1 runtime" >&2
    exit 1
fi

if ! grep -q 'does not match selected plugin Qt 6.12.0' "$tmpdir/error"; then
    echo "mismatched plugin variant was not rejected before QML loading" >&2
    exit 1
fi

cleanup
trap - EXIT
if [[ -e "$tmpdir" ]]; then
    echo "selector test fixture directory was not removed" >&2
    exit 1
fi
