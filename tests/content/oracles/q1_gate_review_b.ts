// Oracle for tests/content/test_q1_gate_review_b.c: TS values behind the second
// content/q1 gate-read fixes (quakec, equipment, caps). Run from the C repository root:
//   bun tests/content/oracles/q1_gate_review_b.ts
// Prints `key value` lines; the C test hard-codes them.
import { createIdentityOwner } from "../../../../quake-anthology-ts/src/contracts/identity.ts";
import type { ActorId } from "../../../../quake-anthology-ts/src/contracts/identity.ts";
import { SessionActorRegistry, ActorCallbackTable } from "../../../../quake-anthology-ts/src/world/actors/index.ts";
import { SharedBodyTable, translatedBodyBounds } from "../../../../quake-anthology-ts/src/world/actors/body.ts";
import { SharedInventoryTable } from "../../../../quake-anthology-ts/src/world/gameplay/index.ts";
import { Q1Foundation } from "../../../../quake-anthology-ts/src/content/q1/foundation/runtime.ts";
import { q1EntityString } from "../../../../quake-anthology-ts/src/content/q1/foundation/text.ts";
import { backpackMessage } from "../../../../quake-anthology-ts/src/content/q1/base/projectiles.ts";
import { Q1SpawnSelector } from "../../../../quake-anthology-ts/src/content/q1/base/rules.ts";
import { Q1CampaignState } from "../../../../quake-anthology-ts/src/content/q1/base/provider.ts";
import type { Q1Entity } from "../../../../quake-anthology-ts/src/formats/q1-map/index.ts";
import { inertAuthority } from "../../../../quake-anthology-ts/tests/support/authority.ts";
import { q1TestHost } from "../../../../quake-anthology-ts/tests/support/q1-host.ts";

function makeGame(edition: "classic" | "rerelease", deathmatch: number) {
  const actors = new SessionActorRegistry(createIdentityOwner("q1-gate-review-b")),
    callbacks = new ActorCallbackTable(actors);
  const bodies = new SharedBodyTable(actors, { absoluteBounds: translatedBodyBounds, onLink: () => undefined, onUnlink: () => undefined });
  const combat = inertAuthority(actors, callbacks);
  const inventory = new SharedInventoryTable(actors);
  const players: ActorId[] = [];
  let runtime: Q1Foundation | null = null;
  const { host } = q1TestHost({ actors, callbacks, bodies, combat, inventory, entity: actor => runtime?.entity(actor), players: () => players });
  const game = new Q1Foundation(host, {
    edition,
    skill: 1,
    deathmatch,
    coop: false,
    gravity: 800,
    maxClients: 4,
    campaign: "q1:id1",
    combatProvider: "q1:combat",
    inventoryProvider: "q1:inventory",
    movementProvider: "q2:movement",
    foreignWorld: false,
  });
  runtime = game;
  return game;
}
const entity = (pairs: Record<string, string>): Q1Entity => ({ properties: Object.entries(pairs).map(([key, value]) => ({ key, value })) }) as Q1Entity;
const world = { min: { x: -512, y: -512, z: -512 }, max: { x: 512, y: 512, z: 512 } };
const codes = (text: string): string => [...text].map(char => char.charCodeAt(0)).join(",");

// 1. q1EntityString has no length limit, and works per UTF-16 unit of the
//    byte string BinaryReader.fixedByteString produces (one char per byte).
{
  const bytes = [0x41, 0x5c, 0xc3, 0xa9, 0x5c, 0x6e, 0x42, 0x5c, 0xe9, 0x43];
  console.log(`escape_bytes ${codes(q1EntityString(String.fromCharCode(...bytes)))}`);
  const long = "x".repeat(1500) + "\\n" + "y".repeat(1500);
  const game = makeGame("rerelease", 0);
  game.spawnEntities([entity({ classname: "worldspawn", message: long })], [world], "gate");
  const spawned = [...game.entities.values()].find(candidate => candidate.classname === "worldspawn");
  console.log(`long_message_length ${spawned?.message.length}`);
  console.log(`long_message_newline ${spawned?.message.indexOf("\n")}`);
}

