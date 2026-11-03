#!/usr/bin/env bash
# Private build tree so concurrent porting work cannot race on the shared
# archive. Seeds from build/ on first use so only changed sources recompile.
# Usage: tools/agent_build.sh NAME [make targets...]; prints BUILD path with no targets.
set -euo pipefail
name=$1; shift
dir=build/agents/$name
if [ ! -d "$dir" ]; then
  mkdir -p "$dir"
  ( cd build && tar --exclude=./agents -cf - . ) | tar -xpf - -C "$dir"
  find "$dir" -name '*.d' -exec sed -i "s#^build/#$dir/#" {} +
fi
# Drop objects make cannot keep current: ones whose source was deleted (they
# would still be archived and linked) and ones with no .d file (make would
# never rebuild them after a header change).
while IFS= read -r obj; do
  rel=${obj#"$dir"/}; src=${rel%.o}.c
  if [ ! -f "$src" ] || [ ! -f "${obj%.o}.d" ]; then rm -f "$obj" "${obj%.o}.d"; fi
done < <(find "$dir/src" "$dir/tests" "$dir/app" -name '*.o' 2>/dev/null)
if [ $# -eq 0 ]; then echo "$dir"; exit 0; fi
exec make BUILD="$dir" -j"${AGENT_JOBS:-1}" "$@"
