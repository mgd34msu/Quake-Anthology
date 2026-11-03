// Oracle for tests/app/test_native_mod_host.c: runs the TS native mod classes
// (createNativeModHost, NativeModProvider, prepareNativeMod) on the retail Q2
// DLLs and base1, printing the values the C test checks. Run from the TS tree:
//   cd ../quake-anthology-ts && bun run ../quake-anthology-c/tests/app/oracles/native_mod_host_oracle.ts
// QA_DUMP_DIR=dir also writes the host-mode checkpoints. GPL-2.0-or-later.
const TS = `${process.env.QA_TS_ROOT ?? `${process.env.HOME}/Projects/quake-anthology-ts`}/src`;
const { createHash } = await import("node:crypto");
const { readFileSync } = await import("node:fs");
const { openArchive } = await import(`${TS}/content/archive/index.ts`);
const { decodeQ2Map } = await import(`${TS}/formats/q2-map/index.ts`);
const { createIdentityOwner } = await import(`${TS}/contracts/identity.ts`);
const { createSceneQueries } = await import(`${TS}/world/collision/index.ts`);
const { SharedPhysics } = await import(`${TS}/app/bootstrap/simulation/physics.ts`);
const { ActorCallbackTable, SessionActorRegistry } = await import(`${TS}/world/actors/index.ts`);
const { GameplayAuthority, SharedInventoryTable } = await import(`${TS}/world/gameplay/index.ts`);
const { Q2_DONOR_PROFILE } = await import(`${TS}/core/numeric.ts`);
const { readNativeModCallbacks } = await import(`${TS}/content/mods/native-callbacks.ts`);
const { NativeModProvider } = await import(`${TS}/compat/q2/native-mod-provider.ts`);
const { createNativeModHost } = await import(`${TS}/app/bootstrap/simulation/native-mod-host.ts`);
const { prepareNativeMod } = await import(`${TS}/app/bootstrap/simulation/native-mod.ts`);
const { ResourceScope } = await import(`${TS}/world/session/resources.ts`);

const Q2 = `${process.env.QA_CORPUS ?? `${process.env.HOME}/qfiles`}/Quake 2`;
const archive = await openArchive(`${Q2}/baseq2/pak0.pak`);
const geometry = decodeQ2Map(await archive.readEntry(archive.findEntries("maps/base1.bsp")[0]), "base1");
await archive.close();
const sha = (bytes: Uint8Array) => createHash("sha256").update(bytes).digest("hex");

export function declarationJson(rerelease: boolean, digest: string): string {
  const call = (name: string) => ({ entry: { kind: "game-export", name }, arguments: rerelease ? [{ kind: "uint8", value: { kind: "float", value: 1 } }] : [], globals: [], returns: "void" });
  return JSON.stringify({ version: 1, runtime: "native", program: { path: rerelease ? "game_x64.dll" : "gamex86.dll", digest },
    target: rerelease ? { api: { kind: "q2-rerelease-game", version: 2023 }, abi: { kind: "windows-x86-64", image: "pe32+", pointerBytes: 8, call: "microsoft-x64" } }
      : { api: { kind: "q2-classic-game", version: 3 }, abi: { kind: "windows-i386", image: "pe32", pointerBytes: 4, call: "cdecl" } },
    cvars: [{ name: "fixture_flag", value: "1" }, { name: "skill", value: "3" }], spawnEntities: "{\n\"classname\" \"worldspawn\"\n\"message\" \"Native Fixture\"\n}\n",
    actorRecords: [], entityRecord: null, initialize: [call("RunFrame")], project: [], release: [],
    callbacks: [{ id: "fixture:frame", operation: "actor.think", stage: "observe", ...call("RunFrame") }] });
}