// 2. backpackMessage lists every recognised extra (no limit of 8 entries).
{
  const kinds = ["rogue:ammo/lava-nails", "rogue:ammo/multi-rockets", "rogue:ammo/plasma"];
  const extra = Array.from({ length: 12 }, (_, index) => ({ item: kinds[index % 3] as string, count: index + 1 }));
  const contents = { weapon: "rocketlauncher", shells: 5, nails: 0, rockets: 3, cells: 0, extra } as Parameters<typeof backpackMessage>[0];
  const classic = backpackMessage(contents, "classic", true);
  console.log(`classic_text ${classic.text}`);
  const rerelease = backpackMessage(contents, "rerelease", true);
  console.log(`rerelease_parts ${rerelease.parts.length}`);
  const last = rerelease.parts[rerelease.parts.length - 1];
  console.log(`rerelease_last ${last?.text} ${last?.args?.[0]}`);
}

// 3. Classic deathmatch spawn selection walks every info_player_deathmatch (no limit of 1024).
{
  const game = makeGame("classic", 1);
  game.spawnEntities([entity({ classname: "worldspawn" })], [world], "gate");
  const points = Array.from({ length: 1100 }, () => game.create("info_player_deathmatch"));
  const selector = new Q1SpawnSelector(game, new Q1CampaignState());
  let picked = null as unknown;
  for (let i = 0; i < 1025; i++) picked = selector.select(false);
  console.log(`dm_pick_1025 ${points.indexOf(picked as (typeof points)[number])}`);
  for (let i = 0; i < 80; i++) picked = selector.select(false);
  console.log(`dm_pick_1105 ${points.indexOf(picked as (typeof points)[number])}`);
}

// 4. qcDamageScale's power test is Number.isInteger(Math.log2(constant)).
console.log(`log2_infinity_integer ${Number.isInteger(Math.log2(Infinity))}`);
console.log(`log2_quarter_integer ${Number.isInteger(Math.log2(0.25))}`);

