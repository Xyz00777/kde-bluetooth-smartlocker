#!/bin/sh
# Enforce release metadata only when the diff includes software/package inputs.
# Shared CMakeLists.txt and Nix inputs are intentionally sensitive even when an
# individual change only adjusts test registration or build configuration.
set -eu

if [ "$#" -ne 1 ] || [ -z "$1" ]; then
    echo "usage: $0 <base-ref>" >&2
    exit 2
fi
base_ref=$1
repo_root=$(git rev-parse --show-toplevel 2>/dev/null) || {
    echo "release-policy: cannot determine repository root" >&2
    exit 1
}
cd "$repo_root" || exit 1

if ! git cat-file -e "$base_ref^{commit}" 2>/dev/null; then
    echo "release-policy: required base '$base_ref' is unavailable; refusing to compare" >&2
    exit 1
fi
if ! changed_paths=$(git diff --name-only "$base_ref" HEAD); then
    echo "release-policy: cannot compare '$base_ref' with HEAD" >&2
    exit 1
fi

release_sensitive=false
while IFS= read -r path; do
    [ -n "$path" ] || continue
    case "$path" in
        src/*|include/*|plasmoid/*|dms-plugin/*|examples/*|systemd/*|completions/*|\
        CMakeLists.txt|flake.nix|flake.lock|scripts/package-appimage.sh|scripts/install-integrations.sh|scripts/install-appimage.sh)
            release_sensitive=true
            break
            ;;
    esac
done <<EOF
$changed_paths
EOF

if [ "$release_sensitive" != true ]; then
    echo "release-policy: no release-sensitive paths changed; version bump not required"
    exit 0
fi

if ! sh scripts/check-version.sh; then
    echo "release-policy: all five application version sources must agree" >&2
    exit 1
fi

python3 - "$base_ref" <<'PY'
import re
import subprocess
import sys
from pathlib import Path

base = sys.argv[1]
sources = {
    "CMakeLists.txt": re.compile(r"project\([^)]*VERSION\s+([0-9]+\.[0-9]+\.[0-9]+)"),
    "flake.nix": re.compile(r'\bversion\s*=\s*"([^"]+)"'),
    "src/main.cpp": re.compile(r'setApplicationVersion\("([^"]+)"\)'),
    "plasmoid/metadata.json": re.compile(r'"Version"\s*:\s*"([^"]+)"'),
    "dms-plugin/plugin.json": re.compile(r'"version"\s*:\s*"([^"]+)"'),
}
semver = re.compile(
    r"(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)"
    r"(?:-((?:0|[1-9]\d*|[0-9A-Za-z-]*[A-Za-z-][0-9A-Za-z-]*)"
    r"(?:\.(?:0|[1-9]\d*|[0-9A-Za-z-]*[A-Za-z-][0-9A-Za-z-]*))*))?"
    r"(?:\+[0-9A-Za-z-]+(?:\.[0-9A-Za-z-]+)*)?"
)

def source_version(path: str, content: str) -> str:
    match = sources[path].search(content)
    if not match:
        raise ValueError(f"could not extract version from {path}")
    return match.group(1)

def read_base(path: str) -> str:
    result = subprocess.run(
        ["git", "show", f"{base}:{path}"], text=True, capture_output=True
    )
    if result.returncode:
        raise ValueError(f"base commit does not contain readable {path}")
    return source_version(path, result.stdout)

def parse(value: str):
    match = semver.fullmatch(value)
    if not match:
        raise ValueError(f"{value!r} is not strict SemVer")
    major, minor, patch, prerelease = match.groups()
    identifiers = prerelease.split(".") if prerelease else None
    return int(major), int(minor), int(patch), identifiers

def greater(candidate, previous) -> bool:
    if candidate[:3] != previous[:3]:
        return candidate[:3] > previous[:3]
    new_pre, old_pre = candidate[3], previous[3]
    if old_pre is None:
        return False
    if new_pre is None:
        return True
    for new, old in zip(new_pre, old_pre):
        if new == old:
            continue
        new_num, old_num = new.isdigit(), old.isdigit()
        if new_num and old_num:
            return int(new) > int(old)
        if new_num != old_num:
            return not new_num
        return new > old
    return len(new_pre) > len(old_pre)

try:
    base_versions = {path: read_base(path) for path in sources}
    base_values = set(base_versions.values())
    if len(base_values) != 1:
        raise ValueError("base commit's five version sources disagree: " + ", ".join(
            f"{path}={version}" for path, version in base_versions.items()
        ))
    base_version = next(iter(base_values))
    current_versions = {
        path: source_version(path, Path(path).read_text(encoding="utf-8"))
        for path in sources
    }
    current_values = set(current_versions.values())
    if len(current_values) != 1:
        raise ValueError("current version sources disagree")
    current_version = next(iter(current_values))
    if not greater(parse(current_version), parse(base_version)):
        raise ValueError(
            f"release-sensitive changes require a strictly higher SemVer than "
            f"{base_version}; update all five sources (current: {current_version})"
        )
    parse(current_version)
    changelog = Path("CHANGELOG.md")
    if not changelog.is_file() or not re.search(
        rf"^##\s+{re.escape(current_version)}\s*$",
        changelog.read_text(encoding="utf-8"),
        re.MULTILINE,
    ):
        raise ValueError(
            f"CHANGELOG.md must contain a '## {current_version}' release heading"
        )
except (OSError, ValueError) as error:
    print(f"release-policy: {error}", file=sys.stderr)
    raise SystemExit(1)

print(f"release-policy: {current_version} is greater than base {base_version}; changelog heading found")
PY
