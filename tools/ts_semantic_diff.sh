#!/usr/bin/env bash
# Formatting-free diff of TS files between the C port's TS baseline and current TS.
# The baseline version is run through the TS repo's own Biome config first, so the
# 2026-11-03 tree-wide reformat and SPDX headers drop out and only real changes remain.
# Usage: tools/ts_semantic_diff.sh src/path/file.ts [more.ts ...]
#        tools/ts_semantic_diff.sh --module content/q1   (every code-changed file under src/content/q1)
set -euo pipefail
TS=${TS_REPO:-$(cd "$(dirname "$0")/../../quake-anthology-ts" && pwd)}
BASE=${TS_BASELINE:-53ad56d327a8197994ff01b6852db86e27aa6bf9}
HERE=$(cd "$(dirname "$0")/.." && pwd)
files=()
if [[ ${1:-} == --module ]]; then
  mapfile -t files < <(awk -F'\t' -v m="src/$2/" '($1=="code"||$1=="added"||$1=="types-only") && index($2,m)==1 {print $2}' "$HERE/docs/ts_semantic_drift.tsv")
else files=("$@"); fi
for f in "${files[@]}"; do
  echo "=== $f"
  if git -C "$TS" cat-file -e "$BASE:$f" 2>/dev/null; then
    (cd "$TS" && git show "$BASE:$f" | ./node_modules/.bin/biome format --stdin-file-path="$f" 2>/dev/null || git show "$BASE:$f") \
      | diff -u --label "baseline/$f" --label "current/$f" - "$TS/$f" | grep -v '^+// SPDX-License-Identifier' || true
  else echo "(new file since baseline: read $TS/$f in full)"; fi
done
