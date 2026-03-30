#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT_DIR"

DOC_DIR="docs/architecture"

if [[ ! -d "$DOC_DIR" ]]; then
  echo "[validate-architecture-docs] missing $DOC_DIR" >&2
  exit 1
fi

echo "[validate-architecture-docs] checking confidence sections"
for file in "$DOC_DIR"/*.md; do
  if ! rg -q "^## Confidence Score" "$file"; then
    echo "[validate-architecture-docs] missing confidence section: $file" >&2
    exit 1
  fi
done

echo "[validate-architecture-docs] checking key discovery coverage"
for file in "$DOC_DIR"/README.md \
            "$DOC_DIR"/emulator-overview.md \
            "$DOC_DIR"/rendering-pipeline.md \
            "$DOC_DIR"/io-audio-network-and-platform.md \
            "$DOC_DIR"/cast-libraries-members-and-items.md; do
  if ! rg -q "^## (Key Discoveries|Discovery Highlights)" "$file"; then
    echo "[validate-architecture-docs] missing discoveries section: $file" >&2
    exit 1
  fi
done

echo "[validate-architecture-docs] checking for unresolved placeholders"
if rg -n "TODO|TBD" "$DOC_DIR"; then
  echo "[validate-architecture-docs] unresolved placeholder text found" >&2
  exit 1
fi

echo "[validate-architecture-docs] ok"
