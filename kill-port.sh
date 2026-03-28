#!/usr/bin/env bash

set -euo pipefail

PORT="${1:-8080}"

find_listener_pids() {
  lsof -tiTCP:"${PORT}" -sTCP:LISTEN 2>/dev/null || true
}

PIDS="$(find_listener_pids)"

if [[ -z "${PIDS}" ]]; then
  echo "No listening process found on port ${PORT}."
  exit 0
fi

echo "Stopping listener(s) on port ${PORT}: ${PIDS}"

while IFS= read -r PID; do
  [[ -n "${PID}" ]] && kill "${PID}" 2>/dev/null || true
done <<< "${PIDS}"

sleep 0.3

REMAINING="$(find_listener_pids)"
if [[ -n "${REMAINING}" ]]; then
  echo "Port ${PORT} still busy, forcing stop: ${REMAINING}"
  while IFS= read -r PID; do
    [[ -n "${PID}" ]] && kill -9 "${PID}" 2>/dev/null || true
  done <<< "${REMAINING}"
fi

echo "Port ${PORT} is clear."
