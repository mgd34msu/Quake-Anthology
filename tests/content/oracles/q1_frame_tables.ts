// Oracle for tests/content/test_q1_frame_tables.c (TS tests/content/q1-monster-frame-tables.test.ts):
// the TS test proves every committed Q1 monster frame table equals the table generated
// from the shipped progs.dat; this dumps those committed tables, plus the frame maps the
// add-on controllers compose from them, so the C tables can be compared entry for entry.
// Writes tests/content/oracles/q1_frame_tables.txt. Run from the C repository root:
//   bun tests/content/oracles/q1_frame_tables.ts
import { writeFileSync } from "node:fs";
import type { MonsterFrame, MonsterOperation } from "../../../../quake-anthology-ts/src/content/q1/base/animation.ts";
import { monsterFrames } from "../../../../quake-anthology-ts/src/content/q1/base/frames.ts";
import { demodogFrames, dogFrames } from "../../../../quake-anthology-ts/src/content/q1/addons/monsters/demodog-frames.ts";
import { infectedFrames } from "../../../../quake-anthology-ts/src/content/q1/addons/monsters/infected/frames.ts";
import { classicFrames } from "../../../../quake-anthology-ts/src/content/q1/addons/monsters/ordinary/classic.ts";
import { chthonFrames } from "../../../../quake-anthology-ts/src/content/q1/addons/monsters/ordinary/chthon.ts";
import { mg3NightmareFrames } from "../../../../quake-anthology-ts/src/content/q1/addons/monsters/ordinary/nightmare.ts";
import { rocketOgreFrames } from "../../../../quake-anthology-ts/src/content/q1/addons/monsters/ordinary/rocket-ogre.ts";
import { frames as finalFrames } from "../../../../quake-anthology-ts/src/content/q1/addons/monsters/bosses/frames/final.ts";
import { frames as ghostFrames } from "../../../../quake-anthology-ts/src/content/q1/addons/monsters/bosses/frames/ghost.ts";
import { frames as oldnewFrames } from "../../../../quake-anthology-ts/src/content/q1/addons/monsters/bosses/frames/oldnew.ts";
import { frames as orbFrames } from "../../../../quake-anthology-ts/src/content/q1/addons/monsters/bosses/frames/orb.ts";
import { frames as szombieFrames } from "../../../../quake-anthology-ts/src/content/q1/addons/monsters/bosses/frames/szombie.ts";
import { frames as lavamanFrames } from "../../../../quake-anthology-ts/src/content/q1/addons/monsters/heavy/tables/mg3_lavaman.ts";
import { frames as rknightFrames } from "../../../../quake-anthology-ts/src/content/q1/addons/monsters/heavy/tables/mg3_rknight.ts";
import { frames as supshamFrames } from "../../../../quake-anthology-ts/src/content/q1/addons/monsters/heavy/tables/mg3_super_shambler.ts";
import { frames as gruntFrames } from "../../../../quake-anthology-ts/src/content/q1/missionpacks/monsters/tables/grunt.ts";
import { frames as rottweilerFrames } from "../../../../quake-anthology-ts/src/content/q1/missionpacks/monsters/tables/rottweiler.ts";
import { frames as hiparmaFrames } from "../../../../quake-anthology-ts/src/content/q1/missionpacks/monsters/tables/hiparma.ts";
import { frames as hipdecoyFrames } from "../../../../quake-anthology-ts/src/content/q1/missionpacks/monsters/tables/hipdecoy.ts";
import { frames as hipgremFrames } from "../../../../quake-anthology-ts/src/content/q1/missionpacks/monsters/tables/hipgrem.ts";
import { frames as hipscrgeFrames } from "../../../../quake-anthology-ts/src/content/q1/missionpacks/monsters/tables/hipscrge.ts";
import { frames as dragonFrames } from "../../../../quake-anthology-ts/src/content/q1/missionpacks/monsters/tables/dragon.ts";
import { frames as eelFrames } from "../../../../quake-anthology-ts/src/content/q1/missionpacks/monsters/tables/eel.ts";
import { frames as invisSwFrames } from "../../../../quake-anthology-ts/src/content/q1/missionpacks/monsters/tables/invis_sw.ts";
import { frames as mpLavamanFrames } from "../../../../quake-anthology-ts/src/content/q1/missionpacks/monsters/tables/lavaman.ts";
import { frames as morphFrames } from "../../../../quake-anthology-ts/src/content/q1/missionpacks/monsters/tables/morph.ts";
import { frames as mummyFrames } from "../../../../quake-anthology-ts/src/content/q1/missionpacks/monsters/tables/mummy.ts";
import { frames as sWrathFrames } from "../../../../quake-anthology-ts/src/content/q1/missionpacks/monsters/tables/s_wrath.ts";
import { frames as wrathFrames } from "../../../../quake-anthology-ts/src/content/q1/missionpacks/monsters/tables/wrath.ts";

