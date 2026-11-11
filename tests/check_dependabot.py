#!/usr/bin/env python3

from pathlib import Path

import yaml

root = Path(__file__).resolve().parent.parent
config = (root / ".github" / "dependabot.yml").read_text(encoding="utf-8")

def validate_config(text: str) -> None:
    document = yaml.safe_load(text)
    assert isinstance(document, dict), "Dependabot config must be a YAML mapping"
    assert document.get("version") == 2, "Dependabot config must use version 2"
    entries = document.get("updates")
    assert isinstance(entries, list), "Dependabot config must contain an updates list"
    ecosystems = [entry.get("package-ecosystem") for entry in entries if isinstance(entry, dict)]
    assert len(ecosystems) == len(entries) and sorted(ecosystems) == ["github-actions", "nix"], (
        f"Expected exactly github-actions and nix entries, got {ecosystems!r}"
    )
    for entry in entries:
        ecosystem = entry["package-ecosystem"]
        directory = entry.get("directory")
        interval = entry.get("schedule", {}).get("interval")
        limit = entry.get("open-pull-requests-limit")
        assert directory == "/", f"{ecosystem}: expected directory /"
        assert interval == "weekly", f"{ecosystem}: expected weekly cadence"
        assert isinstance(limit, int) and 1 <= limit <= 10, f"{ecosystem}: expected bounded PR limit 1..10"


validate_config(config)
for label, invalid_config in (
    ("unsupported ecosystem", config.replace("package-ecosystem: nix", "package-ecosystem: docker")),
    ("non-root directory", config.replace("    directory: /", "    directory: /subdir", 1)),
):
    try:
        validate_config(invalid_config)
    except (AssertionError, yaml.YAMLError):
        pass
    else:
        raise AssertionError(f"invalid {label} fixture was accepted")

print("Dependabot structure: PASS (parsed YAML; exact ecosystems, root directories, weekly cadence, bounded limits)")
print("Negative structural fixtures: PASS (unsupported ecosystem and non-root directory rejected)")
