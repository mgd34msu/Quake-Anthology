#!/usr/bin/env python3
"""Exercise the primitive checker with C definitions, storage and call sites."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile

sys.dont_write_bytecode = True
import check_primitive_rules as rules


def scan(files):
    with tempfile.TemporaryDirectory(prefix="qa-primitive-rules-") as directory:
        root = Path(directory)
        for name, source in files.items():
            path = root / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(source)
        return rules.check(root)


def has(findings, rule, path=None):
    return any(row["rule"] == rule and (path is None or row["path"] == path)
               for row in findings)


def main():
    actor = "typedef struct qa_actor_owner { uint32_t slot; uint32_t generation; } qa_actor_owner;\n"
    fixtures = []

    # Declarations, literal text and original module ABI records are not owners.
    findings = scan({
        "include/qa/actors.h": actor + "typedef uint64_t qa_actor_id;\n",
        "src/world/actors.c": "qa_actor_id qa_actor_lookup(int slot) { return slot; }\n",
        "src/console/cvars_private.h": "struct qa_cvars { int entries; };\n",
        "src/console/cvars.c": "int qa_cvars_find(int entry) { return entry; }\n",
        "src/formats/bsp.c": "int qa_bsp_probe(int word) { return word; }\n",
        "src/platform/events.c": "struct qa_platform_events { qa_platform_event slots[64]; };\n",
        "src/app/application/events.c": "void application_event_stream_begin(void) { publish(); }\n",
        "src/compat/guest.c": """
typedef struct qa_cvars qa_cvars;
typedef struct cvar_s { char *name; char *string; } cvar_t;
// struct qa_actor_owner { int duplicate; }; qa_sha256_update(); SDL_PollEvent(0);
const char *example = "qa_sha256_update(); struct qa_cvars { int duplicate; };";
void consume(void) { qa_builtin_event local[16]; qa_platform_events_poll(); }
struct borrowed_view { const qa_builtin_event *events; qa_builtin_event last; };
""" + "// continued comment \\\nqa_sha256_update();\n"
            + 'const char *continued = "literal \\\nqa_sha256_update()";\n',
    })
    assert not findings, findings
    fixtures.append("canonical owners, forward declarations, ABI adapters, comments and literals")

    findings = scan({
        "include/qa/actors.h": actor,
        "src/other/actors.c": actor + "typedef uint64_t qa_actor_id;\n"
            + "struct renamed_owner { uint32_t slot; uint32_t generation; };\n"
            + "int qa_actor_lookup(int slot) { return slot; }\n",
        "src/other/cvars.c": "struct qa_cvars { int entries; };\n"
            + "int qa_cvars_find(int entry) { return entry; }\n",
        "src/other/aliases.c": "typedef struct second_registry qa_cvars;\n",
        "src/other/bsp.c": "int qa_bsp_probe(int word) { return word; }\n",
        "src/other/identity.c": "int qa_fs_identity_equal(int value) { return value; }\n",
    })
    assert has(findings, "primitive-type-owner", "src/other/actors.c")
    assert has(findings, "primitive-type-owner", "src/other/cvars.c")
    assert has(findings, "primitive-type-owner", "src/other/aliases.c")
    assert has(findings, "primitive-function-owner", "src/other/bsp.c")
    assert has(findings, "primitive-function-owner", "src/other/actors.c")
    assert has(findings, "primitive-function-owner", "src/other/cvars.c")
    assert has(findings, "identity-function-owner", "src/other/identity.c")
    assert has(findings, "duplicate-primitive-layout", "src/other/actors.c")
    fixtures.append("same-name ownership and renamed complete primitive layout")

    findings = scan({"src/other/copies.c": """
static int first_identity(int input) { return input > 4 ? input : 4; }
static int second_identity(int value) { return value > 4 ? value : 4; }
static int different_identity(int value) { return value > 5 ? value : 5; }
static float other_type_identity(int value) { return value > 4 ? value : 4; }
static const char *left_identity(int input) { return input ? "input" : "none"; }
static const char *right_identity(int value) { return value ? "value" : "none"; }
static int first_stamp(struct state *input) { return input->input; }
static int second_stamp(struct state *value) { return value->value; }
static int declaration_identity(int value);
#define GENERATED_IDENTITY(name) int name(int x) { return x; }
"""})
    copies = [row for row in findings if row["rule"] == "duplicate-primitive-body"]
    assert len(copies) == 1, findings
    assert {row["symbol"] for row in copies[0]["related"]} == {
        "first_identity", "second_identity"}, findings
    assert copies[0]["confidence"] == "candidate"
    fixtures.append("exact body tokens, parameter names normalized, fields/constants/literals retained")

    findings = scan({
        "src/platform/events.c": "struct ring { qa_platform_event entries[64]; };\n"
            + "void ingress(void) { SDL_PollEvent(0); recvmsg(fd, msg, 0); clock_gettime(0, t); SDL_CreateThread(worker, name, data); }\n",
        "src/app/duplicate.c": "struct ring { qa_builtin_event entries[64]; };\n"
            + "void frame(void) { SDL_PollEvent(0); recvmsg(fd, msg, 0); clock_gettime(0, t); SDL_CreateThread(worker, name, data); }\n",
        "src/app/borrowed.c": "struct view { const qa_builtin_event *events; qa_builtin_event last; };\n"
            + "void read(void) { qa_builtin_event local[64]; }\n",
    })
    for rule in ("event-buffer-owner", "platform-event-source",
                 "platform-network-source", "platform-job-source"):
        assert has(findings, rule, "src/app/duplicate.c"), findings
    assert not any(row["path"] != "src/app/duplicate.c" for row in findings), findings
    fixtures.append("typed event storage and OS ingress ownership, stack/borrowed views preserved")

    findings = scan({
        "src/app/hash.c": "void fingerprint(void) { qa_sha256_update(ctx, bytes); }\n",
        "src/tools/llm_auth.c": "void oauth_pkce(void) { sha256(bytes, out); }\n",
        "src/core/buckets.c": "int material_hash(int key) { return key & 63; }\n",
    })
    assert has(findings, "crypto-hash", "src/app/hash.c"), findings
    assert not any(row["path"] != "src/app/hash.c" for row in findings), findings
    fixtures.append("external PKCE exception and allowed hash-table bucket keys")

    # Report-only is the default; strict does not turn review candidates into proof.
    checker = Path(rules.__file__).resolve()
    with tempfile.TemporaryDirectory(prefix="qa-primitive-cli-") as directory:
        root = Path(directory)
        path = root / "src/other/copies.c"
        path.parent.mkdir(parents=True)
        path.write_text("int first_identity(int x) { return x + 1; }\n"
                        "int second_identity(int y) { return y + 1; }\n")
        command = [sys.executable, str(checker), "--root", str(root)]
        for suffix in ([], ["--strict"]):
            result = subprocess.run(command + suffix, text=True, capture_output=True)
            assert result.returncode == 0, result.stderr
            assert json.loads(result.stdout)["candidates"] > 0
        path.write_text("int qa_actor_copy(int x) { return x + 1; }\n")
        default = subprocess.run(command, text=True, capture_output=True)
        strict = subprocess.run(command + ["--strict"], text=True, capture_output=True)
        assert default.returncode == 0 and strict.returncode == 1
        assert json.loads(strict.stdout)["ownership_violations"] > 0
    fixtures.append("default reports debt; opt-in strict rejects ownership, not candidates")
    print(json.dumps({"result": "PASS", "behavioral_fixtures": fixtures}, indent=2))


if __name__ == "__main__":
    main()
