#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT_DIR"

echo "[efficiency-scan] repository: $ROOT_DIR"
echo "[efficiency-scan] recent hotspot hints"
rg -n "tick\\(|renderFrame\\(|processTimeouts\\(|preloadAllCasts\\(|decodeBitmap\\(|bumpRevision\\(|getOrCreate\\(" \
  player-core sdk vm player-wasm \
  -g'*.java' | sed -n '1,120p' || true

echo
echo "[efficiency-scan] TODO-like performance markers"
rg -n "TODO|FIXME|cache|revision|lazy|hot path|perf|performance" \
  player-core sdk vm player-wasm docs/architecture \
  -g'*.java' -g'*.md' | sed -n '1,120p' || true

echo
echo "[efficiency-scan] gradle tasks that look relevant"
rg -n "tasks.register\\(" player-core/build.gradle sdk/build.gradle vm/build.gradle player-wasm/build.gradle \
  | sed -n '1,120p' || true
