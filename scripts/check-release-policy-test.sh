#!/bin/sh
set -eu

script_dir=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT HUP INT TERM
repo=$tmp/repo
mkdir -p "$repo/scripts" "$repo/mock-bin" "$repo/base/src" "$repo/base/plasmoid" "$repo/base/dms-plugin"
cp "$script_dir/check-release-policy.sh" "$repo/scripts/"
cp "$script_dir/check-version.sh" "$repo/scripts/"

cat > "$repo/mock-bin/git" <<'MOCK_GIT'
#!/bin/sh
set -eu
case "$1" in
    rev-parse) printf '%s\n' "$FIXTURE_REPO" ;;
    cat-file) [ "${FIXTURE_BASE_AVAILABLE:-yes}" = yes ] ;;
    diff) printf '%s\n' "${CHANGED_PATHS:-}" ;;
    show)
        path=${2#fixture-base:}
        [ "$path" != "$2" ] || exit 2
        cat "$FIXTURE_REPO/base/$path"
        ;;
    *) printf 'unexpected mocked git call: %s\n' "$*" >&2; exit 2 ;;
esac
MOCK_GIT
chmod +x "$repo/mock-bin/git"
export FIXTURE_REPO="$repo"
export PATH="$repo/mock-bin:$PATH"

write_sources() {
    version=$1
    mkdir -p "$repo/src" "$repo/plasmoid" "$repo/dms-plugin"
    printf 'project(example VERSION %s)\n' "$version" > "$repo/CMakeLists.txt"
    printf 'version = "%s";\n' "$version" > "$repo/flake.nix"
    printf 'setApplicationVersion("%s");\n' "$version" > "$repo/src/main.cpp"
    printf '{ "Version": "%s" }\n' "$version" > "$repo/plasmoid/metadata.json"
    printf '{ "version": "%s" }\n' "$version" > "$repo/dms-plugin/plugin.json"
}

write_sources 0.18.0
cp "$repo/CMakeLists.txt" "$repo/base/CMakeLists.txt"
cp "$repo/flake.nix" "$repo/base/flake.nix"
cp "$repo/src/main.cpp" "$repo/base/src/main.cpp"
cp "$repo/plasmoid/metadata.json" "$repo/base/plasmoid/metadata.json"
cp "$repo/dms-plugin/plugin.json" "$repo/base/dms-plugin/plugin.json"
printf '# Changelog\n' > "$repo/CHANGELOG.md"

run_policy() (
    cd "$repo"
    sh scripts/check-release-policy.sh fixture-base
)

expect_pass() {
    label=$1
    CHANGED_PATHS=$2
    export CHANGED_PATHS
    if run_policy > "$tmp/output" 2>&1; then
        printf 'PASS: %s\n' "$label"
    else
        printf 'FAIL: %s unexpectedly rejected\n' "$label" >&2
        cat "$tmp/output" >&2
        exit 1
    fi
}

expect_fail() {
    label=$1
    CHANGED_PATHS=$2
    export CHANGED_PATHS
    if run_policy > "$tmp/output" 2>&1; then
        printf 'FAIL: %s unexpectedly accepted\n' "$label" >&2
        exit 1
    else
        printf 'PASS: %s (rejected)\n' "$label"
    fi
}

expect_pass 'docs-only exemption' 'docs/guide.md'
expect_pass '.github-only exemption' '.github/workflows/ci.yml'
expect_pass 'Dependabot Actions-only exemption' '.github/dependabot.yml'
expect_pass 'tests-only exemption' 'tests/example.cpp'
expect_pass 'check-script-only exemption' 'scripts/check-other.sh'
expect_fail 'release-sensitive unchanged version' 'src/main.cpp'
expect_fail 'shipped AppImage installer requires version bump and changelog' 'scripts/install-appimage.sh'
expect_fail 'AppImage packaging path remains release-sensitive' 'scripts/package-appimage.sh'
expect_fail 'previous installer path remains release-sensitive' 'scripts/install-integrations.sh'

write_sources 0.17.9
expect_fail 'release-sensitive lower version' 'include/example.hpp'

write_sources 0.19.0
expect_fail 'higher version without changelog heading' 'dms-plugin/plugin.json'
printf '# Changelog\n\n## 0.19.0\nPlanned software and AppImage release.\n' > "$repo/CHANGELOG.md"
expect_pass 'mixed software and docs with bump and changelog' "$(printf 'src/main.cpp\ndocs/guide.md')"
expect_pass 'shipped AppImage installer with higher version and changelog' 'scripts/install-appimage.sh'
write_sources 0.18.0
expect_fail 'CMake test-registration-only edit still requires bump' 'CMakeLists.txt'

expect_fail 'flake.lock-only update requires higher version and changelog' 'flake.lock'
write_sources 0.19.0
printf '# Changelog\n' > "$repo/CHANGELOG.md"
expect_fail 'flake.lock-only update with bump but no changelog heading' 'flake.lock'
printf '# Changelog\n\n## 0.19.0\nNix dependency update.\n' > "$repo/CHANGELOG.md"
expect_pass 'flake.lock-only update with higher version and changelog' 'flake.lock'

printf 'setApplicationVersion("0.20.0");\n' > "$repo/src/main.cpp"
expect_fail 'five current versions must remain identical' 'src/main.cpp'

write_sources 0.18.0
export FIXTURE_BASE_AVAILABLE=no
expect_fail 'unavailable required base fails closed' 'src/main.cpp'

printf 'release-policy fixtures: all scenarios passed\n'
