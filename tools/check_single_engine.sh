#!/usr/bin/env bash
set -euo pipefail

python3 - "${1:-HEAD}" <<'PY'
import re
import subprocess
import sys

base = sys.argv[1]
diff = subprocess.run(
    ["git", "diff", "--no-ext-diff", "--src-prefix=a/", "--dst-prefix=b/",
     "--unified=0", base, "--", "src", "include"],
    check=True, text=True, capture_output=True,
).stdout
rules = [
    ("enum boundary", re.compile(r"\(\s*qa_(?:console_dialect|movement_kind|clock_kind|game_family)\s*\)")),
    ("collision family", re.compile(r"\bQA_COLLISION_Q[23]\b")),
    ("capability owner", re.compile(r"\b(?:qa_hud_create|qa_audio_engine_play|qa_audio_bank_create|qa_[A-Za-z0-9_]*particle[A-Za-z0-9_]*pool[A-Za-z0-9_]*)\s*\(")),
    ("family dispatch", re.compile(r"(?:->family\s*==\s*QA_GAME_Q|\bkind\s*==\s*APPLICATION_PROVIDER_Q)")),
]
path = ""
line = 0
new_file = False
hits = 0
for record in diff.splitlines():
    if record.startswith("--- "):
        new_file = record == "--- /dev/null"
    elif record.startswith("+++ b/"):
        path = record[6:]
        name = path.rsplit("/", 1)[-1]
        if new_file and path.startswith("src/app/") and (
            re.fullmatch(r"remote_.*_effects.*\.c", name) or name.endswith("_console.c")
        ):
            print(f"{path}:1 [new adapter implementation]")
            hits += 1
    elif record.startswith("@@"):
        match = re.search(r"\+(\d+)", record)
        line = int(match.group(1)) if match else 0
    elif record.startswith("+"):
        text = record[1:]
        for label, pattern in rules:
            if label == "collision family" and path.startswith("src/world/"):
                continue
            if label == "family dispatch" and not (
                path.startswith("src/app/frontend/") or
                path.startswith("src/app/application/") and
                re.search(r"presentation|sound|audio|effect", path)
            ):
                continue
            if pattern.search(text):
                print(f"{path}:{line} [{label}] {text.strip()}")
                hits += 1
        line += 1
    elif record.startswith(" "):
        line += 1
print(f"{hits} new occurrences to compare with docs/unification-inventory.md")
print("Enum boundaries require source-type review: serialized values and original ABI values are not cross-domain conversions.")
PY
