#!/usr/bin/env bash
# Lists TS files that changed since a baseline snapshot, so ports made from an
# older TS revision get re-checked. The TS repo is edited concurrently; ports read
# whatever was on disk at the time.
# Usage: tools/ts_drift.sh [build/ts-baseline/src-STAMP.tar]   (default: newest)
set -euo pipefail
base=${1:-$(ls -t build/ts-baseline/src-*.tar | head -1)}
tmp=$(mktemp -d build/ts-baseline/cmp.XXXXXX)
trap 'rm -rf "$tmp"' EXIT
tar -C "$tmp" -xf "$base"
diff -rq "$tmp/src" ../quake-anthology-ts/src | sed -E "s#$tmp/##; s#\.\./quake-anthology-ts/##" || true
diff -rq "$tmp/tests" ../quake-anthology-ts/tests | sed -E "s#$tmp/##; s#\.\./quake-anthology-ts/##" || true