// 5. qcDamageScale rejects a division by an infinite source constant, and
//    qcDamageCallLayout tracks every declared global (no limit of 64).
import { QcProgram, QcOpcode, type QcDefinition, type QcFunction, type QcStatement } from "../../../../quake-anthology-ts/src/compat/qc/program.ts";
import { qcDamageScale } from "../../../../quake-anthology-ts/src/content/q1/quakec/damage-scale.ts";
import { qcDamageCallLayout } from "../../../../quake-anthology-ts/src/content/q1/quakec/damage-call.ts";
import { id1ProgramBinding } from "../../../../quake-anthology-ts/src/content/q1/quakec/id1-program.ts";
const def = (type: QcDefinition["type"], offset: number, name: string): QcDefinition => ({ type, nativeType: 0, save: false, offset, name });
const netquake = { kind: "q1-netquake", programVersion: 6, systemCrc: 5927 } as const;
const inputs = ["self", "inflictor", "attacker", "amount"].map(name => ({ kind: "input", name }) as const);
const message = (run: () => unknown): string => {
  try {
    run();
    return "accepted";
  } catch (error) {
    return (error as Error).message;
  }
};
{
  const initial = new Uint8Array(4096);
  new DataView(initial.buffer).setFloat32(50 * 4, Infinity, true);
  new DataView(initial.buffer).setInt32(60 * 4, 1, true);
  const globals = [
    def("entity", 100, "targ"),
    def("entity", 101, "inflictor"),
    def("entity", 102, "attacker"),
    def("float", 103, "dmg"),
    def("float", 50, "scale_const"),
    def("float", 40, "time"),
    def("entity", 28, "self"),
    def("function", 60, "T_Damage"),
  ];
  const functions: QcFunction[] = [
    { index: 0, firstStatement: 0, parameterStart: 0, localWords: 0, name: "", file: "", parameterSizes: [], namedBuiltin: false },
    { index: 1, firstStatement: 1, parameterStart: 100, localWords: 10, name: "T_Damage", file: "combat.qc", parameterSizes: [1, 1, 1, 1], namedBuiltin: false },
  ];
  const statements: QcStatement[] = [
    { opcode: QcOpcode.Done, a: 0, b: 0, c: 0 },
    { opcode: QcOpcode.DivF, a: 103, b: 50, c: 103 },
    { opcode: QcOpcode.StoreF, a: 103, b: 103, c: 0 },
    { opcode: QcOpcode.Return, a: 0, b: 0, c: 0 },
  ];
  const program = new QcProgram("test.qc", netquake, statements, globals, [], functions, new Uint8Array(1), initial, 0, 0, "sha256:other" as never);
  const call = { function: "T_Damage", arguments: inputs, globals: [] };
  const source = { function: "T_Damage", entry: 1, exit: 2, damage: 103, statements: statements.slice(1, 3) };
  console.log(`scale_infinity ${message(() => qcDamageScale(program, call, source as never))}`);
}
{
  // 70 attacker globals, then an amount global aliasing the last attacker word.
  const globals = [def("entity", 100, "targ"), def("entity", 101, "inflictor"), def("entity", 102, "attacker"), def("float", 103, "dmg")];
  const declared: { name: string; value: { kind: "input"; name: string } }[] = [];
  for (let i = 0; i < 70; i++) {
    globals.push(def("entity", 200 + i, `attacker_alias${i}`));
    declared.push({ name: `attacker_alias${i}`, value: { kind: "input", name: "attacker" } });
  }
  globals.push(def("float", 269, "amount_alias"));
  declared.push({ name: "amount_alias", value: { kind: "input", name: "amount" } });
  const functions: QcFunction[] = [
    { index: 0, firstStatement: 0, parameterStart: 0, localWords: 0, name: "", file: "", parameterSizes: [], namedBuiltin: false },
    { index: 1, firstStatement: 1, parameterStart: 100, localWords: 10, name: "T_Damage", file: "combat.qc", parameterSizes: [1, 1, 1, 1], namedBuiltin: false },
  ];
  const statements: QcStatement[] = [
    { opcode: QcOpcode.Done, a: 0, b: 0, c: 0 },
    { opcode: QcOpcode.Return, a: 0, b: 0, c: 0 },
  ];
  const program = new QcProgram("test.qc", netquake, statements, globals, [], functions, new Uint8Array(1), new Uint8Array(4096), 0, 0, "sha256:other" as never);
  console.log(`layout_overlap ${message(() => qcDamageCallLayout(program, { function: "T_Damage", arguments: inputs, globals: declared } as never))}`);
}

// 6. derivedBindings is keyed by the QcProgram: a declared pinned call is seen by
//    every later lookup of that program, and a different declaration is refused.
{
  const NQ = "sha256:f2619787f9aa0f057246eea1665b622b4691b5c5a800b1a46133d1fe8b771580";
  const functions: QcFunction[] = Array.from({ length: 118 }, (_, index) => ({
    index,
    firstStatement: index === 117 ? 1421 : 0,
    parameterStart: index === 117 ? 1580 : 0,
    localWords: index === 117 ? 10 : 0,
    name: index === 117 ? "T_Damage" : `f${index}`,
    file: "",
    parameterSizes: index === 117 ? [1, 1, 1, 1] : [],
    namedBuiltin: false,
  }));
  const globals = [def("entity", 1580, "targ"), def("entity", 1581, "inflictor"), def("entity", 1582, "attacker"), def("float", 1583, "damage"), def("float", 300, "gate_time")];
  const program = new QcProgram("test.qc", netquake, [], globals, [], functions, new Uint8Array(1), new Uint8Array(8192), 0, 0, NQ as never);
  const declared = { function: "T_Damage", arguments: inputs, globals: [] };
  id1ProgramBinding(program, declared as never);
  console.log(`pinned_declared_seen ${id1ProgramBinding(program).damage.call.declaration !== undefined}`);
  const other = { function: "T_Damage", arguments: inputs, globals: [{ name: "gate_time", value: { kind: "input", name: "time" } }] };
  console.log(`pinned_other_declaration ${message(() => id1ProgramBinding(program, other as never))}`);
}
