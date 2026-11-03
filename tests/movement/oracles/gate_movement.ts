// Oracle for tests/movement/test_gate_movement.c. Run: bun tests/movement/oracles/gate_movement.ts
const TS = "../../../../quake-anthology-ts/src";
const { sweepBody } = await import(`${TS}/movement/swept-body.ts`);
const { clientMovementMode } = await import(`${TS}/movement/client-outputs.ts`);
const { pushQ1Pusher } = await import(`${TS}/movement/q1/pusher.ts`);
const { Q3PredictionRuntime } = await import(`${TS}/movement/q3/prediction.ts`);
const { createIdentityOwner } = await import(`${TS}/contracts/identity.ts`);
const owner = createIdentityOwner("gate");

type V = { x: number; y: number; z: number };
const dot = (a: V, b: V) => a.x * b.x + a.y * b.y + a.z * b.z;
const scale = (a: V, s: number) => ({ x: a.x * s, y: a.y * s, z: a.z * s });
// 1. sweepBody: a NaN candidate/plane dot rejects the candidate (crease path).
{
  const normals: V[] = [{ x: 1, y: 0, z: 0 }, { x: 0, y: 1, z: 0 }, { x: 0, y: 0, z: 1 }];
  let state = { origin: { x: 0, y: 0, z: 0 }, velocity: { x: NaN, y: 0, z: 0 } };
  let bump = 0;
  const stop = sweepBody(
    {
      read: () => state,
      writeOrigin: o => (state = { ...state, origin: o }),
      writeVelocity: v => (state = { ...state, velocity: v }),
      trace: () => ({ fraction: 0, end: state.origin, allSolid: false, startSolid: false, n: normals[bump++] }),
      normal: (t: { n: V }) => t.n,
      impact: () => {},
      stopWhenStill: false,
      samePlane: (a, b) => a === b,
      math: {
        advance: o => o,
        remaining: t => t,
        clip: (v, n) => ({ x: v.x - n.x * dot(v, n), y: v.y - n.y * dot(v, n), z: v.z - n.z * dot(v, n) }),
        dot,
        cross: (a, b) => ({ x: a.y * b.z - a.z * b.y, y: a.z * b.x - a.x * b.z, z: a.x * b.y - a.y * b.x }),
        scale,
      },
    },
    1,
  );
  console.log(`sweep_nan_stop ${stop} bumps ${bump}`);
}
// 2. clientMovementMode with NaN health.
console.log(`mode_nan_health ${String(clientMovementMode({ mode: "noclip" } as never, NaN))}`);
console.log(`mode_live_health ${String(clientMovementMode({ mode: "noclip" } as never, 1))}`);
// 3. pushQ1Pusher: the finally re-enables pusher collision when push throws.
{
  const calls: string[] = [];
  const pusherId = owner.actor(1, 1);
  const riderId = owner.actor(2, 1);
  const bounds = { min: { x: -1, y: -1, z: -1 }, max: { x: 1, y: 1, z: 1 } };
  const entity = (id: typeof pusherId, moveType: number) => ({
    actor: { id, owner: "test" },
    state: { flags: 0, ground: { kind: "none" }, moveType, origin: { x: 0, y: 0, z: 0 }, angles: { x: 0, y: 0, z: 0 } },
    bounds,
    absoluteBounds: bounds,
    solid: "box",
    localTimeSeconds: 0,
    nextThinkSeconds: 0,
  });
  const numeric = { store: (v: number) => v, add: (a: number, b: number) => a + b, subtract: (a: number, b: number) => a - b, multiply: (a: number, b: number) => a * b, divide: (a: number, b: number) => a / b, squareRoot: Math.sqrt, profile: { arithmetic: { kind: "binary64" } } };
  let caught = "";
  try {
    pushQ1Pusher(
      { actor: pusherId, displacement: { x: 1, y: 0, z: 0 }, angularDisplacement: { x: 0, y: 0, z: 0 }, elapsedSeconds: 0.1 },
      {
        movement: { numeric },
        read: (id: typeof pusherId) => (id.slot === 1 ? entity(pusherId, 7) : entity(riderId, 3)),
        candidates: () => [riderId],
        write: () => undefined,
        link: () => undefined,
        collisionEnabled: (_a: unknown, enabled: boolean) => {
          calls.push(`collision ${enabled}`);
          return undefined;
        },
        testPosition: () => ({ kind: "world", model: 0 }),
        push: () => {
          calls.push("push");
          throw new Error("trigger failed");
        },
        blocked: () => undefined,
      } as never,
    );
  } catch (error) {
    caught = (error as Error).message;
  }
  console.log(`pusher_finally ${calls.join(",")} caught ${caught}`);
}
// 4. Q3 prediction replay ends after command number INT32_MAX.
{
  let reads = 0;
  const movement = { commandTimeMilliseconds: 0, movementFlags: 0 };
  const runtime = new Q3PredictionRuntime({
    commands: {
      currentNumber: 2147483647,
      read: () => {
        reads++;
        return { kind: "q3", serverTimeMilliseconds: 0, angleWords: [0, 0, 0], buttons: 0, weapon: 0, forwardMove: 0, rightMove: 0, upMove: 0 };
      },
    },
    settings: () => ({ demoPlayback: false, noPredict: false, synchronousClients: false, fixed: false, movementMilliseconds: 8, errorDecayInteger: 0, errorDecayValue: 0, showMiss: 0 }),
    setMovementMilliseconds: () => {},
    restoreSnapshot: () => {},
    warn: () => {},
  } as never);
  const output = await runtime.predict({
    timeMilliseconds: 0,
    previousTimeMilliseconds: 0,
    snapshot: { serverTimeMilliseconds: 0, actor: { movement } },
    nextSnapshot: null,
    nextFrameTeleport: false,
    thisFrameTeleport: false,
    health: 100,
  } as never);
  console.log(`prediction_int32_max ${output.status} reads ${reads}`);
}
