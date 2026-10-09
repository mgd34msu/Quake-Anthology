#!/usr/bin/env python3
"""Report concrete shared-primitive ownership debt; --strict is opt-in."""
import argparse
import json
from pathlib import Path
import re


TYPE_OWNERS = {
    "qa_actor_id": "include/qa/actors.h",
    "qa_saved_actor_id": "include/qa/actors.h",
    "qa_actor_owner": "include/qa/actors.h",
    "qa_actor_registry": "src/world/actors.c",
    "qa_string_id": "include/qa/strings.h",
    "qa_strings": "src/core/strings.c",
    "qa_item_id": "include/qa/gameplay.h",
    "qa_inventory": "src/gameplay/inventory_internal.h",
    "qa_damage_request": "include/qa/gameplay.h",
    "qa_damage_result": "include/qa/gameplay.h",
    "qa_trace_result": "include/qa/collision.h",
    "qa_movement_command": "include/qa/movement.h",
    "qa_movement_input": "include/qa/movement.h",
    "qa_q3_fire_stamp": "include/qa/game_q3.h",
    "qa_fs_identity": "include/qa/filesystem.h",
    "qa_platform_event": "include/qa/platform_events.h",
    "qa_platform_events": "src/platform/events.c",
    "application_event_envelope": "src/app/application/event_stream.h",
    "qa_render_workers": "src/render/cpu/raster.c",
    "qa_render_model_job": "include/qa/render_workers.h",
    "qa_cvars": "src/console/cvars_private.h",
    "cvar_store": "src/console/cvars_private.h",
    "qa_cvar_binding": "include/qa/console.h",
    "qa_cvar_handle": "include/qa/console.h",
    "qa_scheduler": "src/session/scheduler.c",
}
FUNCTION_OWNERS = {
    "qa_bsp_probe": "src/formats/bsp.c",
    "qa_bsp_open": "src/formats/bsp.c",
    "qa_actor_": "src/world/",
    "qa_strings_": "src/core/",
    "qa_cvars_": "src/console/",
    "qa_platform_events_": "src/platform/",
    "application_event_stream_": "src/app/application/events.c",
    "qa_render_workers_": "src/render/cpu/raster.c",
}
LEXEMES = re.compile(
    r"//(?:\\[\s\S]|[^\n\\])*|/\*[\s\S]*?\*/|\"(?:\\[\s\S]|[^\"\\])*\"|'(?:\\[\s\S]|[^'\\])*'"
)
TOKEN = re.compile(r'"(?:\\[\s\S]|[^"\\])*"|\'(?:\\[\s\S]|[^\'\\])*\'|[A-Za-z_]\w*|\d+(?:\.\d*)?|[^\s]')
PRIMITIVE_NAME = re.compile(r"stamp|identity|queue|(?:^|_)jobs?(?:_|$)|cvars?|actor_id")
EVENT_PAYLOAD = r"(?:qa_platform_event|qa_builtin_event|qa_application_q2_map_event|qa_application_q2_player_event|qa_application_protocol_event)"
EVENT_OWNERS = {"src/platform/events.c", "src/app/application/event_stream.h"}


def mask(text, strings=True):
    def replace(match):
        value = match[0]
        if not strings and not value.startswith(("//", "/*")):
            return value
        return "".join("\n" if char == "\n" else " " for char in value)
    return LEXEMES.sub(replace, text)


def closing(code, start, opening, end):
    depth = 0
    for position in range(start, len(code)):
        if code[position] == opening:
            depth += 1
        elif code[position] == end:
            depth -= 1
            if not depth:
                return position
    return len(code) - 1


def blocks(code):
    """Top-level C bodies, including inline header implementations."""
    boundary, position = 0, 0
    while position < len(code):
        char = code[position]
        if char == "#" and not code[code.rfind("\n", 0, position) + 1:position].strip():
            end = code.find("\n", position)
            while end >= 0 and code[:end].rstrip().endswith("\\"):
                end = code.find("\n", end + 1)
            position = len(code) if end < 0 else end + 1
            boundary = position
            continue
        if char == "{":
            end = closing(code, position, "{", "}")
            yield boundary, position, end
            position, boundary = end + 1, end + 1
        elif char == ";":
            boundary = position + 1
            position += 1
        else:
            position += 1


