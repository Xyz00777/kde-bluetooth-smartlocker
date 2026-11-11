#!/usr/bin/env bash
set -euo pipefail

python3 - "$1" <<'PY'
import json
import sys

results = json.loads(sys.argv[1])
if not results or any(result != "success" for result in results):
    print(f"Required validation job failed or was skipped: {results}", file=sys.stderr)
    raise SystemExit(1)
PY
