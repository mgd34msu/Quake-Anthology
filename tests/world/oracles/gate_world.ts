// Oracle for tests/world/test_gate_world.c. Run: bun tests/world/oracles/gate_world.ts
const TS = "../../../../quake-anthology-ts/src";
const { adaptTraceResult } = await import(`${TS}/world/collision/contents.ts`);
const { traceBrushMedia } = await import(`${TS}/world/collision/media.ts`);
const { traceActorBody } = await import(`${TS}/world/collision/body.ts`);
const { createBoxModel } = await import(`${TS}/world/collision/q3/model.ts`);
const { createNumericOperations, Q1_DONOR_PROFILE, Q2_DONOR_PROFILE } = await import(`${TS}/core/numeric.ts`);
const { createIdentityOwner } = await import(`${TS}/contracts/identity.ts`);

const zero = { x: 0, y: 0, z: 0 };
const q3Trace = (surfaceFlags: number) => ({
  kind: "q3", fraction: 0.5, end: zero, startSolid: false, allSolid: false, contact: { kind: "none" }, hit: { kind: "world", model: 0 },
  contents: 1, surfaceFlags, sourcePlane: { normal: { x: 0, y: 0, z: 1 }, distance: 0, type: 2, signbits: 0 },
});
// 1. q3 -> q1: the `|` applies ToInt32 to surface flags.
console.log(`adapt_q1_flags ${adaptTraceResult(q3Trace(2147483648), { kind: "q1", move: "normal", hull: null }).surfaceFlags}`);
// 2. q3 -> q2: each adapted trace owns its surface.
{
  const policy = { kind: "q2", contentsMask: -1, leafContents: "merged" };
  const first = adaptTraceResult(q3Trace(2), policy), second = adaptTraceResult(q3Trace(4), policy);
  console.log(`adapt_q2_surfaces ${first.surface.flags} ${second.surface.flags}`);
}
// 3. traceBrushMedia: a NaN crossing propagates through Math.min.
{
  const plane = { normal: { x: 1, y: 0, z: 0 }, distance: 0, type: 0, signbits: 0 };
  const media = traceBrushMedia([{ contents: 32, planes: [plane] }], "q2", (_p: unknown, e: string) => (e === "start" ? -1 : NaN), createNumericOperations(Q1_DONOR_PROFILE), 1);
  console.log(`media_nan inOpen ${media.inOpen} inWater ${media.inWater}`);
}
// 4. traceQ2Box: a NaN leave on one axis rejects the other axis' entry.
{
  const owner = createIdentityOwner("gate");
  const id = owner.actor(3, 1);
  const actor = {
    body: { actor: id, state: { origin: zero, angles: zero, velocity: zero, bounds: { min: { x: -16, y: -16, z: -16 }, max: { x: 16, y: 16, z: 16 } }, ground: null }, absoluteBounds: { min: zero, max: zero } },
    collision: { family: "q2", role: "solid", shape: { kind: "box" }, contents: 1, owner: null, deadMonster: false, monster: false },
  };
  const result = traceActorBody(
    { start: { x: NaN, y: -64, z: 0 }, end: { x: NaN, y: 64, z: 0 }, shape: { kind: "point" }, target: { kind: "world" }, policy: { kind: "q2", contentsMask: -1, leafContents: "merged" }, numeric: Q2_DONOR_PROFILE, passActor: null } as never,
    actor as never,
  );
  console.log(`q2box_nan fraction ${result.fraction} hit ${result.hit.kind}`);
}
// 5. Temporary box models own fround-staged bounds.
{
  const model = createBoxModel({ min: { x: 0.1, y: 0.1, z: 0.1 }, max: { x: 1, y: 1, z: 1 } });
  console.log(`box_bounds ${model.bounds.min.x} contents ${model.pointContents({ x: 0.1, y: 0.5, z: 0.5 })}`);
}
// 6. transportAttachments follows attachment insertion order, not slot order.
{
  const { SessionActorRegistry } = await import(`${TS}/world/actors/registry.ts`);
  const { SharedBodyTable, translatedBodyBounds } = await import(`${TS}/world/actors/body.ts`);
  const registry = new SessionActorRegistry(createIdentityOwner("gate-bodies"));
  const order: number[] = [];
  const table = new SharedBodyTable(registry, { absoluteBounds: translatedBodyBounds, onLink: (body: { actor: { slot: number } }) => (order.push(body.actor.slot), undefined), onUnlink: () => undefined });
  const bounds = { min: zero, max: zero };
  const state = (x: number) => ({ origin: { x, y: 0, z: 0 }, angles: zero, velocity: zero, bounds, ground: null });
  const anchor = registry.allocate("test", "test:anchor"), first = registry.allocate("test", "test:a"), second = registry.allocate("test", "test:b");
  table.create(anchor, state(0));
  table.create(first, state(100));
  table.create(second, state(200));
  table.attach(second, { anchor: anchor.id, follow: { kind: "translation", offset: { x: 1, y: 0, z: 0 } } });
  table.attach(first, { anchor: anchor.id, follow: { kind: "translation", offset: { x: 2, y: 0, z: 0 } } });
  table.transportAttachments(createNumericOperations(Q1_DONOR_PROFILE));
  console.log(`transport_order ${order.join(",")} slots ${anchor.id.slot},${first.id.slot},${second.id.slot}`);
}
