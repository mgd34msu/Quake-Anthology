// Oracle for tests/world/test_gate_world_render.c (gate-render read of src/world).
// Run from this directory: bun gate_world_render.ts
const TS = "../../../../quake-anthology-ts/src";
const { generatePatchCollide, positionInPatch } = await import(`${TS}/world/collision/q3/patch.ts`);
const { CollisionWindingLibrary } = await import(`${TS}/world/collision/q3/polylib.ts`);
const { boxCell } = await import(`${TS}/world/geometry/q1-solid/polyhedron.ts`);
const { sweepBoxCell } = await import(`${TS}/world/geometry/q1-solid/sweep.ts`);
const { compareInvocationOrder } = await import(`${TS}/world/scheduler.ts`);
const { inventoryGive } = await import(`${TS}/world/gameplay/inventory.ts`);
const { ModOperation } = await import(`${TS}/world/gameplay/mod-composition.ts`);
const { SharedTransitionCoordinator } = await import(`${TS}/world/gameplay/transitions.ts`);
const { SessionActorRegistry } = await import(`${TS}/world/actors/registry.ts`);
const { createIdentityOwner } = await import(`${TS}/contracts/identity.ts`);
const { ResourceScope } = await import(`${TS}/world/session/resources.ts`);

const fail = (f: () => unknown): string => {
  try {
    f();
    return "none";
  } catch (error) {
    return `${(error as Error).name}|${(error as Error).message}`;
  }
};
const v = (x: number, y: number, z: number) => ({ x, y, z });
const flat = [v(0, 0, 0), v(64, 0, 0), v(128, 0, 0), v(0, 64, 0), v(64, 64, 0), v(128, 64, 0), v(0, 128, 0), v(64, 128, 0), v(128, 128, 0)];

// 1. CM_GeneratePatchFacets: the drop message names the dimensions.
console.log(`patch_bad_params ${fail(() => generatePatchCollide(1, 3, flat.slice(0, 3)))}`);
// 2. Number.isInteger(Infinity) is false: RangeError before the size checks.
console.log(`patch_infinite ${fail(() => generatePatchCollide(Infinity, 3, flat))}`);
// 3. positionInPatch: a NaN start fails every border test.
{
  const patch = generatePatchCollide(3, 3, flat);
  const shape = { kind: "box", mins: v(-16, -16, -16), extents: v(16, 16, 16) } as const;
  console.log(`patch_position_nan ${positionInPatch(patch, v(NaN, 64, 0), shape)} ${positionInPatch(patch, v(64, 64, 0), shape)} facets ${patch.facets.length} planes ${patch.planes.length}`);
}
// 4. CheckWinding names the point count.
{
  const lib = new CollisionWindingLibrary();
  const w = lib.alloc(2);
  w.append(v(0, 0, 0));
  w.append(v(1, 0, 0));
  console.log(`check_winding ${fail(() => lib.check(w))}`);
}
// 5. sweepBoxCell: a NaN start yields no interval (Math.min keeps NaN).
{
  const cell = boxCell({ min: v(0, 0, 0), max: v(32, 32, 32) });
  const shape = { kind: "box", axes: [v(1, 0, 0), v(0, 1, 0), v(0, 0, 1)], extents: v(8, 8, 8) } as const;
  const hit = sweepBoxCell(cell, v(NaN, 16, 16), v(64, 16, 16), shape, 0.03125);
  const clean = sweepBoxCell(cell, v(-64, 16, 16), v(64, 16, 16), shape, 0.03125);
  console.log(`sweep_nan ${hit === null ? "null" : "hit"} clean ${clean?.enter} ${clean?.exit} ${clean?.contact}`);
}
// 6. compareInvocationOrder: indexOf takes the first provider match.
{
  const owner = createIdentityOwner("order");
  const a = owner.actor(1, 0), b = owner.actor(2, 0);
  const result = compareInvocationOrder({ kind: "mixed", providers: ["p:a", "p:b", "p:a"] } as never, { provider: "p:a", actor: a, sequence: 0 } as never, { provider: "p:b", actor: b, sequence: 0 } as never);
  console.log(`invocation_order ${result}`);
}
// 7. inventoryGive: a NaN capacity reaches sourceCount, which throws.
console.log(`give_nan_capacity ${fail(() => inventoryGive({ item: "x", count: 1, capacity: NaN } as never, 2))}`);
// 8. ModOperation: a canonical throw the replacement swallows is rethrown.
{
  const op = new ModOperation<number, number>("gate");
  op.register({ provider: "p", id: "m:r", order: 0, kind: "replace", replace: (request, next) => { try { return next(request); } catch { return -1; } } } as never);
  console.log(`mod_swallowed ${fail(() => op.dispatch(5, () => { throw new Error("canonical boom"); }))}`);
}
// 9. Transition commit: a throwing apply leaves the coordinator able to commit again.
{
  let calls = 0;
  const coordinator = new SharedTransitionCoordinator(() => { calls++; if (calls === 1) throw new Error("apply boom"); return undefined; });
  const mode = { kind: "campaign", campaign: "c", allowRoundRestart: false } as never;
  const intent = { kind: "campaign-complete", campaign: "c", gates: [] } as never;
  const first = coordinator.resolve(mode, [intent]), second = coordinator.resolve(mode, [intent]);
  console.log(`transition_first ${fail(() => coordinator.commit(first))} second ${fail(() => coordinator.commit(second))} calls ${calls}`);
}
// 10. Registry release: every callback runs; failures aggregate.
{
  const owner = createIdentityOwner("release");
  const registry = new SessionActorRegistry(owner);
  const actor = registry.allocate("p:a", "test:actor");
  let ran = 0;
  registry.onRelease(() => { ran++; throw new Error("first"); });
  registry.onRelease(() => { ran++; return undefined; });
  console.log(`release_aggregate ${fail(() => registry.release(actor))} ran ${ran} live ${registry.isLive(actor.id)}`);
}
// 11. ResourceScope.close: every cleanup runs; failures aggregate under the scope name.
{
  const scope = new ResourceScope("Gate scope");
  let ran = 0;
  scope.defer(() => { ran++; return undefined; });
  scope.defer(() => { ran++; throw new Error("cleanup boom"); });
  console.log(`scope_close ${fail(() => scope.close())} ran ${ran}`);
}
