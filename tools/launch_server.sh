#!/bin/bash
# Reliable server launcher for Ashgrove Valley.
#
# The server MUST be launched inside a detached screen session so it survives
# the calling shell / agent tool timeouts. Never background it with plain '&'
# from an interactive shell: when the invoking shell exits or a tool times out,
# the server process group gets killed (SIGHUP/SIGTERM to the whole group).
#
# IMPORTANT: when stopping the server use `pkill -x ashgrove_server` (exact
# match on the binary name). Do NOT use `pkill -f ashgrove_server` -- the -f
# flag matches the *full command line*, which also matches the shell running
# the pkill itself (`/bin/bash -c pkill -f ashgrove_server ...`), causing the
# caller to kill its own shell and hang.
set -euo pipefail

SCREEN_NAME="${SCREEN_NAME:-ashgrove}"
PORT="${PORT:-8080}"
APP_DIR="/home/umang/ashgrove"
PID_FILE="${APP_DIR}/run/ashgrove.pid"

mkdir -p "${APP_DIR}/run"

stop_server() {
    # Stop any running instance (exact match only, see header comment).
    if pgrep -x ashgrove_server >/dev/null 2>&1; then
        echo "Stopping existing ashgrove_server..."
        pkill -x ashgrove_server || true
        for _ in $(seq 1 30); do
            pgrep -x ashgrove_server >/dev/null 2>&1 || break
            sleep 1
        done
    fi
    if screen -ls 2>/dev/null | grep -q "\.${SCREEN_NAME}"; then
        screen -S "${SCREEN_NAME}" -X quit 2>/dev/null || true
    fi
    rm -f "${PID_FILE}"
}

start_server() {
    stop_server
    echo "Launching server in screen session '${SCREEN_NAME}' on port ${PORT}..."
    screen -dmS "${SCREEN_NAME}" bash -c \
        "cd ${APP_DIR} && exec ./build/ashgrove_server ${PORT} > /tmp/server.log 2>&1"
    # Wait for the HTTP endpoint to come up (model load can take ~30-60s).
    echo "Waiting for server to listen on :${PORT}..."
    for _ in $(seq 1 90); do
        if pgrep -x ashgrove_server >/dev/null 2>&1; then
            if curl -s -m 2 "http://localhost:${PORT}/state" -o /dev/null 2>/dev/null; then
                echo "Server is UP on :${PORT}."
                pgrep -x ashgrove_server | head -1 > "${PID_FILE}"
                exit 0
            fi
        else
            echo "Server process exited early! See /tmp/server.log:"
            tail -20 /tmp/server.log 2>/dev/null || true
            exit 1
        fi
    done
    echo "Timed out waiting for server. Last log:"
    tail -20 /tmp/server.log 2>/dev/null || true
    exit 1
}

case "${1:-start}" in
    start) start_server ;;
    stop)  stop_server ;;
    restart) start_server ;;
    status)
        if pgrep -x ashgrove_server >/dev/null 2>&1; then
            echo "RUNNING: $(pgrep -x ashgrove_server | head -1)"
        else
            echo "STOPPED"
        fi
        ;;
    *) echo "usage: $0 {start|stop|restart|status}" >&2; exit 1 ;;
esac