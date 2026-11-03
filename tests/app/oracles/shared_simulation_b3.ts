// Oracle for tests/app/test_shared_simulation_b3.c: runs the SharedSimulation
// (runtime.ts) B3 methods on fake `this` records with the same inputs as the
// C checks and prints the results. Run from the C repository root:
//   bun tests/app/oracles/shared_simulation_b3.ts
import { SharedSimulation } from "../../../../quake-anthology-ts/src/app/bootstrap/simulation/runtime.ts";
import { EventRing } from "../../../../quake-anthology-ts/src/foundation/event-ring.ts";

const proto = SharedSimulation.prototype as unknown as Record<string, (...args: unknown[]) => unknown>;
const call = (name: string, self: object, ...args: unknown[]): unknown => proto[name]!.call(self, ...args);
const actor = (slot: number) => ({ slot, generation: 1, equals(other: { slot: number; generation: number }) { return other.slot === slot && other.generation === 1; } });

// toggleQ1Pause + sourcePlayerName over a Q1 composition client's userinfo (q2Userinfo parsing and Map join).
const pauseCases: [string | null, number | undefined, string][] = [
  [null, undefined, ""], [null, 0, ""], ["\\name\\Ranger\\topcolor\\4", 1, ""], ["\\name\\Ranger\\topcolor\\4", 0, ""],
  ["\\skin\\male/grunt\\fov\\90", undefined, ""], ["name\\Ranger\\name\\Second\\odd", 1, ""], ["\\name\\\\x\\y", 1, ""],
];
for (const [userinfo, pausable] of pauseCases) {
  const records = new Map<unknown, unknown>();
  const a = actor(3);
  const info = new Map<string, string>();
  if (userinfo !== null) {
    const tokens = (userinfo.startsWith("\\") ? userinfo.slice(1) : userinfo).split("\\");
    for (let index = 0; index + 1 < tokens.length; index += 2) if (!info.has(tokens[index]!)) info.set(tokens[index]!, tokens[index + 1]!);
  }
  records.set(a, { userinfo: info });
  const self = {
    q1PauseState: false,
    source: { kind: "q1", cvars: { find: (name: string) => name === "pausable" && pausable !== undefined ? { numericValue: pausable } : undefined },
      composition: { clients: { require: () => records.get(a) } } },
    sourcePlayerUserinfo(actorId: unknown) { return proto.sourcePlayerUserinfo!.call(this, actorId); },
  };
  const first = call("toggleQ1Pause", self, userinfo === null ? null : a) as string;
  const second = call("toggleQ1Pause", self, userinfo === null ? null : a) as string;
  const text = userinfo === null ? "" : call("sourcePlayerUserinfo", self, a) as string;
  console.log(`pause ${JSON.stringify(first)} ${JSON.stringify(second)} ${self.q1PauseState} ${JSON.stringify(text)}`);
}

// sharedCarryOf
const armor = { regular: { kind: "q2", count: 50, maxCount: 100, normalProtection: 0.3, energyProtection: 0, item: "q2:item_armor_jacket" }, powered: { kind: "none" } };
const inventory = [{ item: "q2:ammo_shells", count: 10, capacity: 100 }];
const carries: unknown[] = [undefined, { state: { kind: "quakec" } }, { state: { kind: "shared", health: 75, armor, inventory } },
  { state: { kind: "q1", carry: { health: 33, armor, inventory } } }, { state: { kind: "q2", carry: { health: 120, armor, inventory } } }];
for (const carried of carries) {
  const shared = call("sharedCarryOf", {}, carried) as { health: number; inventory: unknown[] } | null;
  console.log(shared === null ? "carry null" : `carry ${shared.health} ${shared.inventory.length}`);
}

// takeLevelChange
{
  const self = { levelChange: { map: "base2", serverFlags: 4, landmark: null } as unknown };
  const first = call("takeLevelChange", self) as { map: string; serverFlags: number } | null;
  const second = call("takeLevelChange", self);
  console.log(`level ${first?.map} ${first?.serverFlags} ${second === null}`);
}

// takeTransitions: campaign intents gain the match gates, others keep theirs.
{
  const transitions = new EventRing<unknown>(8);
  const cursor = transitions.openCursor();
  transitions.admit({ kind: "campaign-level", campaign: "q1:campaign", map: "q1:e1m2", spawnPoint: "", gates: [{ kind: "objective", objective: "a" }], cause: null });
  transitions.admit({ kind: "match-end", match: "q2:dm", winner: null, gates: [] });
  transitions.admit({ kind: "campaign-complete", campaign: "q1:campaign", gates: [] });
  const gates: unknown[] = []; // a match state with no objectives (the C fixture)
  const self = { sourceMatch: { gates: () => gates }, transitions, transitionsCursor: cursor };
  const taken = call("takeTransitions", self) as { kind: string; gates?: unknown[] }[];
  console.log(`transitions ${taken.map(intent => `${intent.kind}:${intent.gates?.length ?? 0}`).join(",")} ${transitions.pending(cursor)}`);
}

// notifyClientEvent: admissions dedupe, listener order, disconnect cleanup.
{
  const a = actor(5), client = { slot: 4, generation: 2 };
  const seen: string[] = [];
  const self = {
    actors: { isLive: () => true }, playerClient: () => client,
    modClientAdmissions: new Set<unknown>(), modClientCommands: new Map<unknown, unknown>([[a, {}]]),
    modClientListeners: new Set<(event: { kind: string }) => undefined>([
      event => { seen.push(`first:${event.kind}`); return undefined; }, event => { seen.push(`second:${event.kind}`); return undefined; }]),
    restoreNativeInventoryCursors: () => { seen.push("cursors"); return undefined; },
  };
  call("notifyClientEvent", self, "admitted", a);
  call("notifyClientEvent", self, "admitted", a);
  call("notifyClientEvent", self, "userinfo", a);
  call("notifyClientEvent", self, "disconnecting", a);
  console.log(`notify ${seen.join(",")} ${self.modClientAdmissions.size} ${self.modClientCommands.size}`);
}
