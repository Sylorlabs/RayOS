#!/usr/bin/env bash
# view_live.sh — watch RayOS desktop/live_frame.ppm with feh or eog when available.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
FRAME="${ROOT}/desktop/live_frame.ppm"

if [[ ! -f "$FRAME" ]]; then
    echo "view_live: waiting for ${FRAME} ..." >&2
    while [[ ! -f "$FRAME" ]]; do
        sleep 0.25
    done
fi

if command -v feh >/dev/null 2>&1; then
    exec feh --auto-zoom --reload 0.25 "$FRAME"
fi

if command -v eog >/dev/null 2>&1; then
    exec eog --watch "$FRAME"
fi

echo "view_live: install feh or eog to preview ${FRAME}" >&2
echo "view_live: frame path: ${FRAME}" >&2
exit 1