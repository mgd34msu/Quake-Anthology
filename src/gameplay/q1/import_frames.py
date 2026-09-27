"""Translate authored JSON animation rows into immutable C data, without executing TypeScript."""

import json
import pathlib
import re
import sys


def literal(value):
    return json.dumps(value, ensure_ascii=True)


def number(value):
    return f"{float(value):.9g}" + ("f" if "." in f"{float(value):.9g}" or "e" in f"{float(value):.9g}" else ".0f")


def main():
    donor = pathlib.Path(sys.argv[1])
    destination = pathlib.Path(sys.argv[2])
    sources = [donor / "base/frames.ts", donor / "missionpacks/monsters/tables/grunt.ts",
               donor / "missionpacks/monsters/tables/rottweiler.ts",
               donor / "missionpacks/monsters/tables/eel.ts",
               donor / "missionpacks/monsters/tables/invis_sw.ts",
               donor / "missionpacks/monsters/tables/mummy.ts",
               donor / "missionpacks/monsters/tables/hipscrge.ts",
               donor / "missionpacks/monsters/tables/hipdecoy.ts",
               donor / "missionpacks/monsters/tables/wrath.ts",
               donor / "missionpacks/monsters/tables/lavaman.ts",
               donor / "missionpacks/monsters/tables/s_wrath.ts",
               donor / "missionpacks/monsters/tables/morph.ts",
               donor / "missionpacks/monsters/tables/dragon.ts",
               donor / "missionpacks/monsters/tables/hiparma.ts",
               donor / "missionpacks/monsters/tables/hipgrem.ts"]
    rows = []
    for source in sources:
        for line in source.read_text().splitlines():
            match = re.fullmatch(r"\s*(\[\"[^\"]+\",\s*\{.*\}\]),?\s*", line)
            if match:
                rows.append(json.loads(match.group(1)))
    virtual_actions = ["sword_pause", "mummy_wake", "mummy_missile", "wrath_attack", "overlord_missile", "morph_wake", "dragon_activate", "dragon_boom2", "armagon_missile_attack", "Gremlin_MeleeAttack", "Gremlin_MissileAttack", "gremlin_gib"]
    zombie_hang = next(frame["frame"] for name, frame in rows if name == "zombie_paine1")
    rows.append(["zombie_hang1", {"frame": zombie_hang, "next": "zombie_hang1", "operations": []}])
    names = {name for name, _ in rows}
    for name in virtual_actions:
        if name not in names:
            rows.append([name, {"frame": 65535, "next": name, "operations": [{"kind": "action", "name": name}]}])
    indices = {name: index for index, (name, _) in enumerate(rows)}
    action_names = sorted({operation["name"] for _, frame in rows for operation in frame["operations"] if operation["kind"] == "action"})
    action_id = lambda name: "Q1_ACTION_" + re.sub(r"[^A-Za-z0-9]+", "_", name.replace("-", "_NEG_")).strip("_").upper()
    declarations = ["#ifndef QA_Q1_FRAME_ACTIONS_H", "#define QA_Q1_FRAME_ACTIONS_H", "", "typedef enum q1_frame_action {"]
    declarations.extend("    " + action_id(name) + "," for name in action_names)
    declarations.extend(["    Q1_ACTION_COUNT", "} q1_frame_action;", "", "#endif", ""])
    destination.with_name("frame_actions.h").write_text("\n".join(declarations))
    operations = []
    frames = []
    for name, frame in rows:
        first = len(operations)
        for operation in frame["operations"]:
            kind = operation["kind"]
            if kind == "ai":
                operations.append("{.kind=Q1_FRAME_AI,.ai=Q1_AI_" + operation["mode"].upper() +
                                  ",.distance=" + number(operation["distance"]) + "}")
            elif kind == "sound":
                channel = {"voice": 2, "weapon": 1, "body": 4}[operation["channel"]]
                chance = -1 if operation["chance"] is None else operation["chance"]
                operations.append("{.kind=Q1_FRAME_SOUND,.text=" + literal(operation["path"]) +
                                  f",.channel={channel},.attenuation=" + number(operation["attenuation"]) +
                                  ",.chance=" + number(chance) + ",.greater=" +
                                  ("true" if operation["comparison"] == "greater" else "false") + "}")
            elif kind == "solid":
                if operation["solid"] != "none":
                    raise ValueError(f"Unsupported authored solid value in {name}")
                operations.append("{.kind=Q1_FRAME_SOLID}")
            elif kind == "lightstyle":
                operations.append("{.kind=Q1_FRAME_LIGHTSTYLE,.text=" + literal(operation["pattern"]) + "}")
            elif kind == "action":
                operations.append("{.kind=Q1_FRAME_ACTION,.action=" + action_id(operation["name"]) + "}")
            else:
                raise ValueError(f"Unknown authored frame operation: {kind}")
        frames.append("{" + literal(name) + f",{frame['frame']},{indices[frame['next']]},{first},{len(operations)-first}" + "}")
    output = ['#include "internal.h"', "", "const q1_frame_operation q1_frame_operations[] = {"]
    output.extend("    " + entry + "," for entry in operations)
    output.extend(["};", "", "const q1_frame q1_frames[] = {"])
    output.extend("    " + entry + "," for entry in frames)
    output.extend(["};", "const size_t q1_frame_count = sizeof(q1_frames) / sizeof(*q1_frames);", "",
                   "static const uint16_t name_order[] = {"])
    order = [str(indices[name]) for name in sorted(indices)]
    output.extend("    " + ",".join(order[index:index+20]) + "," for index in range(0, len(order), 20))
    output.extend(["};", "", "uint16_t q1_frame_index(const char *name) {",
                   "    size_t first = 0, end = sizeof(name_order) / sizeof(*name_order);",
                   "    while (first < end) {",
                   "        size_t middle = first + (end - first) / 2;",
                   "        uint16_t index = name_order[middle];",
                   "        int comparison = strcmp(name, q1_frames[index].name);",
                   "        if (comparison == 0) return index;",
                   "        if (comparison < 0) end = middle; else first = middle + 1;",
                   "    }", "    return UINT16_MAX;", "}", ""])
    destination.write_text("\n".join(output))


if __name__ == "__main__":
    main()
