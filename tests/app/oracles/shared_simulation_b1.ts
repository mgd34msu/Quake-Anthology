// Oracle for tests/app/test_shared_simulation_b1.c: runs the real runtime.ts
// SharedSimulation methods (and guest-monsters.ts) on stub `this` objects
// carrying only the members each method reads.
// bun tests/app/oracles/shared_simulation_b1.ts (from quake-anthology-c, with
// ../quake-anthology-ts beside it).
import { SharedSimulation } from "../../../../quake-anthology-ts/src/app/bootstrap/simulation/runtime.ts";
import { filterEntityText, planGuestMonsters } from "../../../../quake-anthology-ts/src/app/bootstrap/simulation/guest-monsters.ts";

const P = SharedSimulation.prototype as any;
const entities = [
  '{ "classname" "worldspawn" }',
  '{ "classname" "info_player_deathmatch" "origin" "0 0 24" }',
  '{ "classname" "info_player_deathmatch" "origin" "512 0 24" }',
  '{ "classname" "info_player_start" "origin" "64 64 0" }',
  '{ "classname" "info_player_deathmatch" "origin" " 1 2 3" }',
  '{ "classname" "info_player_deathmatch" "origin" "-256 768 40" }',
  '{ "classname" "item_health" "origin" "100 100 8" }',
  '{ "classname" "item_health_large" "origin" "-100 300 8" }',
  '{ "classname" "info_flag_blue" "origin" "900 900 16" }',
  '{ "classname" "monster_soldier" "origin" "10 20 30" "angle" "90" }',
  '{ "classname" "monster_tank" "origin" "1 2 3" "targetname" "t1" }',
  '{ "classname" "monster_gunner" "origin" "4 5 6" "spawnflags" "2" }',
  '{ "classname" "monster_berserk" "origin" "7 8 9" "spawnflags" "256" }',
  '{ "classname" "monster_infantry" "origin" "1 1 1" "item" "item_armor_shard" }',
  '{ "classname" "dm_dball_goal" "origin" "300 0 0" }',
  '{ "classname" "dm_dball_goal" "origin" "-300 0 0" }',
  '{ "classname" "dm_dball_team1_start" "origin" "-200 10 0" }',
].join("\n");
const out: string[] = [];
const vec = (v: { x: number; y: number; z: number } | null | undefined) => v == null ? "null" : `${v.x} ${v.y} ${v.z}`;

// filterEntityText: quotes end at the next quote (no escape handling).
const quoted = '{ "classname" "worldspawn" "message" "a\\"}{" }\n{ "classname" "light" }\n{ "classname" "info_null" }';
out.push(`filter ${JSON.stringify(filterEntityText(entities, new Set([2, 9])))}`);
out.push(`filter_quoted ${JSON.stringify(filterEntityText(quoted, new Set([2])))}`);

// planGuestMonsters over the same text.
const selection = { kind: "replace", default: { source: { provider: "q2:monsters", content: "q2:classic:baseq2:id" }, classname: "monster_gunner" },
  byClassname: { monster_berserk: { kind: "map-defined" } } } as any;
for (const [edition, family, skill, mode, projection] of [["classic", "q2", 1, "singleplayer", true], ["rerelease", "q3", 0, "coop", true],
  ["classic", "q2", 1, "singleplayer", false], ["classic", "q1", 2, "deathmatch", true]] as const) {
  const plan = planGuestMonsters(entities, edition, family, selection, skill, mode, projection);
  out.push(`plan ${edition} ${family} ${skill} ${mode} ${projection} replaced=${plan.replacements.map(r => `${r.ordinal}:${r.classname}`).join(",")} kept=${plan.kept.map(k => `${k.classname}|${k.reason}`).join(";")} text=${JSON.stringify(plan.entities)}`);
}

