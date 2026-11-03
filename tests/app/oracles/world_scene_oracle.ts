// Oracle for tests/app/test_world_seat_presentation.c: ApplicationWorldScene,
// ForeignHeldWeapons and SelectedQ3WeaponPresenter over retail content.
// Run: QTS_TEST_CORPUS=~/qfiles bun tests/app/oracles/world_scene_oracle.ts > tests/app/oracles/world_scene.txt
import { join } from "node:path";
import { parseApplicationCommand } from "../../../../quake-anthology-ts/src/app/bootstrap/options.ts";
import { loadApplicationContent } from "../../../../quake-anthology-ts/src/app/bootstrap/content.ts";
import { ApplicationAssets } from "../../../../quake-anthology-ts/src/app/bootstrap/assets.ts";
import { ApplicationWorldScene } from "../../../../quake-anthology-ts/src/app/bootstrap/presentation-scene.ts";
import { ForeignHeldWeapons } from "../../../../quake-anthology-ts/src/app/bootstrap/held-weapon.ts";
import { createIdentityOwner } from "../../../../quake-anthology-ts/src/contracts/identity.ts";
import { anglesToAxis } from "../../../../quake-anthology-ts/src/core/math.ts";
import { perspectiveProjection } from "../../../../quake-anthology-ts/src/render/scene/view.ts";

const root = process.env["QTS_TEST_CORPUS"] ?? "";
const f = (n: number): string => (Object.is(n, -0) ? 0 : n).toFixed(3);
const out = (line: string): void => { console.log(line); };

function dumpBatch(prefix: string, batch: any): void {
  let p = 0, c = 0, t = 0;
  for (const v of batch.vertices) { p += v.position.x + v.position.y + v.position.z + v.position.w; c += v.color.x + v.color.y + v.color.z + v.color.w; t += v.texCoord.x + v.texCoord.y; }
  out(`${prefix} v=${batch.vertices.length} i=${batch.indices.length} p=${f(p)} c=${f(c)} t=${f(t)} depthWrite=${batch.state.depthWrite} pair=${batch.texturing === "pair"}`);
}
function dumpOperations(label: string, operations: readonly any[]): void {
  out(`${label} count=${operations.length}`);
  operations.forEach((operation, index) => {
    if (operation.kind !== "scene-group") { out(`${label} ${index} op=${operation.kind}`); return; }
    const order = operation.order;
    const key = order.kind === "sequence" ? order.phase : order.kind === "source" ? `${order.source.entity.kind}:${order.source.surface}:${order.source.fog}` : "compiled";
    out(`${label} ${index} group=${order.kind}/${key} ops=${operation.operations.length}`);
    operation.operations.forEach((render: any, r: number) => {
      out(`${label} ${index}.${r} kind=${render.kind}${render.kind === "object-opacity" ? ` opacity=${f(render.opacity)}` : ""} batches=${render.batches?.length ?? 0}`);
      (render.batches ?? []).forEach((batch: any, b: number) => dumpBatch(`${label} ${index}.${r}.${b}`, batch));
    });
  });
}

