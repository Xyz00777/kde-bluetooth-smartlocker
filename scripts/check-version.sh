#!/bin/sh
# Verifies that the application version is identical across all four sources:
#   - CMakeLists.txt         (project VERSION)
#   - flake.nix              (version = "...")
#   - src/main.cpp           (setApplicationVersion("..."))
#   - plasmoid/metadata.json ("Version": "...")
# Exits 1 (listing the mismatching files) when they disagree.
set -u

repo_root=$(git rev-parse --show-toplevel 2>/dev/null || dirname "$0" | sed "s|/scripts$||")
cd "$repo_root" || exit 1

cmake_version=$(sed -n 's/.*project([^)]*VERSION \([0-9][0-9.]*\).*/\1/p' CMakeLists.txt | head -n 1)
nix_version=$(sed -n 's/.*version = "\([0-9][0-9.]*\)".*/\1/p' flake.nix | head -n 1)
cpp_version=$(sed -n 's/.*setApplicationVersion("\([0-9][0-9.]*\)").*/\1/p' src/main.cpp | head -n 1)
json_version=$(sed -n 's/.*"Version": "\([0-9][0-9.]*\)".*/\1/p' plasmoid/metadata.json | head -n 1)

fail=0
for entry in \
    "CMakeLists.txt:$cmake_version" \
    "flake.nix:$nix_version" \
    "src/main.cpp:$cpp_version" \
    "plasmoid/metadata.json:$json_version"; do
    file=${entry%%:*}
    version=${entry#*:}
    if [ -z "$version" ]; then
        echo "check-version: could not extract a version from $file" >&2
        fail=1
    elif [ "$version" != "$cmake_version" ]; then
        echo "check-version: $file is $version, expected $cmake_version (from CMakeLists.txt)" >&2
        fail=1
    fi
done

if [ "$fail" -ne 0 ]; then
    echo "check-version: version mismatch; make all four sources agree before committing" >&2
    exit 1
fi

echo "check-version: all version sources agree at $cmake_version"