function run(rerelease: boolean, viaPrepared: boolean) {
  const dllPath = rerelease ? `${Q2}/rerelease/baseq2/game_x64.dll` : `${Q2}/ctf/gamex86.dll`;
  const program = new Uint8Array(readFileSync(dllPath)), digest = `sha256:${sha(program)}`;
  const declaration = readNativeModCallbacks(new TextEncoder().encode(declarationJson(rerelease, digest)));
  const owner = createIdentityOwner("native-mod-oracle");
  const actors = new SessionActorRegistry(owner, 4096), callbacks = new ActorCallbackTable(actors), scene = createSceneQueries(geometry);
  const world = actors.allocate("q2:world-fixture", "q2:worldspawn");
  const physics = new SharedPhysics({ actors, callbacks, scene, numeric: Q2_DONOR_PROFILE, sourceOrder: (a: any, b: any) => a.slot - b.slot, worldActor: () => world.id, onBlocked: () => undefined });
  const combat = new GameplayAuthority(actors, callbacks, { impulse: () => undefined, beforeReaction: () => undefined, confirmed: () => undefined }), inventory = new SharedInventoryTable(actors);
  const prints: string[] = [], emitted: string[] = [];
  let time = 1;
  const engineFor = (_source: any, runtime: any) => ({ actors, callbacks, bodies: physics.bodies, combat, inventory,
    now: () => runtime.now(), frameSeconds: () => runtime.frameSeconds(), gravity: () => 800, random: () => runtime.random.nextUnit(),
    schedule: () => undefined, touchTriggers: () => undefined, trace: (request: any) => physics.trace(request),
    pointContents: (point: any) => { const result = scene.pointContents({ point, target: { kind: "world" }, policy: { kind: "q2", contentsMask: -1, leafContents: "merged" }, numeric: Q2_DONOR_PROFILE, passActor: null }); return result.kind === "q2" ? result.merged : result.contents; },
    inPvs: (a: any, b: any) => scene.clusterVisible(scene.leafCluster(scene.pointLeaf(a)), scene.leafCluster(scene.pointLeaf(b)), "pvs"),
    inPhs: (a: any, b: any) => scene.clusterVisible(scene.leafCluster(scene.pointLeaf(a)), scene.leafCluster(scene.pointLeaf(b)), "phs"),
    areasConnected: () => true, nearby: () => [], players: () => [], worldActor: () => world.id, isPlayer: () => false, isMonster: () => false,
    setAreaPortal: (portal: number, open: boolean) => { scene.setAreaPortalState(portal, open); return undefined; },
    setSolid: (actor: any, solid: any, model: any) => physics.setSolid(actor, solid, model, "q2"), inlineModelBounds: (model: number) => scene.modelBounds(model),
    setMotion: () => undefined, emit: (event: any) => { emitted.push(event.kind); return undefined; } });
  const native = { scene, mapPath: "maps/base1.bsp", maxClients: 1, frameMilliseconds: 25, skill: 1, mode: "singleplayer" as const, gravity: 800,
    clock: { nowMilliseconds: () => 1_700_000_000_000, performanceCounter: () => 12_345_678n, performanceFrequency: 1_000_000n },
    navigation: { runtime: () => null, moveToPoint: () => "failed" as any, followActor: () => "failed" as any },
    engine: engineFor, collision: (actor: any, value: any) => physics.setCollision(actor, value) };
  const events: string[] = [];
  const services: any = { native, actors, bodies: physics.bodies, combat, inventory, seed: 7, callbacks,
    engine: { print: (text: string) => { prints.push(text); return undefined; }, events: { emit: (_content: any, event: any) => { events.push(event.kind); return undefined; }, registerResource: () => undefined },
      scene, world: () => world.id },
    time: () => ({ kind: "seconds", value: time }) };
  const out: Record<string, unknown> = { edition: rerelease ? "rerelease" : "classic", digest };
  if (viaPrepared) {
    const artifact = { id: "resource:fixture", requestedPath: rerelease ? "game_x64.dll" : "gamex86.dll", digest, mount: "mount:test:native" } as any;
    const description = { selection: { product: "q2-fixture", id: "native" }, source: { provider: "q2:fixture-source", content: "q2:fixture-native" }, title: "Fixture", sourceTitle: "Fixture", purpose: "addition", requires: [], conflicts: [], availability: { kind: "available" } } as any;
    const prepared = prepareNativeMod({ description, declaration, declarationDigest: `sha256:${"cd".repeat(32)}`, program, artifact, localize: (key: string) => key });
    out.identity = prepared.identity; out.moduleCheckpoint = prepared.moduleCheckpoint;
    const resources = new ResourceScope("fixture");
    return (async () => {
      const runtime = await prepared.initialize({ restoring: false, services, instance: "mod:q2-fixture%2Fnative", resources, nextFrame: async () => undefined, assertCurrent: () => undefined });
      const state = await runtime.checkpoint!();
      out.checkpointSha = sha(state.providers[0].bytes); out.checkpointLength = state.providers[0].bytes.length;
      prepared.validateState(state);
      await runtime.restore!(state);
      const again = await runtime.checkpoint!();
      out.restoredSha = sha(again.providers[0].bytes);
      try { runtime.close(); resources.close(); } catch (error) { out.closeError = String(error); }
      out.prints = prints.length; return out;
    })();
  }
  const provider = new NativeModProvider(declaration, services, "mod:fixture-native", native.mapPath, () => undefined);
  provider.reserveProtection();
  const source = { provider: "mod:fixture-native", content: "q2:fixture-native" } as any;
  const prepared = { edition: rerelease ? "rerelease" : "classic", execution: { kind: "native", owner: source, role: "server-game", api: declaration.target.api, profile: declaration.target.abi,
    artifact: { id: "resource:fixture", requestedPath: rerelease ? "game_x64.dll" : "gamex86.dll", digest, mount: "mount:test:native" } }, bytes: program } as any;
  const host = createNativeModHost({ prepared, declaration, source, services, context: native as any, projection: provider, localize: (key: string) => key, nextFrame: async () => undefined });
  provider.attach(host);
  return (async () => {
    await provider.initialize(false);
    const table = host.entities();
    out.entities = { base: table.base.byteOffset.toString(16), stride: table.stride, count: table.count, capacity: table.capacity };
    out.imageBase = host.imageBase.byteOffset.toString(16);
    out.runFrame = host.gameEntry("RunFrame").address.byteOffset.toString(16);
    out.cvars = ["maxclients", "skill", "deathmatch", "coop", "sv_gravity", "fixture_flag", "gamename", "dmflags"].map(name => [name, host.cvars.find(name)?.value ?? null]);
    out.world = sha(new Uint8Array(host.entity(0).bytes.buffer, host.entity(0).bytes.byteOffset, host.entity(0).bytes.byteLength)); out.worldActive = host.active(0); out.client1 = host.client(1)?.byteOffset.toString(16) ?? null;
    out.weaponModel = host.weaponModel(1);
    const invoked = provider.invoke(declaration.callbacks[0], new Map());
    out.invoked = invoked;
    out.worldAfter = sha(new Uint8Array(host.entity(0).bytes.buffer, host.entity(0).bytes.byteOffset, host.entity(0).bytes.byteLength));
    const state = await provider.checkpoint();
    if (process.env.QA_DUMP_DIR) (await import("node:fs")).writeFileSync(`${process.env.QA_DUMP_DIR}/ts-${rerelease ? "rerelease" : "classic"}-host-checkpoint.bin`, state.bytes);
    out.checkpointSha = sha(state.bytes); out.checkpointLength = state.bytes.length;
    time = 2; host.synchronizeFrame(2, 20);
    await provider.restore(state);
    const again = await provider.checkpoint();
    out.restoredSha = sha(again.bytes);
    out.events = events.length; out.prints = prints.length;
    try { provider.close(); } catch (error) { out.closeError = String(error); }
    return out;
  })();
}
for (const rerelease of [false, true]) for (const via of [false, true]) {
  try { console.log(JSON.stringify(await run(rerelease, via), (_k, v) => typeof v === "bigint" ? v.toString() : v)); }
  catch (error) { console.log(`${rerelease ? "rerelease" : "classic"} ${via ? "prepared" : "host"} failed:`, (error as Error).stack, JSON.stringify((function flat(e: any): any { return e?.errors ? e.errors.map(flat) : String(e?.stack ?? e).split("\n").slice(0, 6).join(" | "); })(error))); }
}
