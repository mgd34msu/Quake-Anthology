// Oracle for tests/app/test_shared_simulation_b2.c: runs the SharedSimulation
// (runtime.ts) B2 methods on fake `this` records with the same inputs as the
// C checks and prints the results. Run from the C repository root:
//   bun tests/app/oracles/shared_simulation_b2.ts
import { SharedSimulation } from "../../../../quake-anthology-ts/src/app/bootstrap/simulation/runtime.ts";
import { Q3_Q1_SUPPLY_PROFILE } from "../../../../quake-anthology-ts/src/content/composition/q3-q1-supply.ts";
import { Q1_Q2_SUPPLY_PROFILE } from "../../../../quake-anthology-ts/src/content/composition/q1-q2-supply.ts";
import { Q2_Q3_SUPPLY_PROFILE } from "../../../../quake-anthology-ts/src/content/composition/q2-q3-supply.ts";

const proto = SharedSimulation.prototype as unknown as Record<string, (...args: unknown[]) => unknown>;
const call = (name: string, self: object, ...args: unknown[]): unknown => proto[name]!.call(self, ...args);
const n = (value: number): string => Object.is(value, -0) ? "-0" : String(value);
const actor = (slot: number) => ({ slot, generation: 1, equals(other: { slot: number; generation: number }) { return other.slot === slot && other.generation === 1; } });

// presentationFraction
const fractionCases: [number | null, number, number, number, boolean][] = [
  [null, 1, 1000, 1000, true], [0, 1, 1000, 1000, true], [25, 0, 1000, 990, true],
  [25, 1, 1000, 990, true], [25, 1, 1000, 990, false], [25, 1, 1000, 1030, true], [25, 1, 1000, 960, true],
  [100, 1, 1.5, 1450, true], [100, 1, 1.5, 1525, false], [100, 1, 1.5, Number.NaN, true],
];
for (const [tick, size, seconds, scheduling, ahead] of fractionCases) {
  const self = { presentationTickMilliseconds: tick, presentationSnapshot: new Map(Array.from({ length: size }, (_, i) => [i, {}])),
    timeSeconds: seconds, sourceSchedulingMilliseconds: scheduling, presentationAhead: ahead };
  const result = call("presentationFraction", self) as { fraction: number; backLerp: number };
  console.log(`fraction ${n(result.fraction)} ${n(result.backLerp)}`);
}
// interpolatedPose
const a1 = actor(1), a2 = actor(2);
const snapshot = new Map<unknown, unknown>([[a1, { origin: { x: 10, y: -20, z: 30 }, angles: { x: 350, y: -170, z: 0 }, viewPath: null, viewFrame: 0 }]]);
const poseLookup = { get: (key: { slot: number }) => key.slot === 1 ? snapshot.get(a1) : undefined };
const poseCases: [number, { x: number; y: number; z: number }, { x: number; y: number; z: number }, number][] = [
  [1, { x: 20, y: -10, z: 50 }, { x: 10, y: 170, z: 90 }, 0.25],
  [1, { x: 20, y: -10, z: 50 }, { x: 10, y: 170, z: 90 }, 1],
  [2, { x: 20, y: -10, z: 50 }, { x: 10, y: 170, z: 90 }, 0.5],
  [1, { x: 600, y: -20, z: 30 }, { x: 0, y: 0, z: 0 }, 0.5],
  [1, { x: 13, y: -24, z: 30.5 }, { x: -720.5, y: 540, z: -181 }, 0.75],
  [1, { x: 10, y: -20, z: 30 }, { x: 170, y: 10, z: 359 }, 0],
];
for (const [slot, origin, angles, fraction] of poseCases) {
  const result = call("interpolatedPose", { presentationSnapshot: poseLookup }, slot === 1 ? a1 : a2, origin, angles, fraction) as
    { origin: { x: number; y: number; z: number }; angles: { x: number; y: number; z: number }; previousOrigin: { x: number; y: number; z: number }; backLerp: number };
  console.log(`pose ${[result.origin.x, result.origin.y, result.origin.z, result.angles.x, result.angles.y, result.angles.z,
    result.previousOrigin.x, result.previousOrigin.y, result.previousOrigin.z, result.backLerp].map(n).join(" ")}`);
}
// q1VisualFields
for (const [alpha, scale] of [[0, 0], [0.3, 1.5], [2, -1], [-0.5, 0.1], [Number.NaN, Number.NaN], [0.1, 3.3]] as const) {
  const result = call("q1VisualFields", {}, alpha, scale) as { alpha: number; scale: number };
  console.log(`visual ${n(result.alpha)} ${n(result.scale)}`);
}
// replacedSupplyItems is a module-private function; it is reproduced through
// createSelectedQ1Arsenal's replacedItems. The definition (274-277) is run
// here on the same profiles verbatim.
function replacedSupplyItems(profile: { ammo: readonly { source: string; destinations: readonly string[] }[]; weapons: readonly { source: string; destinations: readonly string[] }[] }): readonly string[] {
  const mappings = [...profile.ammo, ...profile.weapons], retained = new Set(mappings.flatMap(mapping => mapping.destinations));
  return mappings.map(mapping => mapping.source).filter(source => !retained.has(source));
}
for (const profile of [Q3_Q1_SUPPLY_PROFILE, Q1_Q2_SUPPLY_PROFILE, Q2_Q3_SUPPLY_PROFILE])
  console.log(`replaced ${profile.id} ${replacedSupplyItems(profile).join(",")}`);
