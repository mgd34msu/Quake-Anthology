"""Translate authored JSON animation rows into immutable C data, without executing TypeScript."""

import json
import pathlib
import re
import sys


def literal(value):
    return json.dumps(value, ensure_ascii=True)


def number(value):
    return f"{float(value):.9g}" + ("f" if "." in f"{float(value):.9g}" or "e" in f"{float(value):.9g}" else ".0f")


def addon_rows():
    rows = []
    def action(name):
        return {"kind": "action", "name": name}
    def move(mode, distance=0):
        return {"kind": "ai", "mode": mode, "distance": distance}
    def sound(path, chance=None, attenuation=1):
        return {"kind": "sound", "path": path, "chance": chance, "channel": "voice",
                "attenuation": attenuation, "comparison": "less"}
    def frame(name, pose, next_name, operations):
        rows.append([name, {"frame": pose, "next": next_name, "operations": operations}])
    def sequence(name, first, distances, mode, end, additions=None, before=False):
        for step, distance in enumerate(distances, 1):
            ai = [] if mode is None else [move(mode, distance)]
            extra = (additions or {}).get(step, [])
            frame(name + str(step), first + step - 1,
                  name + str(step + 1) if step < len(distances) else end,
                  extra + ai if before else ai + extra)

    idle = sound("soldier/idle.wav", 0.2, 2)
    sequence("infected_army_stand", 0, [0]*8, "stand", "infected_army_stand1")
    sequence("infected_army_walk", 90, [1,1,1,1,2,3,4,4,2,2,2,1,0,1,1,1,3,3,3,3,2,1,1,1], "walk", "infected_army_walk1", {1:[idle]}, True)
    sequence("infected_army_run", 73, [11,15,10,10,8,15,10,8], "run", "infected_army_run1", {1:[idle]}, True)
    sequence("infected_army_atk", 81, [0]*9, "face", "infected_army_run1",
             {5:[action("grunt:army_atk5")], 7:[action("grunt:army_atk7")]})
    sequence("infected_army_pain", 40, [0]*6, None, "infected_army_run1", {6:[move("pain",1)]})
    sequence("infected_army_painb", 46, [0]*14, None, "infected_army_run1",
             {2:[move("painforward",13)],3:[move("painforward",9)],12:[move("pain",2)]})
    sequence("infected_army_painc", 60, [0]*13, None, "infected_army_run1",
             {2:[move("pain",1)],5:[move("painforward",1)],6:[move("painforward",1)],
              8:[move("pain",1)],9:[move("painforward",4)],10:[move("painforward",3)],
              11:[move("painforward",6)],12:[move("painforward",8)]})
    for corpse, pose in [(1,53),(2,62)]:
        prefix = f"hknight_corpse{corpse}"
        frame(prefix, pose, prefix + "_2", [{"kind":"solid","solid":"none"}])
        frame(prefix + "_2", pose, prefix + "_2", [action("infected_corpse_hold")])
        frame(prefix + "_rise0", pose, prefix + f"_rise{corpse}", [action("infected_test_rise")])
        poses = [53,52,51,50,49,48,47,46,45,44,43,42,0,1] if corpse == 1 else [62,61,60,59,58,57,56,55,55,0,1]
        distances = {4:-11,5:-10,10:-7,11:-8,14:-10} if corpse == 1 else {}
        for step, model in enumerate(poses,1):
            ops = [action("infected_rise_pain")] if step == 2 else []
            if step in distances:
                ops.append(move("forward", distances[step]))
            if step == len(poses):
                ops.append(action("infected_resurrect"))
            frame(prefix + f"_rise{step}", model,
                  "hknight_run1" if step == len(poses) else prefix + f"_rise{step+1}", ops)

    idle = sound("dog/idle.wav", 0.2, 2)
    sequence("demodog_stand", 69, [0]*9, "stand", "demodog_stand1")
    sequence("demodog_walk", 78, [8]*8, "walk", "demodog_walk1", {1:[idle]}, True)
    sequence("demodog_run", 48, [16,32,32,20,64,32,16,32,32,20,64,32], "run", "demodog_run1", {1:[idle]}, True)
    sequence("demodog_atta", 0, [0]*8, None, "demodog_run1",
             {i: [sound("dog/dattack1.wav"),action("demodog_bite")] if i == 4 else [move("charge",10)] for i in range(1,9)})
    sequence("demodog_leap", 60, [0]*9, None, "demodog_leap9",
             {1:[move("face")],2:[move("face"),action("demodog_jump")]})
    sequence("demodog_pain", 26, [0]*6, None, "demodog_run1")
    sequence("demodog_painb", 32, [0]*16, None, "demodog_run1",
             {i:[move("pain",v)] for i,v in [(3,4),(4,12),(5,12),(6,2),(8,4),(10,10)]})
    sequence("demodog_die", 8, [0]*9, None, "demodog_die9")
    sequence("demodog_dieb", 17, [0]*9, None, "demodog_dieb9")
    return rows


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
               donor / "missionpacks/monsters/tables/hipgrem.ts",
               donor / "addons/monsters/heavy/tables/mg3_super_shambler.ts",
               donor / "addons/monsters/heavy/tables/mg3_rknight.ts",
               donor / "addons/monsters/heavy/tables/mg3_lavaman.ts"]
    rows = []
    for source in sources:
        for line in source.read_text().splitlines():
            match = re.fullmatch(r"\s*(\[\"[^\"]+\",\s*\{.*\}\]),?\s*", line)
            if match:
                row = json.loads(match.group(1))
                if source.name == "mg3_lavaman.ts":
                    row[0] = "mg3_" + row[0]
                    row[1]["next"] = "mg3_" + row[1]["next"]
                rows.append(row)
    rows.extend(addon_rows())
    virtual_actions = ["sword_pause", "mummy_wake", "mummy_missile", "wrath_attack", "overlord_missile", "morph_wake", "dragon_activate", "dragon_boom2", "armagon_missile_attack", "Gremlin_MeleeAttack", "Gremlin_MissileAttack", "gremlin_gib"]
    virtual_actions.extend(["supsham_melee", "supsham_missile", "rknight_magic", "rknight_run", "rknight_melee"])
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
    pairs = [(indices[name[9:]], indices[name]) for name in indices
             if name.startswith("infected_army_") and name[9:] in indices]
    output.extend(["uint16_t q1_infected_frame(uint16_t frame) {", "    switch (frame) {"])
    output.extend(f"    case {source}: return {target};" for source, target in pairs)
    output.extend(["    default: return frame;", "    }", "}", ""])
    pairs = [(indices[name[4:]], indices[name]) for name in indices
             if name.startswith("mg3_lavaman_") and name[4:] in indices]
    output.extend(["uint16_t q1_mg3_lavaman_frame(uint16_t frame) {", "    switch (frame) {"])
    output.extend(f"    case {source}: return {target};" for source, target in pairs)
    output.extend(["    default: return frame;", "    }", "}", ""])
    destination.write_text("\n".join(output))


if __name__ == "__main__":
    main()