// tagSpawnOrigins / farthestDeathmatchSpawn (no players) / lmctf flag homes / deathball origins.
// The TS tree later moved these members to foreign-match-rules.ts; the
// C port and its expected values follow runtime.ts as read for the port.
if (typeof P.tagSpawnOrigins === "function") {
const stub = (match: string, mapProvider: string, sourceKind: string, text: string) => {
  const self: any = { tagSpawns: null, lmctfSpots: null, deathballSpots: null, options: { world: { entities: text }, mode: "deathmatch" },
    recipe: { match: { provider: match }, map: { entities: { provider: mapProvider } } }, source: { kind: sourceKind },
    players: () => [], bodies: { read: () => null } };
  for (const name of ["tagSpawnOrigins", "farthestDeathmatchSpawn", "lmctfOrigins", "lmctfFlagHome", "deathballOrigins"]) self[name] = P[name].bind(self);
  for (const name of ["foreignLmctf", "foreignCtf", "flagMode", "foreignTag", "foreignDeathball"])
    Object.defineProperty(self, name, { get: Object.getOwnPropertyDescriptor(P, name)!.get!.bind(self) });
  return self;
};
const s = stub("q2:lmctf", "q1:id1", "q1", entities);
out.push(`tag_spawns ${s.tagSpawnOrigins().map(vec).join(",")}`);
out.push(`farthest ${vec(s.farthestDeathmatchSpawn())}`);
out.push(`modes ${s.foreignTag} ${s.foreignLmctf} ${s.foreignCtf} ${s.flagMode} ${s.foreignDeathball}`);
out.push(`lmctf_home ${vec(s.lmctfFlagHome(1))} ${vec(s.lmctfFlagHome(2))} health=${s.lmctfOrigins().health.map(vec).join(",")}`);
const c = stub("q1:ctf", "q2:base", "q3", entities);
out.push(`ctf_modes ${c.foreignLmctf} ${c.foreignCtf} ${c.flagMode} home=${vec(c.lmctfFlagHome(1))} ${vec(c.lmctfFlagHome(2))}`);
const d = stub("q2:deathball", "q1:id1", "q1", entities);
const ball = d.deathballOrigins();
out.push(`deathball ${vec(ball.ball)} goals=${[...ball.goals].map(([t, v]: any) => `${t}:${vec(v)}`).join(",")}`);
const noGoals = entities.split("\n").filter(line => !line.includes("dm_dball")).join("\n");
const e = stub("q2:deathball", "q1:id1", "q1", noGoals);
const synthesized = e.deathballOrigins();
out.push(`deathball_synth ${vec(synthesized.ball)} goals=${[...synthesized.goals].map(([t, v]: any) => `${t}:${vec(v)}`).join(",")}`);
const fallback = stub("q2:tag", "q1:id1", "q1", '{ "classname" "worldspawn" }\n{ "classname" "info_player_start" "origin" "8 9 10" }\n{ "classname" "info_player_deathmatch" "origin" "1 2" }');
out.push(`tag_fallback ${fallback.tagSpawnOrigins().map(vec).join(",")} farthest=${vec(fallback.farthestDeathmatchSpawn())} modes=${fallback.foreignTag}`);

}
// foreignQ2Identity / foreignQ1Identity / foreignAdjunctIdentity / guestMapEdition.
const identity = (world: string, mapProvider: string, content: string, guests: any = {}) => {
  const self: any = { options: { world: { kind: world }, ...guests }, recipe: { map: { entities: { provider: mapProvider, content: mapProvider },
    geometry: { provenance: { mount: { identity: { content } } } } } } };
  Object.defineProperty(self, "tsPrimary", { get: Object.getOwnPropertyDescriptor(P, "tsPrimary")!.get!.bind(self) });
  for (const name of ["foreignQ2Identity", "foreignQ1Identity", "foreignAdjunctIdentity"]) self[name] = P[name].bind(self);
  const q2 = self.foreignQ2Identity(), q1 = self.foreignQ1Identity(), adjunct = self.foreignAdjunctIdentity();
  return `${q2 === null ? "null" : `${q2.reference.provider}|${q2.edition}|${q2.program}`} ${q1 === null ? "null" : `${q1.reference.provider}|${q1.edition}|${q1.program}`} ${adjunct === null ? "null" : `${adjunct.family}|${adjunct.reference.provider}`}`;
};
out.push(`id1 ${identity("q2-bsp", "q1:base", "q2:rerelease:xatrix:pak0")}`);
out.push(`id2 ${identity("q2-bsp", "q3:base", "q2:classic:n64")}`);
out.push(`id3 ${identity("q2-bsp", "q2:base", "q2:classic:baseq2")}`);
out.push(`id4 ${identity("q1-bsp", "q2:base", "q1:rerelease:mg1:x")}`);
out.push(`id5 ${identity("q1-bsp", "q3:base", "q1")}`);
out.push(`id6 ${identity("q1-bsp", "q2:base", "q1:classic:hipnotic", { q3Guest: {} })}`);
const edition = (content: string) => P.guestMapEdition.call({ recipe: { map: { entities: { content } } } });
out.push(`edition ${edition("q2:rerelease:baseq2:x")} ${edition("q2:classic:rerelease")}`);

// nativeWaterContents over movement profiles.
const water = (profile: string, stateKind: string, stateWater: number, waterType: number) =>
  P.nativeWaterContents.call({}, { profile: { kind: profile }, state: { kind: stateKind, waterType: stateWater }, waterType });
out.push(`water ${water("q1-netquake", "q1-netquake", -4, 0)} ${water("q1-quakeworld", "qw", 0, -5)} ${water("q3", "q3", 0, 24)} ${water("q3", "q3", 0, 16)} ${water("q2-classic", "q2-classic", 0, 1 | 64 | 32)} ${water("q1-netquake", "q1-netquake", -2, 0)}`);
// selectionConsumption on a native guest (no enemy plan) with foreign movement and character.
const consumption = (movementClock: string, movementContent: string, mirrored: number) => {
  const self: any = { source: { kind: "q2-native", edition: "classic" }, guestMonstersAdmitted: false, nativeMirroredCombat: { size: mirrored },
    recipe: { enemies: { kind: "map-defined" }, movement: { provider: "mv:provider", content: movementContent },
      map: { entities: { provider: "q2:base", content: "q2:classic:baseq2:id" } }, character: { definition: { provider: "q3:characters" } },
      timing: [{ provider: "mv:provider", clock: { kind: movementClock } }] } };
  self.foreignNativeMovement = P.foreignNativeMovement.bind(self); self.foreignNativeCharacter = P.foreignNativeCharacter.bind(self);
  return P.selectionConsumption.call(self).map((e: any) => `${e.axis}|${e.requested}|${e.outcome}|${e.detail}`).join(";");
};
out.push(`consumption1 ${consumption("q1-netquake", "q1:x", 0)}`);
out.push(`consumption2 ${consumption("q2-classic", "q2:other", 0)}`);
out.push(`consumption3 ${consumption("q3", "q3:x", 2)}`);
console.log(out.join("\n"));
