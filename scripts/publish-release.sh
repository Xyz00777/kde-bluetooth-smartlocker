#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 2 ]]; then
  printf 'usage: %s <before-sha> <artifact-directory>\n' "$0" >&2
  exit 2
fi

before=$1
assets_dir=$2
zero_sha=0000000000000000000000000000000000000000

if [[ -z $before || $before == "$zero_sha" ]]; then
  printf 'release: no comparable push history; artifact-only update\n'
  exit 0
fi
git cat-file -e "${before}^{commit}" || {
  printf 'release: required push before %s is unavailable\n' "$before" >&2
  exit 1
}

changed_paths=$(git diff --name-only "$before" HEAD)
release_sensitive=false
while IFS= read -r path; do
  case "$path" in
    src/*|include/*|plasmoid/*|dms-plugin/*|examples/*|systemd/*|completions/*|CMakeLists.txt|flake.nix|flake.lock|scripts/package-appimage.sh|scripts/install-integrations.sh|scripts/install-appimage.sh)
      release_sensitive=true
      break
      ;;
  esac
done <<< "$changed_paths"
if [[ $release_sensitive != true ]]; then
  printf 'release: no release-sensitive paths changed; artifact-only update\n'
  exit 0
fi

./scripts/check-release-policy.sh "$before"
version=$(python3 -c 'import re; from pathlib import Path; match = re.search(r"project\([^)]*VERSION\s+([0-9]+\.[0-9]+\.[0-9]+)", Path("CMakeLists.txt").read_text()); print(match.group(1) if match else "")')
[[ -n $version ]] || { printf 'release: cannot read project SemVer\n' >&2; exit 1; }
tag="v${version}"
git rev-parse --verify HEAD >/dev/null
mapfile -t assets < <(find "$assets_dir" -maxdepth 1 -type f \( -name '*.AppImage' -o -name '*.sha256' \) -print | sort)
if [[ ${#assets[@]} -ne 2 ]]; then
  printf 'release: expected exactly one AppImage and one SHA-256 file in %s\n' "$assets_dir" >&2
  exit 1
fi

tag_response=$(mktemp)
trap 'rm -f "$tag_response"' EXIT
tag_sha=''
if gh api "repos/${GITHUB_REPOSITORY}/git/ref/tags/${tag}" > "$tag_response" 2>/dev/null; then
  tag_sha=$(python3 -c 'import json, sys; print(json.load(open(sys.argv[1], encoding="utf-8"))["object"]["sha"])' "$tag_response")
  if [[ $tag_sha != "$(git rev-parse HEAD)" ]]; then
    printf 'release: tag %s points to %s, not HEAD; refusing mutation\n' "$tag" "$tag_sha" >&2
    exit 1
  fi
else
  gh api --method POST "repos/${GITHUB_REPOSITORY}/git/refs" \
    -f "ref=refs/tags/${tag}" -f "sha=$(git rev-parse HEAD)" >/dev/null
fi

notes=$(mktemp)
trap 'rm -f "$notes" "$tag_response"' EXIT
awk -v heading="## ${version}" 'index($0, heading) == 1 { found=1; next } found && /^## / { exit } found { print }' CHANGELOG.md > "$notes"
if gh release view "$tag" >/dev/null 2>&1; then
  gh release upload "$tag" "${assets[@]}" --clobber
else
  gh release create "$tag" "${assets[@]}" --title "$tag" --notes-file "$notes"
fi
