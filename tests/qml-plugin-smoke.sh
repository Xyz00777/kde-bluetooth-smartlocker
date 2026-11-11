#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 3 ]]; then
    echo "usage: $0 <6.11.1> <plugin-directory> <qml-executable>" >&2
    exit 2
fi

expected_minor="$1"
plugin_dir="$2"
qml_executable="$3"

case "$expected_minor" in
    6.11.1) ;;
    *) echo "unsupported host Qt version: $expected_minor" >&2; exit 2 ;;
esac

version_output="$(QT_QPA_PLATFORM=offscreen "$qml_executable" --version 2>&1)"
if [[ "$version_output" =~ Qt[[:space:]]+([0-9]+\.[0-9]+\.[0-9]+) ]]; then
    runtime_version="${BASH_REMATCH[1]}"
elif [[ "$version_output" =~ ([0-9]+\.[0-9]+\.[0-9]+) ]]; then
    runtime_version="${BASH_REMATCH[1]}"
else
    echo "cannot determine Qt runtime version from qml --version: $version_output" >&2
    exit 2
fi

if [[ "$runtime_version" != "$expected_minor" ]]; then
    echo "Qt runtime $runtime_version does not match selected host Qt $expected_minor" >&2
    exit 2
fi

if [[ ! -f "$plugin_dir/qmldir" || ! -f "$plugin_dir/libsmartlockerqml.so" || ! -f "$plugin_dir/qt-version" ]]; then
    echo "selected Qt $expected_minor plugin directory is incomplete" >&2
    exit 2
fi
plugin_version="$(<"$plugin_dir/qt-version")"
if [[ "$plugin_version" != "$expected_minor" ]]; then
    echo "Qt runtime $runtime_version does not match selected plugin Qt $plugin_version" >&2
    exit 2
fi

plugin_root="$(dirname "$(dirname "$(dirname "$plugin_dir")")")"
qt_prefix="$(dirname "$(dirname "$qml_executable")")"
qt_qml_root="$qt_prefix/lib/qt-6/qml"
if [[ ! -d "$qt_qml_root/QtQml" ]]; then
    echo "selected Qt $expected_minor runtime has no QtQml import module at $qt_qml_root" >&2
    exit 2
fi

export QML2_IMPORT_PATH="$plugin_root:$qt_qml_root${QML2_IMPORT_PATH:+:$QML2_IMPORT_PATH}"
exec "$qml_executable" -platform offscreen "$(dirname "$0")/qml-plugin-smoke.qml"
