#!/usr/bin/env bash
set -euo pipefail

root=$(CDPATH='' cd -- "$(dirname -- "$0")/.." && pwd)
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
fixture=$tmp/repo
mkdir -p "$fixture/scripts" "$fixture/src" "$fixture/plasmoid" "$fixture/dms-plugin" "$fixture/release-assets" "$tmp/bin"
cp "$root/scripts/publish-release.sh" "$fixture/scripts/"
cp "$root/scripts/require-checks.sh" "$fixture/scripts/"
cp "$root/scripts/check-release-policy.sh" "$fixture/scripts/"
cp "$root/scripts/check-version.sh" "$fixture/scripts/"
cp "$root/CMakeLists.txt" "$fixture/"
cp "$root/CHANGELOG.md" "$fixture/"
sed -i 's/VERSION 0\.18\.0/VERSION 0.19.0/' "$fixture/CMakeLists.txt"
cat > "$fixture/scripts/check-release-policy.sh" <<'MOCK_POLICY'
#!/usr/bin/env bash
set -euo pipefail
[[ ${FIXTURE_POLICY:-pass} == pass ]]
MOCK_POLICY
chmod +x "$fixture/scripts/check-release-policy.sh"
cp "$root/flake.nix" "$fixture/"
cp "$root/src/main.cpp" "$fixture/src/"
cp "$root/plasmoid/metadata.json" "$fixture/plasmoid/metadata.json"
cp "$root/dms-plugin/plugin.json" "$fixture/dms-plugin/plugin.json"
touch "$fixture/release-assets/test.AppImage" "$fixture/release-assets/test.AppImage.sha256"

cat > "$tmp/bin/git" <<'MOCK_GIT'
#!/usr/bin/env bash
set -euo pipefail
case "$1" in
  cat-file) [[ ${FIXTURE_HISTORY:-yes} == yes ]] ;;
  diff) printf '%s\n' "${FIXTURE_PATHS:-src/main.cpp}" ;;
  rev-parse) [[ ${2:-} == HEAD || ${3:-} == HEAD ]] && printf '%s\n' "$FIXTURE_HEAD" ;;
  *) printf 'unexpected git call: %s\n' "$*" >&2; exit 2 ;;
esac
MOCK_GIT
cat > "$tmp/bin/gh" <<'MOCK_GH'
#!/usr/bin/env bash
set -euo pipefail
printf '%s\n' "$*" >> "$GH_LOG"
case "$*" in
  'api repos/test/project/git/ref/tags/v0.19.0')
    [[ ${FIXTURE_TAG:-absent} != absent ]] || exit 1
    printf '{"object":{"sha":"%s"}}\n' "$FIXTURE_TAG"
    ;;
  'api --method POST repos/test/project/git/refs'*)
    [[ ${FIXTURE_TAG:-absent} == absent ]] || exit 9
    ;;
  'release view v0.19.0') [[ ${FIXTURE_RELEASE:-absent} == present ]] ;;
  'release create v0.19.0'*) [[ ${FIXTURE_RELEASE:-absent} == absent ]] ;;
  'release upload v0.19.0'*) [[ ${FIXTURE_RELEASE:-absent} == present ]] ;;
  *) printf 'unexpected gh call: %s\n' "$*" >&2; exit 2 ;;
esac
MOCK_GH
chmod +x "$tmp/bin/git" "$tmp/bin/gh"
export PATH="$tmp/bin:$PATH" GH_LOG="$tmp/gh.log" GITHUB_REPOSITORY=test/project
export FIXTURE_HEAD=commit-head

