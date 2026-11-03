// Oracle for the checkpoint writers in tests/app/test_shared_simulation_save.c:
// builds each record SharedSimulation.checkpointFromSource encodes the way the
// TS owner builds it (real constructors where they need no world, otherwise
// the capture literal copied from the owner) and writes
// tests/app/oracles/checkpoint_writers.txt, one `name<TAB>encodeCheckpointValue`
// line per record. Run from the C repository root:
//   bun tests/app/oracles/checkpoint_writers.ts
import { encodeCheckpointValue } from "../../../../quake-anthology-ts/src/persistence/value.ts";
import { q3RequestWeapon, q3RequestWeaponHolster, q3SpawnArsenalRuntime, q3SpawnLoadout } from "../../../../quake-anthology-ts/src/content/q3/foundation/arsenal.ts";
import { saveQ2Attack } from "../../../../quake-anthology-ts/src/content/q2/foundation/checkpoint.ts";
import { createGrappleWeaponState } from "../../../../quake-anthology-ts/src/content/q2/equipment/grapple-weapon.ts";
import { captureCtfGrapple, captureLmctfGrapple } from "../../../../quake-anthology-ts/src/content/q2/equipment/grapple-services.ts";
import { SourceRandom } from "../../../../quake-anthology-ts/src/app/bootstrap/simulation/random.ts";
import { SourcePickupCargo } from "../../../../quake-anthology-ts/src/app/bootstrap/simulation/dropped-pickups.ts";
import { newQ1Travel } from "../../../../quake-anthology-ts/src/content/q1/base/travel.ts";
import { SaveReader } from "../../../../quake-anthology-ts/src/persistence/value.ts";
import { readGrappleRuntimeCheckpoint } from "../../../../quake-anthology-ts/src/app/bootstrap/simulation/grapple-checkpoint.ts";
import { readQ3SelectedArsenalCheckpoint } from "../../../../quake-anthology-ts/src/app/bootstrap/simulation/arsenal/q3.ts";

const lines: string[] = [];
const put = (name: string, value: unknown): void => { lines.push(`${name}\t${new TextDecoder().decode(encodeCheckpointValue(value))}`); };
const actor = (slot: number, generation: number) => ({ slot, generation });

// world:q3-runtime arsenals[].state: spawn, request, holster.
put("q3-arsenal", q3RequestWeaponHolster(q3RequestWeapon(q3SpawnArsenalRuntime("missionpack", 125, 7), 3)));
// world:q3-runtime movement[].state: ClientThink, spectator policy and restored literals.
put("movement-think", { debugLevel: 1, traceMask: 0x02010001, fixedMsec: 8, gauntletHit: true, noFootsteps: false, ...({ ladders: true }) });
put("movement-spectator", { debugLevel: 0, traceMask: 0x10001, fixedMsec: null, noFootsteps: false, gauntletHit: false });
put("movement-saved", { traceMask: 0x02010001, fixedMsec: null, noFootsteps: true, gauntletHit: false, debugLevel: 2 });
// world:q3-runtime lastAttacks[].attack: saveQ2Attack of a Q3 combat-bridge provenance.
put("q2-attack", saveQ2Attack({ sequence: 9, time: { kind: "milliseconds", value: 1500 }, attacker: { slot: 1, generation: 2 } as never, inflictor: { slot: 5, generation: 1 } as never,
  originatingProjectile: { slot: 6, generation: 3 } as never, weapon: "q3:weapon/rocketlauncher", weaponProvider: "q3:weapons", combatProvider: "q3:combat",
  inventoryProvider: "q3:inventory", movementProvider: "q3:movement", damagePowerupOwner: "q3:weapons", cause: { kind: "q3", meansOfDeath: 7, damageFlags: 1 } }));
// An empty Q2EntityServices.capture() (Q2FoundationCheckpoint literal order).
const q2Entities = { version: 1, nextSourceSlot: 1, sequence: 0, counters: { totalSecrets: 0, foundSecrets: 0, totalGoals: 0, foundGoals: 0, totalMonsters: 0, killedMonsters: 0, serverFlags: 0 },
  freedSlots: [], entities: [] };
