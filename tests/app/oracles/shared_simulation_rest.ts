// Oracle for tests/app/test_shared_simulation_rest.c: runs the TS modules the
// runtime-rest rows depend on with the same inputs as the C checks and prints
// the results. Run from the C repository root:
//   bun tests/app/oracles/shared_simulation_rest.ts
import { createIdentityOwner } from "../../../../quake-anthology-ts/src/contracts/identity.ts";
import { overlapPlane, pmoveTouchList, reportedGroundPlane } from "../../../../quake-anthology-ts/src/app/bootstrap/simulation/native-pmove-contacts.ts";
import { nativeMoveSlices } from "../../../../quake-anthology-ts/src/app/bootstrap/simulation/native-commands.ts";
import { q2MissionWeaponInventory } from "../../../../quake-anthology-ts/src/content/q2/missionpacks/items.ts";
import { registerSelectedQ2MissionWeapons } from "../../../../quake-anthology-ts/src/content/composition/q2-expansion-arsenal.ts";
import { Q2Weapons } from "../../../../quake-anthology-ts/src/content/q2/foundation/weapons/player.ts";
import { q2RereleaseAmmoLimits } from "../../../../quake-anthology-ts/src/content/q2/foundation/rerelease-ammo.ts";
import type { ClockProfile } from "../../../../quake-anthology-ts/src/contracts/time.ts";

const n = (value: number): string => Object.is(value, -0) ? "-0" : String(value);
const plane = (p: { normal: { x: number; y: number; z: number }; distance: number } | null): string =>
  p === null ? "null" : `${n(p.normal.x)},${n(p.normal.y)},${n(p.normal.z)},${n(p.distance)}`;
const owner = createIdentityOwner("native-pmove-contacts");
const up = { normal: { x: 0, y: 0, z: 1 }, distance: 24 }, wall = { normal: { x: -1, y: 0, z: 0 }, distance: -100 };
const slope = { normal: { x: 0, y: 0.6, z: 0.8 }, distance: 3 }, steep = { normal: { x: 0, y: 0.8, z: 0.6 }, distance: 5 };
{
  const a = owner.actor(3, 0), b = owner.actor(5, 0), slots = new Map([[a, 3], [b, 5]]);
  const touches = pmoveTouchList([{ actor: b, plane: wall }, { actor: null, plane: up }, { actor: a, plane: up }, { actor: b, plane: up }], [], actor => slots.get(actor));
  console.log("touches1", touches.map(t => `${t.slot}:${plane(t.plane)}`).join(" "));
}
{
  const named = owner.actor(2, 0), door = owner.actor(7, 0), loose = owner.actor(9, 0), slots = new Map([[named, 2], [door, 7]]);
  const derived = { normal: { x: 1, y: 0, z: 0 }, distance: 4 };
  const touches = pmoveTouchList([{ actor: named, plane: wall }, { actor: door, plane: null }], [{ actor: named, plane: up }, { actor: door, plane: derived }, { actor: loose, plane: up }], actor => slots.get(actor));
  console.log("touches2", touches.map(t => `${t.slot}:${plane(t.plane)}`).join(" "));
}
console.log("ground", plane(reportedGroundPlane([{ actor: null, plane: up }, { actor: null, plane: wall }])), plane(reportedGroundPlane([{ actor: null, plane: wall }])),
  plane(reportedGroundPlane([])), plane(reportedGroundPlane([{ actor: null, plane: slope }, { actor: null, plane: steep }, { actor: null, plane: null }])));
const bounds = { min: { x: -16, y: -16, z: -24 }, max: { x: 16, y: 16, z: 32 } };
for (const [origin, other] of [[{ x: 85, y: 0, z: 30 }, { min: { x: 100, y: -64, z: 0 }, max: { x: 108, y: 64, z: 128 } }],
  [{ x: 0, y: 0, z: 24.5 }, { min: { x: -64, y: -64, z: -8 }, max: { x: 64, y: 64, z: 0 } }],
  [{ x: -20, y: 70, z: 0 }, { min: { x: -64, y: 80, z: -8 }, max: { x: 64, y: 96, z: 64 } }],
  [{ x: 0, y: 0, z: 0 }, { min: { x: -16, y: -16, z: -24 }, max: { x: 16, y: 16, z: 32 } }],
  [{ x: NaN, y: 0, z: 0 }, { min: { x: 0, y: 0, z: 0 }, max: { x: 1, y: 1, z: 1 } }]] as const)
  console.log("overlap", plane(overlapPlane(origin, bounds, other)));
const netquake: ClockProfile = { kind: "q1-netquake", minimumFrameSeconds: 0.001, maximumFrameSeconds: 0.1, fixedFrameSeconds: null };
const fixed: ClockProfile = { ...netquake, fixedFrameSeconds: 0.04 };
const quakeWorld: ClockProfile = { kind: "q1-quakeworld", maximumCommandMilliseconds: 50 };
const quake3: ClockProfile = { kind: "q3", serverFrameMilliseconds: 50, fixedMovementMilliseconds: null, maximumCommandMilliseconds: 200 };
const q2: ClockProfile = { kind: "q2-classic", frameMilliseconds: 100 } as ClockProfile;
for (const [name, clock, ms, carry] of [["nq", netquake, 100, 0], ["nq", netquake, 250, 0], ["q3", quake3, 100, 0], ["qw", quakeWorld, 100, 0],
  ["qw", quakeWorld, 120, 0], ["qw", quakeWorld, 0, 0], ["fixed", fixed, 100, 0], ["fixed", fixed, 100, 20], ["fixed", fixed, 33, 0], ["fixed", fixed, -50, 0],
  ["fixed", fixed, NaN, 0], ["q2", q2, 75, 0]] as const) {
  const quoted = nativeMoveSlices(clock, ms, carry);
  console.log("slices", name, n(ms), n(carry), quoted.slices.map(n).join(","), n(quoted.carry));
}
for (const pack of ["xatrix", "rogue"] as const) for (const edition of ["classic", "rerelease"] as const)
  console.log("mission", pack, edition, q2MissionWeaponInventory(pack, edition).map(e => `${e.item}=${n(e.capacity)}`).join(" "));
for (const name of ["ammo_trap", "ammo_tesla", "ammo_disruptor", "ammo_prox", "ammo_shells", "ammo_none"]) {
  const limits = q2RereleaseAmmoLimits[name];
  console.log("limits", name, limits === undefined ? "undefined" : `${limits.tag},${limits.capacity},${limits.bandolier},${limits.pack}`);
}
for (const edition of ["classic", "rerelease"] as const) for (const program of ["baseq2", "xatrix", "rogue", "mg2"] as const) {
  const weapons = new Q2Weapons({ emit: () => undefined, noise: () => undefined, dodge: () => undefined, lagCompensation: { kind: "current-world" } } as never);
  let registered = 0;
  const game = { options: { edition }, sourceCallbacks: { register: () => { registered++; } } } as never;
  const result = registerSelectedQ2MissionWeapons(game, weapons, program, { monster: () => null, playerEffect: () => undefined });
  console.log("register", edition, program, registered, result.pickupOrder.join(","), result.inventoryDefinitions.map(e => `${e.item}=${n(e.capacity)}`).join(" "),
    weapons.registeredDefinitions().map(d => d.name).join(","));
}