run_publisher() (
  cd "$fixture"
  scripts/publish-release.sh "$1" release-assets
)
pass() { printf 'PASS: %s\n' "$1"; }
expect_no_publish() {
  : > "$GH_LOG"
  run_publisher "$1"
  [[ ! -s $GH_LOG ]]
  pass "$2"
}
expect_no_publish 0000000000000000000000000000000000000000 'all-zero before is artifact-only'
expect_no_publish '' 'missing before is artifact-only'
export FIXTURE_PATHS=docs/readme.md
expect_no_publish before-sha 'docs-only main update is artifact-only'
export FIXTURE_PATHS=src/main.cpp
export FIXTURE_HISTORY=no
if run_publisher missing-sha >/dev/null 2>&1; then exit 1; fi
pass 'missing required nonzero history fails closed'
export FIXTURE_HISTORY=yes FIXTURE_PATHS=src/main.cpp
export FIXTURE_TAG=absent FIXTURE_RELEASE=absent
: > "$GH_LOG"
run_publisher before-sha
grep -Fq 'refs/tags/v0.19.0' "$GH_LOG"
grep -Fq 'release create v0.19.0' "$GH_LOG"
pass 'software bump creates versioned tag and release with assets'
export FIXTURE_PATHS=scripts/install-appimage.sh FIXTURE_TAG=absent FIXTURE_RELEASE=absent
: > "$GH_LOG"
run_publisher before-sha
grep -Fq 'release create v0.19.0' "$GH_LOG"
pass 'publisher treats the shipped installer path as release-sensitive'
FIXTURE_PATHS=$(printf 'src/main.cpp\ndocs/readme.md')
export FIXTURE_PATHS
: > "$GH_LOG"
run_publisher before-sha
grep -Fq 'release create v0.19.0' "$GH_LOG"
pass 'batched push compares before to final HEAD and sees software path'
export FIXTURE_TAG=commit-head FIXTURE_RELEASE=present
: > "$GH_LOG"
run_publisher before-sha
grep -Fq 'release upload v0.19.0' "$GH_LOG"
grep -Fq -- '--clobber' "$GH_LOG"
if grep -Fq 'git/refs' "$GH_LOG"; then exit 1; fi
pass 'same-commit tag rerun clobbers same release assets without moving tag'
export FIXTURE_TAG=other-commit
: > "$GH_LOG"
if run_publisher before-sha >/dev/null 2>&1; then exit 1; fi
if grep -Fq 'git/refs' "$GH_LOG"; then exit 1; fi
if grep -Fq 'release ' "$GH_LOG"; then exit 1; fi
pass 'conflicting tag fails before any mutation'

"$root/scripts/require-checks.sh" '["success","success"]'
for results in '["success","failure"]' '["success","skipped"]'; do
  if "$root/scripts/require-checks.sh" "$results" >/dev/null 2>&1; then exit 1; fi
done
pass 'aggregate accepts only all-success; failed and skipped checks fail'

workflow=$root/.github/workflows/ci.yml
python3 - "$workflow" <<'PY'
import sys
from pathlib import Path

import yaml

workflow = yaml.load(Path(sys.argv[1]).read_text(encoding="utf-8"), Loader=yaml.BaseLoader)
assert workflow["name"] == "CI"
assert "pull_request" in workflow["on"]
assert workflow["permissions"] == {"contents": "read"}
jobs = workflow["jobs"]
required = jobs["required-checks"]
assert required["if"] == "always()"
assert required["needs"] == ["checkout", "release-policy", "lint", "nix", "appimage-validation"]
assert "scripts/require-checks.sh" in required["steps"][-1]["run"]
assert all(name in jobs for name in required["needs"])
main_guard = "github.event_name == 'push' && github.ref == 'refs/heads/main'"
assert jobs["main-artifact"]["if"] == main_guard
assert jobs["release"]["if"] == main_guard
assert jobs["release"]["needs"] == ["required-checks", "main-artifact"]
assert jobs["release"]["permissions"] == {"contents": "write"}
assert jobs["main-artifact"]["steps"][-1]["with"]["retention-days"] == "90"
assert "AppImage" in jobs["main-artifact"]["steps"][-1]["with"]["path"]
assert ".sha256" in jobs["main-artifact"]["steps"][-1]["with"]["path"]
assert all(job.get("permissions", {}).get("contents", "read") == "read" for name, job in jobs.items() if name != "release")

def publishes(event, ref):
    return event == "push" and ref == "refs/heads/main"

for event, ref in (("pull_request", "refs/pull/7/merge"), ("push", "refs/heads/topic"), ("push", "refs/heads/main")):
    expected = event == "push" and ref == "refs/heads/main"
    assert publishes(event, ref) is expected
PY
if grep -Eq 'uses:[[:space:]]*[^@[:space:]]+@(main|master|v[0-9])([[:space:]]|$)' "$workflow"; then exit 1; fi
grep -Fq 'actions/upload-artifact@ea165f8d65b6e75b540449e92b4886f43607fa02' "$workflow"
pass 'parsed workflow aggregate dependencies, PR/non-main/main guards, scoped permissions and action pins'
printf 'workflow/release fixtures: all scenarios passed\n'