function op(value: MonsterOperation): string {
  switch (value.kind) {
    case "ai":
      return `ai:${value.mode}:${value.distance}`;
    case "sound":
      return `sound:${value.path}:${value.channel}:${value.attenuation}:${value.comparison}:${value.chance === null ? "null" : value.chance}`;
    case "solid":
      return `solid:${value.solid}`;
    case "lightstyle":
      return `lightstyle:${value.pattern}`;
    case "action":
      return `action:${value.name}`;
  }
}
const lines: string[] = [];
function table(name: string, frames: ReadonlyMap<string, MonsterFrame>): void {
  lines.push(`table\t${name}\t${frames.size}`);
  for (const [key, frame] of frames) lines.push(`${key}\t${frame.frame}\t${frame.next}\t${frame.operations.map(op).join("|")}`);
}
// ordinary/index.ts hangFrames and sourceFrames(context, rocket).
const hang = monsterFrames.get("zombie_paine1");
if (hang === undefined) throw new Error("Missing native zombie hang model frame");
const hangFrames: ReadonlyMap<string, MonsterFrame> = new Map([["zombie_hang1", { frame: hang.frame, next: "zombie_hang1", operations: [] }]]);
const sourceFrames = (program: string, rocket: boolean): ReadonlyMap<string, MonsterFrame> =>
  new Map([
    ...hangFrames,
    ...classicFrames(program === "mg1" ? "mg1" : "mg3"),
    ...chthonFrames,
    ...(program === "mg3" ? mg3NightmareFrames : []),
    ...(rocket ? rocketOgreFrames : []),
  ]);

table("monsterFrames", monsterFrames);
table("demodogFrames", demodogFrames);
table("dogFrames", dogFrames);
table("infectedFrames", infectedFrames);
table("hangFrames", hangFrames);
table("sourceFrames:mg1", sourceFrames("mg1", false));
table("sourceFrames:mg1:rocket", sourceFrames("mg1", true));
table("sourceFrames:mg3", sourceFrames("mg3", false));
table("sourceFrames:mg3:rocket", sourceFrames("mg3", true));
table("sourceFrames:other", sourceFrames("dopa", false));
table("sourceFrames:other:rocket", sourceFrames("dopa", true));
table("bosses/final", finalFrames);
table("bosses/ghost", ghostFrames);
table("bosses/oldnew", oldnewFrames);
table("bosses/orb", orbFrames);
table("bosses/szombie", szombieFrames);
table("heavy/mg3_lavaman", lavamanFrames);
table("heavy/mg3_rknight", rknightFrames);
table("heavy/mg3_super_shambler", supshamFrames);
table("mp/grunt", gruntFrames);
table("mp/rottweiler", rottweilerFrames);
table("mp/hiparma", hiparmaFrames);
table("mp/hipdecoy", hipdecoyFrames);
table("mp/hipgrem", hipgremFrames);
table("mp/hipscrge", hipscrgeFrames);
table("mp/dragon", dragonFrames);
table("mp/eel", eelFrames);
table("mp/invis_sw", invisSwFrames);
table("mp/lavaman", mpLavamanFrames);
table("mp/morph", morphFrames);
table("mp/mummy", mummyFrames);
table("mp/s_wrath", sWrathFrames);
table("mp/wrath", wrathFrames);
writeFileSync("tests/content/oracles/q1_frame_tables.txt", `${lines.join("\n")}\n`);