def definitions(code, text, path):
    records, functions = [], []
    for start, opening, end in blocks(code):
        head = code[start:opening].strip()
        record = re.search(r"\b(struct|union|enum)\s+(\w+)\s*$", head)
        if record:
            records.append((record[2], start + code[start:opening].index(record[2]),
                            tuple(TOKEN.findall(code[opening + 1:end]))))
            continue
        if "typedef" in head or "=" in head or not head.endswith(")"):
            continue
        calls = list(re.finditer(r"\b(\w+)\s*\(", head))
        if not calls:
            continue
        candidate = calls[0]
        name = candidate[1]
        if name in {"if", "while", "for", "switch", "__attribute__", "_Static_assert"}:
            continue
        params_start = head.index("(", candidate.start())
        params_end = closing(head, params_start, "(", ")")
        parameters = head[params_start + 1:params_end]
        arguments = []
        for index, parameter in enumerate(parameters.split(",")):
            names = re.findall(r"\b[A-Za-z_]\w*\b", parameter)
            if len(names) > 1:
                arguments.append((names[-1], f"$arg{index}"))
        signature = parameters
        body = TOKEN.findall(mask(text[opening + 1:end], strings=False))
        for argument, normalized in arguments:
            signature = re.sub(r"\b" + re.escape(argument) + r"\b", normalized, signature)
            body = [normalized if token == argument and not (index and (
                        body[index - 1] == "." or
                        index > 1 and body[index - 2:index] == ["-", ">"]))
                    else token for index, token in enumerate(body)]
        body = ["$self" if token == name else token for token in body]
        return_type = [token for token in TOKEN.findall(head[:candidate.start()])
                       if token not in {"static", "inline", "extern"}]
        functions.append((name, start + code[start:opening].find(name),
                          tuple(return_type + ["("] + TOKEN.findall(signature) + [")"]), tuple(body)))
    # Scalar aliases are definitions; opaque forward aliases are not.
    for match in re.finditer(r"\btypedef\s+(?!struct\b|enum\b|union\b)([^;{}]+)\s+(\w+)\s*;", code):
        if "(" not in match[1] and match[2] in TYPE_OWNERS:
            records.append((match[2], match.start(2), ()))
    for match in re.finditer(r"\btypedef\s+(?:struct|union|enum)\s+(\w+)\s+(\w+)\s*;", code):
        if match[2] in TYPE_OWNERS and match[1] != match[2]:
            records.append((match[2], match.start(2), ()))
    return records, functions