const random = new SourceRandom(1234); random.nextInteger(); random.nextInteger();
// HandGrenadeRuntime.capture().
put("hand-grenades", { version: 1, controller: { version: 1, edition: "classic", actors: [{ actor: actor(1, 2), state: structuredClone({
  config: { enabled: true, initialAmmo: 5, capacity: 50, infiniteAmmo: false }, action: { kind: "cooking", expiresAt: 12.5 } }) }] },
  controls: [{ actor: actor(1, 2), ...{ held: true, pressed: false, released: true } }], source: { entities: q2Entities, random: random.checkpoint() } });
// GrappleRuntime.capture() on q2-ctf and q2-lmctf sources.
const common = { version: 2, weaponAnimations: [{ actor: actor(1, 2), ...{ state: createGrappleWeaponState(), nextFrameAt: 0.25, kickOrigin: { x: 0, y: 0, z: 0 }, kickPitch: -2 } }],
  controls: [{ actor: actor(1, 2), ...{ teleportBit: null, jump: true, held: false, pressed: false, released: true, previousVelocity: { x: 1, y: 2, z: 3 }, predictionSuppressed: false } }],
  random: random.checkpoint() };
const grappleCtf = { ...common, source: { kind: "q2-ctf", entities: q2Entities,
  states: [{ actor: actor(1, 2), state: captureCtfGrapple({ grapple: { slot: 9, generation: 4 } as never, grappleState: "pull", grappleReleaseTime: 3.5, grappleNoKnockback: null }) }] } };
const grappleLmctf = { ...common, source: { kind: "q2-lmctf", entities: q2Entities,
  states: [{ actor: actor(1, 2), state: captureLmctfGrapple({ hook: null, hookState: 2, hookLength: 120, hookHeld: true }) }] } };
put("grapple-ctf", grappleCtf);
put("grapple-lmctf", grappleLmctf);
// GrappleRuntime.capture() after restore(readGrappleRuntimeCheckpoint(...)): the weapon animations and controls keep the
// records the reader built ({ actor, ...entry } of each), the random stream and the source are captured afresh.
const restoredGrapple = (live: typeof grappleCtf) => {
  const read = readGrappleRuntimeCheckpoint(new SaveReader(live));
  return { version: 2, weaponAnimations: read.weaponAnimations.map(({ actor: saved, ...animation }) => ({ actor: saved, ...animation })),
    controls: read.controls.map(({ actor: saved, ...state }) => ({ actor: saved, ...state })), random: live.random, source: live.source };
};
put("grapple-ctf-restored", restoredGrapple(grappleCtf));
put("grapple-lmctf-restored", restoredGrapple(grappleLmctf));
// WeaponSlot.snapshot() literals.
put("slot-active", { kind: "active", provider: "q2:weapons" });
put("slot-activating", { kind: "activating", from: "q2:weapons", next: { provider: "q2:grapple", item: "q2:weapon_grapple" }, request: 3 });
// SourcePickupCargo.capture() over a registry whose actors are live and resolve by slot.
const registry = { onRelease: () => undefined, isLive: () => true, sourceOf: (id: { slot: number }) => ({ provider: "q2:game", slot: id.slot }),
  atSource: (_provider: string, slot: number) => ({ id: { slot, generation: 1 } }), resolveSaved: (saved: { slot: number; generation: number }) => ({ id: saved }) };
const cargo = new SourcePickupCargo(registry as never, "q2:game");
cargo.set({ slot: 11, generation: 1 } as never, { item: "q2:ammo_shells", count: 10 });
cargo.revisit(new Map([["base1", [{ slot: 12, item: "q2:ammo_cells", count: 50 }]]]), "base2");
cargo.set({ slot: 13, generation: 1 } as never, { item: "q2:item_quad", count: 1 });
put("dropped-pickups", cargo.capture());
const restored = new SourcePickupCargo(registry as never, "q2:game");
restored.restore(new SaveReader(cargo.capture()));
put("dropped-pickups-restored", restored.capture());
// Q3SelectedArsenal.capture(actor): read() spreads q3SpawnLoadout and replaces ammo with inventory entries.
const loadout = q3SpawnLoadout("q3:weapons", "baseq3", false);
const selectedArsenal = { supplyProfile: null, arsenal: { ...loadout, ammo: [{ item: "q3:weapon/machinegun", count: 1, capacity: 1 }, { item: "q3:ammo/bullets", count: 100, capacity: 200 }] },
  runtime: { ...q3SpawnArsenalRuntime("baseq3", 100) }, torsoAnimation: 11, lastFireMilliseconds: null, pendingUse: "q3:holdable/medkit" };
