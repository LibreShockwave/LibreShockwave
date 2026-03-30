#!/usr/bin/env bash
set -euo pipefail

LS_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

DIST_DIR="$LS_ROOT/player-wasm/build/dist"
HTTP_PORT="${HTTP_PORT:-8080}"
TARGET_HOST="${TARGET_HOST:-au.h4bbo.net}"
TARGET_GAME_PORT="${TARGET_GAME_PORT:-8443}"
TARGET_MUS_PORT="${TARGET_MUS_PORT:-8443}"
USE_LOCAL_PROXY="${USE_LOCAL_PROXY:-0}"
PROXY_HOST="${PROXY_HOST:-$TARGET_HOST}"
LOCAL_PROXY_HOST="${LOCAL_PROXY_HOST:-127.0.0.1}"
LOCAL_GAME_WS_PORT="${LOCAL_GAME_WS_PORT:-30101}"
LOCAL_MUS_WS_PORT="${LOCAL_MUS_WS_PORT:-38101}"
VENV_DIR="${VENV_DIR:-${TMPDIR:-/tmp}/libreshockwave-websockify-venv}"

cleanup() {
    if [[ -n "${HTTP_PID:-}" ]]; then kill "$HTTP_PID" 2>/dev/null || true; fi
    if [[ -n "${GAME_PID:-}" ]]; then kill "$GAME_PID" 2>/dev/null || true; fi
    if [[ -n "${MUS_PID:-}" ]]; then kill "$MUS_PID" 2>/dev/null || true; fi
}

trap cleanup EXIT INT TERM

run_gradle() {
    zsh "$LS_ROOT/gradlew" "$@"
}

file_mtime_epoch() {
    local target="$1"
    if stat -f %m "$target" >/dev/null 2>&1; then
        stat -f %m "$target"
    else
        stat -c %Y "$target"
    fi
}

format_epoch() {
    local epoch="$1"
    if date -r "$epoch" "+%Y-%m-%d %H:%M:%S %Z" >/dev/null 2>&1; then
        date -r "$epoch" "+%Y-%m-%d %H:%M:%S %Z"
    else
        date -d "@$epoch" "+%Y-%m-%d %H:%M:%S %Z"
    fi
}

sync_static_web_resources() {
    mkdir -p "$DIST_DIR"
    cp "$LS_ROOT/player-wasm/src/main/resources/web/index.html" "$DIST_DIR/index.html"
    cp "$LS_ROOT/player-wasm/src/main/resources/web/libreshockwave.css" "$DIST_DIR/libreshockwave.css"
    cp "$LS_ROOT/player-wasm/src/main/resources/web/shockwave-worker.js" "$DIST_DIR/shockwave-worker.js"

    perl -0pi -e "s|<!--BUILD_VERSION-->|$WEB_BUILD_LABEL|g; s|href=\"libreshockwave\\.css\"|href=\"libreshockwave.css?v=$WEB_ASSET_VERSION\"|g; s|src=\"libreshockwave\\.js\"|src=\"libreshockwave.js?v=$WEB_ASSET_VERSION\"|g" \
        "$DIST_DIR/index.html"
}

echo "[build] Compiling LibreShockwave editor classes"
run_gradle :editor:classes

echo "[build] Assembling LibreShockwave web bundle"
run_gradle :player-wasm:assembleWasm

WEB_ASSET_VERSION="$(file_mtime_epoch "$DIST_DIR/libreshockwave.js")"
WEB_BUILD_LABEL="Updated $(format_epoch "$WEB_ASSET_VERSION")"
echo "[web] $WEB_BUILD_LABEL"

sync_static_web_resources

echo "[stack] Starting LibreShockwave web player"
(
    cd "$DIST_DIR"
    python3 -m http.server "$HTTP_PORT" --bind 127.0.0.1
) &
HTTP_PID=$!

if [[ "$USE_LOCAL_PROXY" == "1" ]]; then
    if [[ ! -x "$VENV_DIR/bin/python" ]]; then
        echo "[setup] Creating websockify virtualenv at $VENV_DIR"
        python3 -m venv "$VENV_DIR"
    fi

    if ! "$VENV_DIR/bin/python" -c "import websockify" >/dev/null 2>&1; then
        echo "[setup] Installing websockify into virtualenv"
        "$VENV_DIR/bin/pip" install websockify
    fi

    echo "[stack] Starting local WebSocket proxies"
    "$VENV_DIR/bin/python" -m websockify --verbose --traffic "$LOCAL_GAME_WS_PORT" "$TARGET_HOST:$TARGET_GAME_PORT" &
    GAME_PID=$!

    "$VENV_DIR/bin/python" -m websockify --verbose --traffic "$LOCAL_MUS_WS_PORT" "$TARGET_HOST:$TARGET_MUS_PORT" &
    MUS_PID=$!

    BROWSER_HOST="$LOCAL_PROXY_HOST"
    BROWSER_GAME_PORT="$LOCAL_GAME_WS_PORT"
    BROWSER_MUS_PORT="$LOCAL_MUS_WS_PORT"
else
    echo "[stack] Using direct remote WebSocket transport"
    echo "[stack] Game: $TARGET_HOST:$TARGET_GAME_PORT"
    echo "[stack] MUS:  $TARGET_HOST:$TARGET_MUS_PORT"
    BROWSER_HOST="$PROXY_HOST"
    BROWSER_GAME_PORT="$TARGET_GAME_PORT"
    BROWSER_MUS_PORT="$TARGET_MUS_PORT"
fi

OPEN_URL="http://127.0.0.1:$HTTP_PORT/?proxyHost=$BROWSER_HOST&gamePort=$BROWSER_GAME_PORT&musPort=$BROWSER_MUS_PORT"
echo
echo "Open this URL in your browser:"
echo "$OPEN_URL"
echo
if [[ "$USE_LOCAL_PROXY" == "1" ]]; then
    echo "Press Ctrl-C to stop the web player and proxies."
else
    echo "Press Ctrl-C to stop the web player."
fi

if [[ "${NO_BROWSER:-0}" != "1" ]] && command -v open >/dev/null 2>&1; then
    open "$OPEN_URL" >/dev/null 2>&1 || true
fi

if [[ "$USE_LOCAL_PROXY" == "1" ]]; then
    wait "$HTTP_PID" "$GAME_PID" "$MUS_PID"
else
    wait "$HTTP_PID"
fi