// weaponPresentationClock
for (const [family, milliseconds] of [["q3", 1234.7], ["q3", -5.5], ["q1", 99], [null, 1]] as const) {
  const self = { selectedArsenal: family === null ? null : { family }, weaponProvider: { content: "q3:baseq3", provider: "q3:baseq3" }, selectedMilliseconds: milliseconds };
  const result = call("weaponPresentationClock", self) as { content: string; timeMilliseconds: number } | null;
  console.log(`clock ${result === null ? "null" : `${result.content} ${n(result.timeMilliseconds)}`}`);
}
// ---- runtime-b2b rows: error and fallback branches ----
const thrown = (run: () => unknown): string => { try { run(); return "returned"; } catch (error) { return (error as Error).message; } };
const rejected = async (run: () => Promise<unknown>): Promise<string> => { try { await run(); return "resolved"; } catch (error) { return (error as Error).message; } };
console.log(`spawn ${thrown(() => call("selectedQ3Spawn", { source: { kind: "loading" } }, actor(1)))}`);
console.log(`objectives ${thrown(() => call("dropSelectedQ3Objectives", { source: { kind: "loading" } }, actor(1)))}`);
console.log(`native-player ${thrown(() => call("selectedNativePlayer", { source: { kind: "quakec" } }))}`);
console.log(`protection ${thrown(() => call("nativeDropProtection", { source: { kind: "q1" } }, actor(1), "debit", () => 7))}`);
console.log(`protection-classic ${String(call("nativeDropProtection", { source: { kind: "q2-native", edition: "classic" } }, actor(1), "consume", () => 42))}`);
console.log(`rows ${String(call("nativeInventoryRows", { source: { kind: "q1" }, nativePrimaryCommands: null, selectedArsenal: null,
  inventory: { itemDefinitions: () => [] } }, actor(1)))}`);
console.log(`projection ${thrown(() => call("nativeDropProjection", { selectedArsenal: null, selectedOriginalSupply: null, selectedOriginalWeapons: null,
  options: {}, nativePrimary: proto.nativePrimary }, actor(1), "q2:weapon_shotgun"))}`);
console.log(`grenades ${String(call("createHandGrenades", { recipe: { equipment: { handGrenades: { kind: "disabled" } } } }))}`);
console.log(`grapple ${String(call("createGrapple", { recipe: { equipment: { grapple: { kind: "disabled" } } } }))}`);
console.log(`grapple-qvm ${String(call("createGrapple", { recipe: { equipment: { grapple: { kind: "enabled", mechanic: "q3-qvm" } } } }))}`);
console.log(`qvm-grapple-disabled ${await rejected(() => call("initializeQvmGrapple", { recipe: { equipment: { grapple: { kind: "disabled" } } } }) as Promise<unknown>)}`);
console.log(`qvm-grapple ${await rejected(() => call("initializeQvmGrapple", { recipe: { equipment: { grapple: { kind: "enabled", mechanic: "q3-qvm" } } }, options: {} }) as Promise<unknown>)}`);
console.log(`qvm-behavior ${await rejected(() => call("createQvmWeaponBehavior", { options: {} }, { kind: "qvm" }) as Promise<unknown>)}`);
console.log(`native-behavior ${await rejected(() => call("createNativeWeaponBehavior", { options: {} }, { kind: "rerelease-native" }, async () => undefined) as Promise<unknown>)}`);
// Q3 source pickups: no selected arsenal, a non-arsenal item, an unknown weapon, a released recipient.
const pickup = (type: number, tag: number) => ({ item: { type, tag, quantity: 10 }, playerActor: actor(1), count: 0, dropped: false, gameType: 0 });
const q3Self = (arsenal: object | null) => ({ selectedArsenal: arsenal, selectedSupply: arsenal === null ? null : {}, selectedQ3Source: null,
  actors: { isLive: () => false }, selectedQ3Supply: proto.selectedQ3Supply });
for (const [label, arsenal, item] of [["none", null, pickup(1, 2)], ["health", { family: "q1" }, pickup(4, 0)], ["unknown", { family: "q1" }, pickup(1, 0)],
  ["released", { family: "q2" }, pickup(2, 2)], ["q3-family", { family: "q3" }, pickup(1, 2)]] as const) {
  const preview = call("previewSelectedQ3Pickup", q3Self(arsenal), item) as { kind: string };
  const admit = call("admitSelectedQ3Pickup", q3Self(arsenal), item) as { kind: string };
  console.log(`pickup ${label} ${preview.kind} ${admit.kind}`);
}