put("q3-selected-arsenal", selectedArsenal);
// Q3SelectedArsenal.capture(actor) after restore(actor, readQ3SelectedArsenalCheckpoint(...)) with no equipment owner:
// read() spreads the restored arsenal record and replaces its ammo with the (same) inventory entries.
const readArsenal = readQ3SelectedArsenalCheckpoint(new SaveReader(selectedArsenal));
put("q3-selected-arsenal-restored", { supplyProfile: readArsenal.supplyProfile, arsenal: { ...readArsenal.arsenal, ammo: readArsenal.arsenal.ammo },
  runtime: { ...readArsenal.runtime }, torsoAnimation: readArsenal.torsoAnimation, lastFireMilliseconds: readArsenal.lastFireMilliseconds,
  pendingUse: readArsenal.pendingUse ?? null });
// Q3CharacterActor.capture().
put("q3-character", { version: 1, product: "baseq3", animation: { ...{ kind: "q3", legs: 22, torso: 11, legsTimerMilliseconds: 0, torsoTimerMilliseconds: 150 } }, flags: 128,
  eventSequence: 4, respawnTime: 3000, spawnCount: 2, dead: false, gibbed: false, initialized: true });
// Q1TravelState.
put("q1-travel", newQ1Travel({ edition: "classic", skill: 1, deathmatch: 0 }));
// The Q2 "model" event of Q2EntityServices, spread with its saved actor.
const model = { kind: "model", actor: { slot: 4, generation: 1 }, path: "models/monsters/soldier/tris.md2", attachedModels: ["", "models/weapons/g_shotg/tris.md2", ""],
  frame: 12, oldFrame: 11, scale: 1, alpha: 0.5, skin: 2, effects: 0, renderFlags: 4 };
put("q2-model", { ...model, actor: { slot: model.actor.slot, generation: model.actor.generation } });
// The sourceModels record SharedSimulation's restore rebuilds (runtime.ts restore), captured again.
const restoredModel = { kind: "model", actor: model.actor, path: model.path, attachedModels: [...model.attachedModels], frame: model.frame, oldFrame: model.oldFrame,
  scale: model.scale, skin: model.skin, effects: model.effects, renderFlags: model.renderFlags, alpha: model.alpha };
put("q2-model-restored", { ...restoredModel, actor: { slot: restoredModel.actor.slot, generation: restoredModel.actor.generation } });
// q2BuildView, then Q2Players.buildView's helpIcon.
const view = { angles: { x: 1, y: 2, z: 3 }, offset: { x: 0, y: 0, z: 22 }, kickAngles: { x: 0, y: 0, z: 0 }, gunAngles: { x: 0, y: 0, z: 0 }, gunOffset: { x: 0, y: 0, z: 0 },
  blend: { x: 1, y: 0.3, z: 0, w: 0.6 }, fov: 90, underwater: true, flashes: 1, health: 87, armor: 25, ammo: 40, score: 3, selectedItem: "q2:item_quad",
  timer: { item: "q2:item_quad", seconds: 12 }, spectator: false, layouts: 2 };
put("q2-view", view);
put("q2-view-help", { ...view, helpIcon: true });
// SimulationWeaponBehaviors.checkpoint() with no selected components.
put("weapon-behaviors-empty", { version: 1, sources: [], attachments: { version: 1, attachments: [] } });
await Bun.write("tests/app/oracles/checkpoint_writers.txt", `${lines.join("\n")}\n`);
console.log(`checkpoint_writers ${lines.length} records`);