async function q1(): Promise<void> {
  const command = parseApplicationCommand(["--game", "q1-classic-id1", "--map", "e1m1", "--dedicated", "--content-root", root]);
  if (command.kind !== "run") throw new Error("Expected run");
  const content = await loadApplicationContent(command.options), identity = createIdentityOwner("world-scene");
  const assets = new ApplicationAssets(content, { identity: Symbol("world-scene"), session: identity.session, generation: 0 });
  try {
    await assets.loadWorld();
    const scene = new ApplicationWorldScene(assets);
    const viewer = identity.actor(0, 0), other = identity.actor(1, 0), third = identity.actor(2, 0), fourth = identity.actor(3, 0);
    const entities = content.recipe.map.entities.content;
    const base = { content: entities, family: "q1" as const, frame: 0, oldFrame: 0, skin: 0, effects: 0, renderFlags: 0, angles: { x: 0, y: 30, z: 0 }, scale: 1, visible: true, viewWeapon: false };
    const presentations = [
      { ...base, actor: other, path: "progs/soldier.mdl", origin: { x: 480, y: -352, z: 88 }, alpha: Math.fround(0.371337), frame: 3, oldFrame: 2, backLerp: 0.25 },
      { ...base, actor: third, path: "progs/flame.mdl", origin: { x: 400, y: -300, z: 80 }, scale: Math.fround(1.31337) },
      { ...base, actor: fourth, path: "*1", origin: { x: 0, y: 0, z: 0 }, alpha: 0.5 },
      { ...base, actor: identity.actor(4, 0), path: "progs/s_explod.spr", origin: { x: 420, y: -330, z: 90 }, frame: 2, oldFrame: 2 },
      { ...base, actor: viewer, path: "progs/v_shot.mdl", origin: { x: 480, y: -400, z: 110 }, viewOrigin: { x: 480, y: -400, z: 110 }, viewWeapon: true },
      { ...base, actor: identity.actor(5, 0), path: "progs/player.mdl", origin: { x: 500, y: -380, z: 88 }, frame: 12, oldFrame: 11, backLerp: 0.5 },
    ];
    const snapshot: any = { frame: { frame: 1, time: { kind: "seconds", value: 1.25 } } };
    scene.receive([{ kind: "q1", event: { kind: "lightstyle", style: 0, pattern: "abcdefghijklmnopqrstuvwxyz" }, sequence: 1, content: entities, seconds: 1 } as any,
      { kind: "q1", event: { kind: "lightstyle", style: 5, pattern: "mmnmmommommnonmmonqnmmo" }, sequence: 2, content: entities, seconds: 1 } as any]);
    await scene.prepare(viewer, snapshot, presentations as any, []);
    const styles = scene.styles();
    out(`q1.styles ${[0, 1, 5, 63].map(index => f(styles.q1Styles![index]!)).join(" ")} ${[0, 5, 7].map(index => f(styles.q2Styles![index]!.white)).join(" ")} style0=${f(scene.style(0, 12))}`);
    const camera = { origin: { x: 360, y: -352, z: 100 }, axis: anglesToAxis({ x: 0, y: 0, z: 0 }), projection: perspectiveProjection(90, 73.74, 16384),
      viewport: { x: 0, y: 0, width: 640, height: 480 }, clip: { kind: "none" } } as const;
    const input = { camera, target: { kind: "seat", seat: identity.seat(0) }, time: { kind: "seconds", value: 1.25 } } as any;
    dumpOperations("q1.supplemental", scene.supplemental(input));
    const prepared = scene.view(input, [], [], false);
    dumpOperations("q1.view.objects", prepared.view.operations.filter((operation: any) => operation.kind === "object-opacity").map((operation: any) => ({ kind: "scene-group", order: { kind: "sequence", phase: "translucent" }, operations: [operation] })));
    // ForeignHeldWeapons over the application assets: the Q1 authored gun subset.
    const held = new ForeignHeldWeapons(assets);
    const passes = await held.frame({ ...base, actor: viewer, path: "progs/v_shot.mdl", origin: { x: 0, y: 0, z: 0 }, viewWeapon: true } as any, { origin: { x: 1, y: 2, z: 3 }, color: { x: 1, y: 1, z: 1, w: 1 } });
    for (const pass of passes) {
      const entity = pass.entity, model: any = entity.model, t = entity.transform;
      out(`q1.held path=${entity.resource.requestedPath} kind=${model.kind} triangles=${model.triangles.length} frame=${(entity.pose as any).frame} opacity=${entity.opacity}`);
      out(`q1.held transform ${[t.origin.x, t.origin.y, t.origin.z, ...t.axis.flatMap(v => [v.x, v.y, v.z])].map(f).join(" ")}`);
    }
    scene.close();
  } finally { assets.close(); await content.close(); }
}

async function q3(): Promise<void> {
  const command = parseApplicationCommand(["--game", "q3-baseq3", "--map", "q3dm1", "--dedicated", "--content-root", root]);
  if (command.kind !== "run") throw new Error("Expected run");
  const content = await loadApplicationContent(command.options), identity = createIdentityOwner("world-scene-q3");
  const assets = new ApplicationAssets(content, { identity: Symbol("world-scene-q3"), session: identity.session, generation: 0 });
  try {
    await assets.loadWorld();
    const scene = new ApplicationWorldScene(assets);
    const viewer = identity.actor(0, 0);
    const contentId = content.recipe.presentation.assets;
    const weapon = { actor: viewer, content: contentId, family: "q3" as const, path: "models/weapons2/shotgun/shotgun.md3", frame: 0, oldFrame: 0, skin: 0, effects: 0, renderFlags: 0,
      origin: { x: 100, y: 200, z: 60 }, angles: { x: 5, y: 40, z: 0 }, scale: 1, visible: true, viewWeapon: true,
      q3Weapon: { timeMilliseconds: 1250, torsoAnimation: 11, lastFireMilliseconds: 1240, firing: false, horizontalSpeed: 120, bobCycle: 37, weapon: 3 } };
    const snapshot: any = { frame: { frame: 1, time: { kind: "milliseconds", value: 1250 } } };
    await scene.prepare(viewer, snapshot, [weapon] as any, [], 90);
    const camera = { origin: { x: 100, y: 200, z: 86 }, axis: anglesToAxis({ x: 5, y: 40, z: 0 }), projection: perspectiveProjection(90, 73.74, 16384),
      viewport: { x: 0, y: 0, width: 640, height: 480 }, clip: { kind: "none" } } as const;
    const input = { camera, target: { kind: "seat", seat: identity.seat(0) }, time: { kind: "milliseconds", value: 1250 } } as any;
    dumpOperations("q3.supplemental", scene.supplemental(input));
    scene.close();
  } finally { assets.close(); await content.close(); }
}

await q1();
await q3();
void join;
