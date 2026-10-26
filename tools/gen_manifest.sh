#!/bin/sh
# Regenerates docs/manifest.tsv: every TypeScript source file, its C target,
# and whether the C file exists yet. Usage: tools/gen_manifest.sh [ts-src-dir]
set -eu
TS_SRC="${1:-$HOME/Projects/quake-anthology-ts/src}"
OUT="docs/manifest.tsv"
printf 'ts_source\tc_source\theader\tstatus\n' > "$OUT.tmp"
find "$TS_SRC" -type f | sort | while IFS= read -r f; do
  rel=${f#"$TS_SRC"/}
  case "$rel" in
    *.ts)
      base=${rel%.ts}
      cbase=$(printf '%s' "$base" | tr '-' '_')
      csrc="src/$cbase.c"
      hdr="include/qa/$cbase.h"
      if [ -f "$csrc" ]; then status=ported; else status=pending; fi
      printf 'src/%s\t%s\t%s\t%s\n' "$rel" "$csrc" "$hdr" "$status" >> "$OUT.tmp"
      ;;
    *)
      printf 'src/%s\t-\t-\tnon-ts\n' "$rel" >> "$OUT.tmp"
      ;;
  esac
done
mv "$OUT.tmp" "$OUT"
ported=$(awk -F'\t' '$4=="ported"' "$OUT" | wc -l)
pending=$(awk -F'\t' '$4=="pending"' "$OUT" | wc -l)
echo "manifest: $ported ported, $pending pending -> $OUT"
