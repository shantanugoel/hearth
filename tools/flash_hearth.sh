#!/usr/bin/env bash
# Build and flash the ordinary firmware; demo also loads a sample hub board.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
MODE="${1:-clean}"
PORT="${2:-/dev/ttyACM0}"

case "$MODE" in
    clean|demo) ;;
    *) echo "Usage: $0 [clean|demo] [serial-port]" >&2; exit 2 ;;
esac

"$ROOT/tools/idf.sh" build
"$ROOT/tools/idf.sh" -p "$PORT" flash

if [ "$MODE" = demo ]; then
    python3 "$ROOT/tools/load_demo.py" --hub "${HEARTH_DEMO_HUB:-http://127.0.0.1:8790}"
else
    echo "Clean firmware flashed. No sample board data was added."
fi
