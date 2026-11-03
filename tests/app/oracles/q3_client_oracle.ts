// Oracle for tests/app/test_top_q3_client.c (retail case): ApplicationQ3Client
// (kind "qvm") and ApplicationQvmClient over baseq3 vm/ui.qvm and vm/cgame.qvm
// on q3dm1, driven by a fixed game state and snapshot session (the fixture
// below; the C test builds the same one). Prints the client's console output,
// the registered models and skins, and a few frames of draw commands.
// Run from this repository: QTS_TEST_CORPUS=~/qfiles bun tests/app/oracles/q3_client_oracle.ts > tests/app/oracles/q3_client.txt
import { mkdtempSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { parseApplicationCommand } from "../../../../quake-anthology-ts/src/app/bootstrap/options.ts";
import { loadApplicationContent } from "../../../../quake-anthology-ts/src/app/bootstrap/content.ts";
import { ApplicationAssets } from "../../../../quake-anthology-ts/src/app/bootstrap/assets.ts";
import { ApplicationAudio } from "../../../../quake-anthology-ts/src/app/bootstrap/audio.ts";
import { ApplicationInput } from "../../../../quake-anthology-ts/src/app/bootstrap/input.ts";
import { ApplicationKeys } from "../../../../quake-anthology-ts/src/app/bootstrap/keys.ts";
import { ApplicationQ3Client } from "../../../../quake-anthology-ts/src/app/bootstrap/q3-client.ts";
import { createIdentityOwner } from "../../../../quake-anthology-ts/src/contracts/identity.ts";
import { SessionClient, SessionSeat } from "../../../../quake-anthology-ts/src/world/session/session.ts";
import { ConfigStore } from "../../../../quake-anthology-ts/src/settings/config.ts";
import { CvarRegistry } from "../../../../quake-anthology-ts/src/core/cvars/index.ts";
import { SdlWindow } from "../../../../quake-anthology-ts/src/platform/sdl.ts";
import { createSceneQueries } from "../../../../quake-anthology-ts/src/world/collision/index.ts";
import { Q3BrowserView } from "../../../../quake-anthology-ts/src/network/q3/browser-view.ts";
import { ClientGameStateStorage } from "../../../../quake-anthology-ts/src/network/q3/game-state.ts";
import { PlayerStateRecord } from "../../../../quake-anthology-ts/src/network/q3/state/player.ts";

const root = process.env["QTS_TEST_CORPUS"] ?? "";
const f = (n: number): string => (Object.is(n, -0) ? 0 : n).toFixed(3);
const out = (line: string): void => { console.log(line); };
const escape = (text: string): string => text.replace(/\\/g, "\\\\").replace(/\n/g, "\\n");

// ---- fixture (mirrored in tests/app/test_top_q3_client.c) ----
const CONFIGSTRINGS: readonly (readonly [number, string])[] = [
  [0, "\\sv_hostname\\fixture\\g_gametype\\0\\sv_maxclients\\8\\timelimit\\0\\fraglimit\\20\\dmflags\\0\\version\\Q3 1.32 fixture\\protocol\\68\\mapname\\q3dm1"],
  [1, "\\sv_serverid\\7\\sv_pure\\0\\timescale\\1\\g_syncronousClients\\0\\sv_cheats\\0"],
  [20, "baseq3-1"],
  [21, "0"],
  [33, "maps/q3dm1.bsp"],
  [544, "n\\Fixture\\t\\0\\model\\sarge\\hmodel\\sarge\\c1\\4\\c2\\5\\hc\\100\\w\\0\\l\\0\\tt\\0\\tl\\0"],
];
const SNAPSHOT_TIMES = [1000, 1050, 1100, 1150, 1200];
const FRAMES = 4;
function playerState(serverTime: number): PlayerStateRecord<number, number, number> {
  const state = new PlayerStateRecord("baseq3", 0, 2, 0);
  state.commandTime = serverTime;
  state.origin = { x: -352, y: 1112, z: 48 };
  state.viewangles = { x: 0, y: 90, z: 0 };
  state.viewheight = 26;
  state.clientNum = 0;
  state.speed = 320;
  state.gravity = 800;
  state.stats.set(0, 100); // STAT_HEALTH
  state.stats.set(2, (1 << 1) | (1 << 2)); // STAT_WEAPONS: gauntlet, machinegun
  state.stats.set(6, 100); // STAT_MAX_HEALTH
  state.ammo.set(2, 100);
  state.persistant.set(3, 0); // PERS_TEAM
  return state;
}

async function main(): Promise<void> {
  const command = parseApplicationCommand(["--game", "q3-baseq3", "--map", "q3dm1", "--content-root", root]);
  if (command.kind !== "run") throw new Error("Expected run");
  const store = mkdtempSync(join(tmpdir(), "q3-client-oracle-"));
  const options = { ...command.options, userContentRoot: store };
  const content = await loadApplicationContent(options), identity = createIdentityOwner("q3-client");
  const assets = new ApplicationAssets(content, { identity: Symbol("q3-client"), session: identity.session, generation: 0 });
  await assets.loadWorld();
  let clock = 0;
  const now = (): number => clock;
  const client = new SessionClient(identity.client(0, 1)), seat = new SessionSeat(identity.seat(0), client);
  const actor = identity.actor(0, 0);
  const window = SdlWindow.open({ title: "Quake", backend: "cpu", width: 640, height: 480, hidden: true });
  const input = await ApplicationInput.open(
    window,
    [{ seat, actor }],
    options,
    "q3",
    { playerView: () => ({ origin: { x: 0, y: 0, z: 0 }, angles: { x: 0, y: 90, z: 0 }, viewHeight: 26 }) } as any,
    { quit: () => undefined, execute: () => undefined, print: () => undefined },
    now,
    new ConfigStore(store),
  );
  const local = input.locals[0]!;
  const audio = new ApplicationAudio(content, now, 1, "missing-custom-model", () => undefined, { deferOutput: true });
  const keyCvars = new CvarRegistry({ dialect: "q3", context: { session: identity.session, origin: { kind: "local-console" } }, print: () => undefined });
  const keys = await new ApplicationKeys(() => undefined).prepare(options, content.catalog, keyCvars);
  if (keys === null) throw new Error("Q3 key profile");
  const queries = createSceneQueries(assets.world.map);
  const browser = new Q3BrowserView({
    browser: {
      q3Core: {} as any,
      q3List: () => ({ addresses: [], pending: false, generation: 0, resetGeneration: 0 }),
      resolveQ3: async () => null,
      addQ3: () => undefined,
      removeQ3: () => undefined,
      scanQ3: () => undefined,
      requestQ3Master: async () => undefined,
      loadQ3Cache: async () => null,
      saveQ3Cache: async () => undefined,
      assertOpen: () => undefined,
    },
    now,
    maxPing: () => 800,
    statusResendTime: () => 1000,
  } as any);
  const gameState = new ClientGameStateStorage(message => { throw new Error(message); });
  gameState.beginEntries();
  for (const [index, value] of CONFIGSTRINGS) gameState.append(index, value);
  let snapshot = 0;
  const snapshotAt = (number: number) => {
    const serverTime = SNAPSHOT_TIMES[number - 1];
    if (serverTime === undefined) return null;
    return { messageNumber: number, serverTime, deltaNumber: number - 1, flags: 0, serverCommandNumber: 0, parseEntitiesNumber: 0,
      areaMask: new Uint8Array(32), playerState: playerState(serverTime), entities: [] };
  };
  const wireCommand = (number: number) => ({ serverTime: SNAPSHOT_TIMES[Math.min(number, SNAPSHOT_TIMES.length) - 1] ?? 0, angles: [0, 0, 0] as [number, number, number],
    forwardmove: 0, rightmove: 0, upmove: 0, buttons: 0, weapon: 2 });
  const connection = {
    generation: 1,
    serverMessageSequence: 1,
    lastExecutedServerCommand: 0,
    get clientNumber() { return 0; },
    snapshotPing: () => 0,
    getServerCommand: () => null,
    gameState,
    commands: {
      get currentNumber() { return snapshot; },
      read: (number: number) => (number < 1 || number > snapshot ? null : wireCommand(number)),
    },
  };
  const source = {
    sourceMode: "live" as const,
    get time() { return SNAPSHOT_TIMES[Math.max(snapshot, 1) - 1]!; },
    clientNumber: 0,
    serverMessageSequence: 1,
    lastExecutedServerCommand: 0,
    commands: {
      get currentNumber() { return snapshot; },
      read: (number: number) => {
        if (number < 1 || number > snapshot) return null;
        const value = wireCommand(number);
        return { ...value, angles: { x: value.angles[0], y: value.angles[1], z: value.angles[2] } };
      },
    },
    current: () => ({ number: snapshot, serverTime: SNAPSHOT_TIMES[Math.max(snapshot, 1) - 1]! }),
    read: (number: number) => (number < 1 || number > snapshot ? null : snapshotAt(number)),
    getGameState: () => gameState.copyStrings(),
    systemInfo: () => gameState.get(1) ?? "",
    getServerCommand: () => null,
    actorAt: (number: number) => identity.actor(number, 0),
    snapshotPing: () => 0,
  };
  const prints: string[] = [];
  const guestCvars = input.guestCvars(seat.id);
  const q3 = await ApplicationQ3Client.create({
    kind: "qvm",
    saveFontData: () => false,
    assets,
    renderer: { backend: { stencilBits: 8 }, driver: null, glConfig: null } as any,
    queries,
    local,
    audio,
    viewport: () => ({ x: 0, y: 0, width: 640, height: 480 }),
    now,
    commands: { reliable: text => { prints.push(`reliable ${text}`); }, console: text => { prints.push(`console ${text}`); }, print: text => { prints.push(text); } },
    commandRegistration: input.clientCommandRegistration(seat.id),
    localServer: true,
    source,
    connection: connection as any,
    browser,
    keys,
    guestCvars,
    guestInput: input.guestInput(seat.id),
    commandBuffer: input.guestCommands,
    clientState: () => ({ phase: 8, connectPacketCount: 0, clientNumber: 0, serverName: "localhost", message: "" }),
  });
  const flush = (label: string): void => {
    for (const text of prints.splice(0)) out(`${label} print ${escape(text)}`);
  };
  flush("init");
  const resources = (label: string): void => {
    const diagnostics = q3.resourceDiagnostics();
    for (const model of diagnostics.models) out(`${label} model ${model.handle} ${model.path} ${model.model.kind}`);
    for (const skin of diagnostics.skins) out(`${label} skin ${skin.handle} ${skin.path} ${skin.skin.surfaces.length}`);
  };
  resources("init");
  for (let frame = 1; frame <= FRAMES; frame++) {
    snapshot = frame;
    clock = frame * 50;
    await q3.prepare(frame, { x: 0, y: 0, width: 640, height: 480 });
    flush(`frame${frame}`);
    const rendered = q3.frame();
    out(`frame${frame} commands=${rendered.commands.length}`);
    rendered.commands.forEach((command, index) => dumpCommand(`frame${frame}.${index}`, command));
  }
  resources("final");
  await q3.shutdown();
  flush("shutdown");
  q3.close();
  audio.close?.();
  assets.close();
  await content.close();
  window.close();
}

function imageKey(image: any): string {
  if (image === undefined || image === null) return "none";
  const source = image.source.kind === "resource" ? image.source.resource.requestedPath : image.source.name;
  return `${image.ordinal}:${image.width}x${image.height}:${source}`;
}
function dumpBatch(prefix: string, batch: any): void {
  let p = 0, c = 0, t = 0;
  for (const v of batch.vertices) { p += v.position.x + v.position.y + v.position.z + v.position.w; c += v.color.x + v.color.y + v.color.z + v.color.w; t += v.texCoord.x + v.texCoord.y; }
  out(`${prefix} v=${batch.vertices.length} i=${batch.indices.length} p=${f(p)} c=${f(c)} t=${f(t)} depthWrite=${batch.state.depthWrite}`);
}
function dumpCommand(prefix: string, command: any): void {
  switch (command.kind) {
    case "draw-buffer": out(`${prefix} draw-buffer ${command.buffer} clear=${command.clear}`); return;
    case "image-resource": out(`${prefix} image-resource ${command.operation.kind} ${imageKey(command.operation.image)}`); return;
    case "set-color": out(`${prefix} set-color ${[command.color.x, command.color.y, command.color.z, command.color.w].map(f).join(" ")}`); return;
    case "stretch-pic":
      out(`${prefix} stretch-pic ${[command.rect.x, command.rect.y, command.rect.width, command.rect.height, command.uv.s1, command.uv.t1, command.uv.s2, command.uv.t2].map(f).join(" ")} ${imageKey(command.image)}`);
      return;
    case "view": {
      const view = command.view, viewport = view.viewport;
      out(`${prefix} view ${[viewport.x, viewport.y, viewport.width, viewport.height].map(f).join(" ")} clear=${view.clear === null ? "null" : `${view.clear.color === null ? "depth" : "color"}`} ops=${view.operations.length}`);
      view.operations.forEach((operation: any, index: number) => {
        out(`${prefix}.${index} ${operation.kind} batches=${operation.batches?.length ?? 0}`);
        (operation.batches ?? []).forEach((batch: any, b: number) => dumpBatch(`${prefix}.${index}.${b}`, batch));
      });
      return;
    }
    default: out(`${prefix} ${command.kind}`);
  }
}

await main();
