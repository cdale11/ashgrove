#!/usr/bin/env bash
# Dev orchestrator: builds + runs the C++ game server and the web client.
# Usage: ./dev.sh [--port 8000] [--skip-build]
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PORT=8000
SKIP_BUILD=false
CONDA_PROFILE="${CONDA_PROFILE:-$HOME/miniconda3/etc/profile.d/conda.sh}"

for arg in "$@"; do
  case "$arg" in
    --skip-build) SKIP_BUILD=true ;;
    --port=*) PORT="${arg#*=}" ;;
    *) echo "Unknown arg: $arg"; exit 1 ;;
  esac
done

if [ ! -f "$CONDA_PROFILE" ]; then
  echo "conda profile not found at $CONDA_PROFILE. Set CONDA_PROFILE." >&2
  exit 1
fi

# shellcheck source=/dev/null
source "$CONDA_PROFILE"
conda activate ashgrove

if [ "$SKIP_BUILD" = false ]; then
  cmake -B "$ROOT/build" -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_PREFIX_PATH="$CONDA_PREFIX" \
    -DCMAKE_POLICY_VERSION_MINIMUM=3.5 >/dev/null
  cmake --build "$ROOT/build" -j"$(nproc)"
fi

echo "Starting Ashgrove game server on port $PORT ..."
"$ROOT/build/bin/ashgrove_server" --port "$PORT" &
SERVER_PID=$!

cleanup() {
  kill "$SERVER_PID" 2>/dev/null || true
}
trap cleanup EXIT INT TERM

echo "Starting web client (http://localhost:5173) ..."
cd "$ROOT/frontend"
npm run dev
