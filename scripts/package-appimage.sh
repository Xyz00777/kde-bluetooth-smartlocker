#!/usr/bin/env bash
set -euo pipefail

required=(APPIMAGE_DAEMON APPIMAGE_HOST_PLUGIN APPIMAGE_LINUXDEPLOY APPIMAGE_QT_PLUGIN APPIMAGE_TOOL APPIMAGE_RUNTIME APPIMAGE_VERSION APPIMAGE_OUT APPIMAGE_QMAKE APPIMAGE_ZLIB APPIMAGE_LIBSTDCXX APPIMAGE_LIBGCC APPIMAGE_GLIBC)
for name in "${required[@]}"; do
    if [[ -z "${!name:-}" ]]; then
        printf 'Missing required environment variable: %s\n' "$name" >&2
        exit 2
    fi
done

source_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
work="$(mktemp -d)"
trap 'chmod -R u+rwX "$work" 2>/dev/null || true; rm -rf "$work"' EXIT
appdir="$work/SmartLocker.AppDir"
mkdir -p "$appdir/usr/bin" "$appdir/usr/share/applications" \
    "$appdir/usr/share/icons/hicolor/scalable/apps" "$appdir/usr/share/kde-bluetooth-smartlocker"
cp "$APPIMAGE_DAEMON/bin/kde-bluetooth-smartlocker" "$appdir/usr/bin/"
mkdir -p "$appdir/usr/lib"
cp -L "$APPIMAGE_ZLIB" "$appdir/usr/lib/libz.so.1"
cp -L "$APPIMAGE_LIBSTDCXX" "$appdir/usr/lib/libstdc++.so.6"
cp -L "$APPIMAGE_LIBGCC" "$appdir/usr/lib/libgcc_s.so.1"
cp -a "$APPIMAGE_DAEMON/share/kde-bluetooth-smartlocker/dms-plugin" \
    "$appdir/usr/share/kde-bluetooth-smartlocker/"
mkdir -p "$appdir/usr/share/plasma/plasmoids"
cp -a "$APPIMAGE_DAEMON/share/plasma/plasmoids/org.kde.smartlocker" \
    "$appdir/usr/share/plasma/plasmoids/"
host_plugin="$work/plasma-qt6_11"
cp -a "$APPIMAGE_HOST_PLUGIN/lib/qt-6/qml/org/kde/smartlocker" "$host_plugin"
test "$(<"$host_plugin/qt-version")" = 6.11.1
test -f "$host_plugin/qmldir"
chmod -R u+rwX "$appdir" "$host_plugin"

cp "$source_root/packaging/kde-bluetooth-smartlocker.desktop" "$appdir/"
cp "$source_root/packaging/kde-bluetooth-smartlocker.svg" \
    "$appdir/usr/share/icons/hicolor/scalable/apps/kde-bluetooth-smartlocker.svg"
cat > "$appdir/AppRun" <<'APPRUN'
#!/bin/sh
HERE="$(dirname "$(readlink -f "$0")")"
if [ "${1:-}" = --install ] || [ "${1:-}" = --uninstall ]; then
    SMARTLOCKER_APPDIR="$HERE"; export SMARTLOCKER_APPDIR
    exec /bin/bash "$HERE/usr/libexec/smartlocker-install" "$@"