def check(root):
    findings, records, functions = [], [], []
    sources = {}

    def add(path, code, position, rule, detail, confidence="ownership", related=None):
        finding = {"path": path, "line": code[:position].count("\n") + 1,
                   "rule": rule, "confidence": confidence, "detail": detail}
        if related:
            finding["related"] = related
        if finding not in findings:
            findings.append(finding)

    for directory in ("include", "src"):
        for source in sorted((root / directory).rglob("*")):
            if source.suffix not in {".c", ".h"} or not source.is_file():
                continue
            path = source.relative_to(root).as_posix()
            text = source.read_text(errors="replace")
            code = mask(text)
            sources[path] = code
            types, bodies = definitions(code, text, path)
            for name, position, layout in types:
                records.append((name, path, position, layout))
                owner = TYPE_OWNERS.get(name)
                if owner and path != owner:
                    add(path, code, position, "primitive-type-owner", f"{name} is owned by {owner}")
            for name, position, signature, body in bodies:
                functions.append((name, path, position, signature, body))
                for prefix, owner in FUNCTION_OWNERS.items():
                    if name.startswith(prefix) and not (path.startswith(owner) if owner.endswith("/") else path == owner):
                        add(path, code, position, "primitive-function-owner", f"{name} belongs in {owner}")
                if name in {"qa_fs_identity_hash", "qa_fs_identity_equal"} and path != "src/platform/filesystem.c":
                    add(path, code, position, "identity-function-owner", f"{name} is owned by src/platform/filesystem.c")
            for start, opening, end in blocks(code):
                head = code[start:opening]
                if re.search(r"\b(?:struct|union)\b", head) and path not in EVENT_OWNERS:
                    storage = re.search(r"\b" + EVENT_PAYLOAD + r"\s+\w+\s*\[", code[opening:end])
                    if storage:
                        add(path, code, opening + storage.start(), "event-buffer-owner",
                            "Stored engine input/output event array outside its shared queue; inspect retention before migration")
            if path.startswith("src/") and not path.startswith("src/platform/"):
                for match in re.finditer(r"\b(?:clock_gettime|gettimeofday|SDL_GetTicks(?:64|NS)?|SDL_GetPerformanceCounter|SDL_PollEvent|SDL_WaitEvent(?:Timeout)?)\s*\(", code):
                    add(path, code, match.start(), "platform-event-source", "OS time/input enters through src/platform")
                for match in re.finditer(r"\b(?:recvfrom|recvmsg)\s*\(", code):
                    add(path, code, match.start(), "platform-network-source", "OS socket acquisition outside src/platform; distinguish network ingress from module IPC before migrating")
                for match in re.finditer(r"\b(?:SDL_CreateThread|pthread_create|thrd_create)\s*\(", code):
                    add(path, code, match.start(), "platform-job-source", "Thread creation belongs to the shared platform job service")
            if path != "src/tools/llm_auth.c":
                for match in re.finditer(r"\b(?:qa_sha256\w*|sha256\w*|SHA256\w*)\b", code):
                    add(path, code, match.start(), "crypto-hash", "SHA-256 is allowed only for external OAuth PKCE in src/tools/llm_auth.c")

    for name, path, position, layout in records:
        if not layout:
            continue
        for canonical, owner, canonical_position, canonical_layout in records:
            if canonical in TYPE_OWNERS and owner == TYPE_OWNERS[canonical] and name != canonical and layout == canonical_layout:
                add(path, sources[path], position, "duplicate-primitive-layout",
                    f"{name} repeats the complete field tokens of {canonical}; confirm semantics before merging",
                    "candidate", [{"path": owner, "line": sources[owner][:canonical_position].count("\n") + 1, "symbol": canonical}])

    # Compare actual tokens, not computed digests; types/constants/literals stay significant.
    groups = {}
    for name, path, position, signature, body in functions:
        if PRIMITIVE_NAME.search(name) and len(body) >= 5:
            groups.setdefault((signature, body), []).append((name, path, position))
    for group in groups.values():
        if len(group) < 2:
            continue
        participants = [{"path": path, "line": sources[path][:position].count("\n") + 1, "symbol": name} for name, path, position in group]
        first = group[0]
        add(first[1], sources[first[1]], first[2], "duplicate-primitive-body",
            "Identical type/body tokens after parameter/self renaming; inspect ABI/backend semantics",
            "candidate", participants)
    return sorted(findings, key=lambda row: (row["path"], row["line"], row["rule"]))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--strict", action="store_true", help="Return nonzero for ownership violations; no build integration")
    args = parser.parse_args()
    findings = check(args.root)
    violations = [row for row in findings if row["confidence"] == "ownership"]
    print(json.dumps({"result": "DEBT" if findings else "NO_MATCHES", "ownership_violations": len(violations),
                      "candidates": len(findings) - len(violations), "findings": findings,
                      "limits": "Unpreprocessed lexical C scan: macros, renamed/different implementations and reachability need source/runtime review. No findings does not prove semantic uniqueness."}, indent=2))
    return int(args.strict and bool(violations))


if __name__ == "__main__":
    raise SystemExit(main())
