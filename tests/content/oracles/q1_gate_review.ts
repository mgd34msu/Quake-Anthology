// Oracle for tests/content/test_q1_gate_review.c: the TS values behind the
// content/q1 gate-read fixes. Run from the C repository root:
//   bun tests/content/oracles/q1_gate_review.ts
// Prints `key value` lines; the C test hard-codes them.
import { createIdentityOwner } from "../../../../quake-anthology-ts/src/contracts/identity.ts";
import type { ActorId } from "../../../../quake-anthology-ts/src/contracts/identity.ts";
import { SessionActorRegistry, ActorCallbackTable } from "../../../../quake-anthology-ts/src/world/actors/index.ts";
import { SharedBodyTable, translatedBodyBounds } from "../../../../quake-anthology-ts/src/world/actors/body.ts";
import { SharedInventoryTable } from "../../../../quake-anthology-ts/src/world/gameplay/index.ts";
import { Q1Foundation } from "../../../../quake-anthology-ts/src/content/q1/foundation/runtime.ts";
import type { Q1Entity } from "../../../../quake-anthology-ts/src/formats/q1-map/index.ts";
import { inertAuthority } from "../../../../quake-anthology-ts/tests/support/authority.ts";
import { q1TestHost } from "../../../../quake-anthology-ts/tests/support/q1-host.ts";
import { registerQ3PickupHook } from "../../../../quake-anthology-ts/src/content/composition/q1/q3-pickups.ts";

function makeGame(foreignWorld: boolean) {
  const actors = new SessionActorRegistry(createIdentityOwner("q1-gate-review")),
    callbacks = new ActorCallbackTable(actors);
  const bodies = new SharedBodyTable(actors, { absoluteBounds: translatedBodyBounds, onLink: () => undefined, onUnlink: () => undefined });
  const combat = inertAuthority(actors, callbacks);
  const inventory = new SharedInventoryTable(actors);
  const players: ActorId[] = [];
  let runtime: Q1Foundation | null = null;
  const { host } = q1TestHost({ actors, callbacks, bodies, combat, inventory, entity: actor => runtime?.entity(actor), players: () => players });
  const game = new Q1Foundation(host, {
    edition: "rerelease",
    skill: 1,
    deathmatch: 0,
    coop: false,
    gravity: 800,
    maxClients: 4,
    campaign: "q1:id1",
    combatProvider: "q1:combat",
    inventoryProvider: "q1:inventory",
    movementProvider: "q2:movement",
    foreignWorld,
  });
  runtime = game;
  return game;
}
const entity = (pairs: Record<string, string>): Q1Entity => ({ properties: Object.entries(pairs).map(([key, value]) => ({ key, value })) }) as Q1Entity;
const world = { min: { x: -512, y: -512, z: -512 }, max: { x: 512, y: 512, z: 512 } };

// 1. restoreFoundation assigns game.time through the Math.fround setter.
{
  const source = makeGame(false);
  source.spawnEntities([entity({ classname: "worldspawn" })], [world], "gate");
  const saved = source.capture();
  const target = makeGame(false);
  target.restore({ ...saved, entities: [], world: null, time: 0.1 }, { scheduleThinks: false });
  console.log(`restored_time ${target.time}`);
}

// 2. Foreign worlds catch UnknownQ1SpawnError: a classname the composition's foreignPickup
//    (the Q3 pickup hook) accepts spawns as a pickup; only classes nothing spawns are reported.
{
  const game = makeGame(true);
  registerQ3PickupHook(game);
  const report = game.spawnEntities(
    [entity({ classname: "worldspawn" }), entity({ classname: "item_quad", origin: "0 0 0" }), entity({ classname: "info_gate_unknown", origin: "8 0 0" })],
    [world],
    "gate",
  );
  console.log(`foreign_count ${report.foreign.length}`);
  for (const note of report.foreign) console.log(`foreign ${note.ordinal} ${note.classname}`);
  const pickup = [...game.entities.values()].find(candidate => candidate.classname === "item_quad");
  console.log(`pickup_model ${pickup?.model ?? "missing"}`);
  console.log(`pickup_touch ${pickup?.touch !== null && pickup !== undefined ? "item_touch" : "none"}`);
}

// 3. linkDoors groups every overlapping door; there is no group size limit.
{
  const game = makeGame(false);
  const list = [entity({ classname: "worldspawn" })];
  for (let i = 0; i < 70; i++) list.push(entity({ classname: "func_door", model: "*1", targetname: i === 0 ? "gate_doors" : "" }));
  game.spawnEntities(list, [world, { min: { x: 0, y: 0, z: 0 }, max: { x: 64, y: 8, z: 64 } }], "gate");
  const doors = [...game.entities.values()].filter(candidate => candidate.classname === "func_door");
  console.log(`door_count ${doors.length}`);
  console.log(`master_group ${doors[0]?.doorGroup.length}`);
  console.log(`last_group ${doors[69]?.doorGroup.length}`);
}

// 4. adjoin and spawnEntity restore their scoped state in `finally` when the callee throws.
{
  const game = makeGame(false);
  game.registerSpawn("gate_owned", () => undefined);
  try {
    game.adjoin(() => {
      throw new Error("gate adjoin");
    });
  } catch (error) {
    console.log(`adjoin_error ${(error as Error).message}`);
  }
  try {
    game.registerSpawn("gate_owned", () => undefined);
    console.log("adjoin_duplicate accepted");
  } catch (error) {
    console.log(`adjoin_duplicate ${(error as Error).message}`);
  }
  game.registerSpawn("gate_throws", () => {
    throw new Error("gate spawn");
  });
  game.spawnEntities([entity({ classname: "worldspawn" })], [world], "gate");
  const thrower = game.create("gate_throws");
  try {
    game.spawnEntity(thrower, { deathmatch: 3 });
  } catch (error) {
    console.log(`spawn_error ${(error as Error).message}`);
  }
  console.log(`deathmatch_after ${game.options.deathmatch}`);
}

// 5. Error messages of the raise paths whose C allocations must unwind.
{
  const game = makeGame(false);
  try {
    game.spawnEntities([entity({ classname: "worldspawn" }), entity({ classname: "func_wall", model: "*5" })], [world], "gate");
  } catch (error) {
    console.log(`inline_error ${(error as Error).message}`);
  }
  const source = makeGame(false);
  source.spawnEntities([entity({ classname: "worldspawn" })], [world], "gate");
  const saved = source.capture();
  const target = makeGame(false);
  try {
    target.restore({ ...saved, entities: [], world: null, extensions: [{ id: "gate:nope", bytes: new Uint8Array() }] }, { scheduleThinks: false });
  } catch (error) {
    console.log(`extension_error ${(error as Error).message}`);
  }
}