fi
export LD_LIBRARY_PATH="$HERE/usr/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export QT_PLUGIN_PATH="$HERE/usr/plugins"
export QML_IMPORT_PATH="$HERE/usr/qml"
export QML2_IMPORT_PATH="$HERE/usr/qml"
for hook in "$HERE"/apprun-hooks/*.sh; do
    [ -f "$hook" ] && . "$hook"
done
exec "$HERE/usr/lib/glibc/ld-linux-x86-64.so.2" \
    --library-path "$HERE/usr/lib:$HERE/usr/lib/glibc" \
    "$HERE/usr/bin/kde-bluetooth-smartlocker" "$@"
APPRUN
chmod +x "$appdir/AppRun"

export APPIMAGE_EXTRACT_AND_RUN=1
export SOURCE_DATE_EPOCH=1
export TZ=UTC
export LC_ALL=C
export QMAKE="${APPIMAGE_QMAKE:?Missing Qt 6.12 qmake path}"
mkdir -p "$work/bin"
cp "$APPIMAGE_LINUXDEPLOY" "$work/bin/linuxdeploy.AppImage"
cp "$APPIMAGE_QT_PLUGIN" "$work/bin/linuxdeploy-plugin-qt.AppImage"
cp "$APPIMAGE_TOOL" "$work/bin/appimagetool.AppImage"
cp "$APPIMAGE_RUNTIME" "$work/bin/runtime-x86_64"
chmod +x "$work/bin/linuxdeploy.AppImage" "$work/bin/linuxdeploy-plugin-qt.AppImage" "$work/bin/appimagetool.AppImage"
export APPIMAGE_QT_PLUGIN="$work/bin/linuxdeploy-plugin-qt.AppImage"
cat > "$work/bin/linuxdeploy-plugin-qt" <<'PLUGIN_WRAPPER'
#!/bin/sh
exec "$APPIMAGE_QT_PLUGIN" --appimage-extract-and-run "$@"
PLUGIN_WRAPPER
chmod +x "$work/bin/linuxdeploy-plugin-qt"
export PATH="$work/bin:$PATH"
"$work/bin/linuxdeploy.AppImage" --appimage-extract-and-run --appdir "$appdir" \
    --executable "$appdir/usr/bin/kde-bluetooth-smartlocker" \
    --desktop-file "$appdir/kde-bluetooth-smartlocker.desktop" --plugin qt
patchelf --set-interpreter /lib64/ld-linux-x86-64.so.2 \
    "$appdir/usr/bin/kde-bluetooth-smartlocker"
mkdir -p "$appdir/usr/share/kde-bluetooth-smartlocker"
cp -a "$host_plugin" "$appdir/usr/share/kde-bluetooth-smartlocker/plasma-qt6_11"
chmod -R u+rwX "$appdir/usr/share/kde-bluetooth-smartlocker/plasma-qt6_11"
mkdir -p "$appdir/usr/libexec"
cp "$source_root/scripts/install-appimage.sh" "$appdir/usr/libexec/smartlocker-install"
chmod +x "$appdir/usr/libexec/smartlocker-install"
if [[ "$appdir/usr/share/kde-bluetooth-smartlocker/plasma-qt6_11" == "$appdir/usr/lib/qt-6/qml"* ]]; then
    printf 'Host Qt 6.11 plugin entered bundled Qt QML imports\n' >&2
    exit 1
fi

mkdir -p "$appdir/usr/lib/glibc"
cp -aL "$APPIMAGE_GLIBC/lib/." "$appdir/usr/lib/glibc/"
while IFS= read -r -d '' elf; do
    file -b "$elf" | grep -Eq '^ELF .* (shared object|pie executable|executable)' || continue
    rpath="$(patchelf --print-rpath "$elf")"
    if [[ -n "$rpath" ]]; then
        origin_rpath=\$ORIGIN
        patchelf --set-rpath "$origin_rpath" "$elf"
    fi
done < <(find "$appdir/usr/lib/glibc" -type f -print0)

for library in libz.so.1 libstdc++.so.6 libgcc_s.so.1; do
    origin_rpath=\$ORIGIN
    patchelf --set-rpath "$origin_rpath" "$appdir/usr/lib/$library"
done
bash "$appdir/AppRun" --version
bash "$appdir/AppRun" --help >/dev/null

output="$APPIMAGE_OUT/kde-bluetooth-smartlocker-${APPIMAGE_VERSION}-x86_64.AppImage"
mkdir -p "$APPIMAGE_OUT"
ARCH=x86_64 VERSION="$APPIMAGE_VERSION" APPIMAGE_EXTRACT_AND_RUN=1 \
    "$work/bin/appimagetool.AppImage" --appimage-extract-and-run \
    --runtime-file "$work/bin/runtime-x86_64" "$appdir" "$output"
chmod +x "$output"
(cd "$APPIMAGE_OUT" && sha256sum "$(basename "$output")" > "$(basename "$output").sha256")
(cd "$APPIMAGE_OUT" && sha256sum --check "$(basename "$output").sha256")

bash "$source_root/scripts/check-appimage-elf.sh" "$appdir"

[[ "$(find "$APPIMAGE_OUT" -maxdepth 1 -name '*.AppImage' | wc -l)" -eq 1 ]]
