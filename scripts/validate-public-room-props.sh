#!/bin/zsh
set -euo pipefail

repo_root="$(cd "$(dirname "$0")/.." && pwd)"
cd "$repo_root"

./gradlew :player-core:test \
  --tests com.libreshockwave.player.cast.CastLibManagerExternalLoadTest \
  --tests com.libreshockwave.player.cast.ParkCastPropsTest \
  --tests com.libreshockwave.player.cast.CinemaCastPropsTest
