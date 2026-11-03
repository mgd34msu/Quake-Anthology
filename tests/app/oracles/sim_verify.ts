/* Oracle for tests/app/test_sim_verify.c: constructs the TS SharedSimulation
 * the way application.ts does (parseApplicationCommand ->
 * loadApplicationContent -> SimulationOptions, with the guest options of
 * Application.guestOptions on deterministic clocks), admits one player,
 * steps it with a deterministic input script and prints a stable state dump
 * at each checkpoint frame. Run from the TS checkout:
 *   cd ../quake-anthology-ts && bun ../quake-anthology-c/tests/app/oracles/sim_verify.ts <case> <corpus> [frames...]
 * Cases: q1 q2c q2r q3 qc q2nc q2nr q3qvm mixed preset. GPL-2.0-or-later. */
import { createHash } from "node:crypto";
import { mkdirSync, rmSync, writeFileSync } from "node:fs";
import { join } from "node:path";
import { loadApplicationContent } from "../../../../quake-anthology-ts/src/app/bootstrap/content.ts";
import { parseApplicationCommand } from "../../../../quake-anthology-ts/src/app/bootstrap/options.ts";
import { loadSimulation } from "../../../../quake-anthology-ts/src/app/bootstrap/simulation/index.ts";
import type { SharedSimulation } from "../../../../quake-anthology-ts/src/app/bootstrap/simulation/runtime.ts";
import { createIdentityOwner } from "../../../../quake-anthology-ts/src/contracts/identity.ts";
import type { ActorId } from "../../../../quake-anthology-ts/src/contracts/identity.ts";
import type { UserCommand } from "../../../../quake-anthology-ts/src/contracts/protocol.ts";
import type { MovementState } from "../../../../quake-anthology-ts/src/contracts/movement.ts";
import type { ExecutableRecipe } from "../../../../quake-anthology-ts/src/contracts/content.ts";
import type { ArmorState } from "../../../../quake-anthology-ts/src/contracts/gameplay.ts";
import type { SimulationOutput } from "../../../../quake-anthology-ts/src/contracts/session.ts";
import { decodeCheckpointValue } from "../../../../quake-anthology-ts/src/persistence/value.ts";
import { encodeSaveImage } from "../../../../quake-anthology-ts/src/persistence/save-image.ts";
import { UserFileStore } from "../../../../quake-anthology-ts/src/core/files/writable.ts";
import { classicGuestFiles } from "../../../../quake-anthology-ts/src/app/bootstrap/simulation/classic-guest-files.ts";

const [caseName, corpus, ...frameArgs] = process.argv.slice(2);
if (caseName === undefined || corpus === undefined) throw new Error("usage: <case> <corpus> [frames...]");
const checkpoints = (frameArgs.length === 0 ? ["1", "10", "100"] : frameArgs).map(Number);
const argvOf: Record<string, string[]> = {
  q1: ["--game", "q1-classic-id1", "--map", "e1m1", "--movement", "q1", "--character", "q1"],
  q2c: ["--game", "q2-classic-baseq2", "--map", "base1", "--movement", "q2", "--character", "q2"],
  q2r: ["--game", "q2-rerelease-baseq2", "--map", "base1", "--movement", "q2", "--character", "q2"],
  q3: ["--game", "q3-baseq3", "--map", "q3dm1", "--movement", "q3", "--character", "q3", "--mode", "deathmatch"],
  qc: ["--game", "q1-classic-id1", "--map", "e1m1", "--progs", "progs.dat", "--movement", "q1", "--character", "q1"],
  q2nc: ["--game", "q2-classic-baseq2", "--map", "base1", "--q2-game", "gamex86.dll", "--movement", "q2", "--character", "q2"],
  q2nr: ["--game", "q2-rerelease-baseq2", "--map", "base1", "--q2-game", "game_x64.dll", "--movement", "q2", "--character", "q2"],
  q3qvm: ["--game", "q3-baseq3", "--map", "q3dm1", "--movement", "q3", "--character", "q3", "--mode", "deathmatch"],
  mixed: ["--game", "q2-classic-baseq2", "--map", "base1"],
  preset: ["--preset", "q2-q1-q3"],
};
const base = argvOf[caseName];
if (base === undefined) throw new Error(`unknown case ${caseName}`);
// One user content root for both sides: its path names the user mounts the checkpoint records.
// TMPDIR is deliberately ignored: the C test uses the same repository-relative default.
const userRoot = process.env["SIM_VERIFY_USER_ROOT"] ?? join(process.cwd(), "build/agents/sim-verify2/tmp", `sim-verify-user-${caseName}`);
rmSync(userRoot, { recursive: true, force: true }); mkdirSync(userRoot, { recursive: true });
const command = parseApplicationCommand(["--content-root", corpus, "--user-content-root", userRoot, ...base, "--dedicated"]);
if (command.kind !== "run") throw new Error("expected run");
const out: string[] = [];
const console_: string[] = [];
const num = (value: number): string => String(value);
const vec = (value: { readonly x: number; readonly y: number; readonly z: number }): string => `${num(value.x)} ${num(value.y)} ${num(value.z)}`;
const armor = (value: ArmorState): string => {
  const regular = value.regular;
  const r = regular.kind === "none" ? "none" : `${regular.kind}:${num(regular.points)}:${"item" in regular ? regular.item ?? "-" : "-"}`;
  return `${r} ${value.powered.kind === "none" ? "none" : `${value.powered.kind}:${num(value.powered.cells)}`}`;
};
/* Key-order-insensitive form of a checkpoint value: separates key-order drift from value drift. */
function canonical(value: unknown): string {
  if (value === null || value === undefined) return "null";
  if (typeof value === "boolean") return value ? "true" : "false";
  if (typeof value === "number") return String(value);
  if (typeof value === "bigint") return `n${value.toString()}`;
  if (typeof value === "string") return JSON.stringify(value);
  if (value instanceof Uint8Array) return `b${Buffer.from(value).toString("hex")}`;
  if (Array.isArray(value)) return `[${value.map(canonical).join(",")}]`;
  const record = value as Record<string, unknown>;
  return `{${Object.keys(record).sort().map(key => `${JSON.stringify(key)}:${canonical(record[key])}`).join(",")}}`;
}
const canon = (bytes: Uint8Array): string => createHash("sha256").update(canonical(decodeCheckpointValue(bytes))).digest("hex").slice(0, 16);
const sha = (bytes: Uint8Array): string => createHash("sha256").update(bytes).digest("hex").slice(0, 16);

/* The Q3 QVM guest runs on the same baseq3 launch with the recipe's
 * execution replaced by the shipped qagame.qvm (q3-baseq3 is a built-in
 * product, whose launch selects the TypeScript source). */
let content = await loadApplicationContent(command.options);
if (caseName === "q3qvm") {
  const qagame = await content.mounts.open("vm/qagame.qvm");
  if (qagame === null) throw new Error("vm/qagame.qvm is not mounted");
  const recipe: ExecutableRecipe = { ...content.recipe, execution: [{ kind: "qvm", owner: content.recipe.map.entities, role: "server-game",
    artifact: qagame.reference, api: { kind: "q3-qagame", version: 8 } }] };
  await content.close();
  content = await loadApplicationContent(command.options, recipe);
}
const options = command.options;
const identity = createIdentityOwner(`sim-verify-${caseName}`);
let clockMilliseconds = 0;
const guest: Record<string, unknown> = {};
if (content.preparedQ2Game !== null) {
  const product = content.catalog.product(content.recipe.map.entities.content);
  const directory = join(userRoot, product.expectation.contentDirectory);
  const writable = classicGuestFiles(directory), installed = product.looseRoot === null ? null : classicGuestFiles(product.looseRoot);
  const callbacks = {
    capabilities: { nowMilliseconds: () => clockMilliseconds, performanceCounter: () => BigInt(clockMilliseconds * 1000), performanceFrequency: 1_000_000n,
      openFile: (path: string, mode: { readonly write: boolean }) => writable(path, mode as never) ?? (mode.write ? null : installed?.(path, mode as never) ?? null),
      standardOutput: (_stream: number, bytes: Uint8Array) => { console_.push(new TextDecoder().decode(bytes)); } },
    print: (text: string) => { console_.push(text); }, addCommand: (text: string) => { console_.push(`[command] ${text}`); return undefined; },
    debugGraph: () => undefined,
  };
  const prepared = content.preparedQ2Game;
  if (prepared.edition === "classic") guest["q2Guest"] = { ...callbacks, edition: "classic", prepared };
  else guest["q2Guest"] = { ...callbacks, edition: "rerelease", prepared, localize: (text: string) => text, clipboard: { kind: "dedicated" } };
}
if (content.preparedQ3Game !== null) {
  const product = content.catalog.product(content.recipe.map.entities.content);
  guest["q3Guest"] = { prepared: content.preparedQ3Game, gameType: 0,
    writable: new UserFileStore(join(userRoot, product.expectation.contentDirectory)), gameDirectory: "baseq3",
    print: (text: string) => { console_.push(text); },
    common: { milliseconds: () => clockMilliseconds, realTime: () => 1704067200, commands: { executeNow: (text: string) => { console_.push(`[now] ${text}`); }, append: (text: string) => { console_.push(`[append] ${text}`); }, insert: (text: string) => { console_.push(`[insert] ${text}`); } } } };
}
// Application.open loads through loadSimulation (budgeted guest calls are pumped across frames).
const simulation: SharedSimulation = await loadSimulation({ dedicated: true, ...guest,
  ...(content.preparedQuakeC === null ? {} : { preparedQuakeC: content.preparedQuakeC }),
  identity, weaponBehaviors: content.preparedWeaponBehaviors, recipe: content.recipe, world: content.world, mounts: content.mounts,
  preparedMods: content.preparedMods, enabledMods: content.recipe.mods?.map(mod => mod.selection) ?? [],
  skill: options.skill, mode: options.mode, seed: options.seed, maxClients: 1,
  playerColors: { top: 0, bottom: 0 }, promptSupported: () => false,
  playerIdentity: () => ({ seat: 0, socialId: "" }) } as Parameters<typeof loadSimulation>[0], async () => undefined);

function command_(state: MovementState, frame: number, timeSeconds: number): UserCommand {
  const forward = frame % 40 < 30 ? 200 : -100, side = frame % 16 < 8 ? 50 : -50, yaw = (frame * 7) % 360;
  const buttons = frame % 25 === 24 ? 1 : 0, up = frame % 33 === 32 ? 200 : 0;
  switch (state.kind) {
    case "q1-netquake": return { kind: state.kind, acknowledgedServerTimeSeconds: timeSeconds, viewAngles: { x: 0, y: yaw, z: 0 }, forwardMove: forward, sideMove: side, upMove: up, buttons, impulse: 0 };
    case "q1-quakeworld": return { kind: state.kind, milliseconds: 100, angles: { x: 0, y: yaw, z: 0 }, forwardMove: forward, sideMove: side, upMove: up, buttons, impulse: 0 };
    case "q2-classic": return { kind: state.kind, milliseconds: 100, angleShorts: [0, Math.trunc(yaw * 65536 / 360) & 65535, 0], forwardMove: forward, sideMove: side, upMove: up, buttons, impulse: 0, lightLevel: 0 };
    case "q2-rerelease": return { kind: state.kind, milliseconds: 100, angles: { x: 0, y: yaw, z: 0 }, forwardMove: forward, sideMove: side, buttons, serverFrame: frame };
    // Key order of the engine's own Q3 command literal (movement/q3/prediction.ts): checkpoints record it as given.
    case "q3": return { kind: state.kind, serverTimeMilliseconds: state.commandTimeMilliseconds + 100, angleWords: [0, Math.trunc(yaw * 65536 / 360) & 65535, 0], buttons, weapon: 2, forwardMove: forward > 0 ? 127 : -127, rightMove: side > 0 ? 64 : -64, upMove: up > 0 ? 127 : 0 };
  }
}

const eventCounts = new Map<string, number>(), presentationCounts = new Map<string, number>();
function drainPresentation(): void {
  for (const event of simulation.drainPresentationEvents()) presentationCounts.set(event.kind, (presentationCounts.get(event.kind) ?? 0) + 1);
}
function record(output: SimulationOutput): void {
  for (const event of output.events) {
    const payload = event.payload;
    const key = payload.kind === "message" ? `message:${(payload.event as { kind?: string }).kind ?? "?"}` : payload.kind === "sound" ? `sound:${payload.resource}` : payload.kind;
    eventCounts.set(key, (eventCounts.get(key) ?? 0) + 1);
    if (payload.kind === "message") { const text = (payload.event as { text?: unknown }).text; if (typeof text === "string") console_.push(`[message] ${text}`); }
  }
}

function dump(frame: number, output: SimulationOutput, player: ActorId | null): void {
  out.push(`== frame ${frame}`);
  out.push(`source ${(simulation as unknown as { source: { kind: string } }).source.kind}`);
  out.push(`time ${num(simulation.timeSeconds)}`);
  const snapshot = output.snapshot;
  out.push(`frame-context ${num(snapshot.frame.frame)} ${snapshot.frame.time.kind}:${num(snapshot.frame.time.value)} ${snapshot.frame.elapsed.kind}:${num(snapshot.frame.elapsed.value)} ${snapshot.frame.phase}`);
  const kinds = new Map<string, number>();
  for (const actor of snapshot.actors) { const key = `${actor.owner} ${actor.definition}`; kinds.set(key, (kinds.get(key) ?? 0) + 1); }
  out.push(`actors ${snapshot.actors.length}`);
  for (const key of [...kinds.keys()].sort()) out.push(`actor-kind ${key} ${kinds.get(key)}`);
  const bodies = new Map(snapshot.bodies.map(body => [body.actor.slot, body]));
  const lines: string[] = [];
  for (const actor of snapshot.actors) {
    const body = bodies.get(actor.id.slot);
    lines.push(`actor ${actor.definition} ${body === undefined ? "-" : vec(body.body.origin)}`);
  }
  for (const line of lines.sort()) out.push(line);
  out.push(`bodies ${snapshot.bodies.length}`);
  for (const body of [...snapshot.bodies].sort((a, b) => a.actor.slot - b.actor.slot))
    out.push(`body ${body.actor.slot}:${body.actor.generation} o ${vec(body.body.origin)} v ${vec(body.body.velocity)} a ${vec(body.body.angles)} g ${body.body.ground === null ? "-" : body.body.ground.slot}`);
  for (const inventory of [...snapshot.inventories].sort((a, b) => a.actor.slot - b.actor.slot))
    out.push(`inventory ${inventory.actor.slot} ${inventory.entries.map(entry => `${entry.item}=${num(entry.count)}/${num(entry.capacity)}`).join(" ")}`);
  for (const actor of snapshot.actors) {
    const state = simulation.combat.read(actor.id);
    if (state !== null) out.push(`combat ${actor.id.slot} ${num(state.health)} ${armor(state.armor)} ${state.team ?? "-"}`);
  }
  if (player !== null) {
    const body = simulation.bodies.read(player);
    out.push(`player body ${body === null ? "-" : `o ${vec(body.origin)} v ${vec(body.velocity)} a ${vec(body.angles)} min ${vec(body.bounds.min)} max ${vec(body.bounds.max)}`}`);
    try {
      const view = simulation.playerView(player);
      out.push(`player view o ${vec(view.origin)} a ${vec(view.angles)} h ${num(view.viewHeight)}`);
    } catch (error) { out.push(`player view error ${error instanceof Error ? error.message : String(error)}`); }
    const combat = simulation.combat.read(player);
    out.push(`player combat ${combat === null ? "-" : `${num(combat.health)} ${armor(combat.armor)}`}`);
    out.push(`player inventory ${simulation.inventory.entries(player).map(entry => `${entry.item}=${num(entry.count)}`).join(" ")}`);
  }
  for (const key of [...eventCounts.keys()].sort()) out.push(`event ${key} ${eventCounts.get(key)}`);
  for (const key of [...presentationCounts.keys()].sort()) out.push(`presentation ${key} ${presentationCounts.get(key)}`);
  for (const line of console_) out.push(`console ${JSON.stringify(line)}`);
  console_.length = 0;
  let image: ReturnType<SharedSimulation["checkpoint"]>;
  try { image = simulation.checkpoint(); } catch (error) { out.push(`checkpoint-error ${error instanceof Error ? error.message : String(error)}`); return; }
  const dumpDirectory = process.env["SIM_VERIFY_DUMP_DIR"];
  image.providers.forEach((provider, index) => {
    out.push(`checkpoint ${provider.provider} ${provider.schema} ${provider.version} ${sha(provider.bytes)} canonical ${canon(provider.bytes)}`);
    if (dumpDirectory !== undefined && dumpDirectory !== "") writeFileSync(join(dumpDirectory, `${frame}-${index}-${provider.schema.replace(/[^a-z0-9-]/gi, "_")}.json`), provider.bytes);
  });
  const encoded = encodeSaveImage(image);
  if (dumpDirectory !== undefined && dumpDirectory !== "") writeFileSync(join(dumpDirectory, `${frame}-image.bin`), encoded);
  const headerLength = new DataView(encoded.buffer, encoded.byteOffset).getUint32(encoded.indexOf(123) - 4, true), start = encoded.indexOf(123);
  out.push(`checkpoint-image ${sha(encoded)} canonical ${canon(encoded.subarray(start, start + headerLength))} ${canon(encoded.subarray(start + headerLength))}`);
}

const qvm = simulation.q3Guest();
try {
  const client = identity.client(0, 0);
  if (qvm !== null) {
    // The Q3 guest admits through its awaited native lifecycle (Q3ApplicationServerHost connect/begin/think); no bot library is attached.
    qvm.state.cvars.set("bot_enable", "0", true);
    await qvm.initialize({ configstring: (index, value) => { console_.push(`[configstring] ${index} ${value}`); },
      sendServerCommand: (slot, text) => { console_.push(`[server-command] ${slot} ${text}`); }, dropClient: async (slot, reason) => { console_.push(`[drop] ${slot} ${reason}`); } });
    const wire = (frame: number) => {
      const forward = frame % 40 < 30 ? 127 : -127, side = frame % 16 < 8 ? 64 : -64, yaw = (frame * 7) % 360;
      return { serverTime: clockMilliseconds, angles: [0, Math.trunc(yaw * 65536 / 360) & 65535, 0] as const, forwardmove: forward, rightmove: side,
        upmove: frame % 33 === 32 ? 127 : 0, buttons: frame % 25 === 24 ? 1 : 0, weapon: 2 };
    };
    const admission = await qvm.connect(client, "\\name\\Player 1\\model\\sarge/default\\rate\\25000\\snaps\\20\\ip\\localhost");
    if (admission.kind !== "accepted") throw new Error(admission.reason);
    const qvmPlayer = admission.player;
    await qvm.begin(qvmPlayer, wire(0));
    out.push(`admitted ${qvmPlayer.actor.slot}:${qvmPlayer.actor.generation} entity ${qvmPlayer.sourceEntity}`);
    let output = simulation.currentOutput();
    record(output);
    if (checkpoints.includes(0)) dump(0, output, qvmPlayer.actor);
    for (let frame = 1; frame <= Math.max(...checkpoints); frame++) {
      clockMilliseconds += 100;
      await qvm.think(qvmPlayer, wire(frame));
      output = await simulation.stepAsync({ elapsedMilliseconds: 100, commands: [] });
      record(output);
      drainPresentation();
      if (checkpoints.includes(frame)) dump(frame, output, qvmPlayer.actor);
    }
  } else {
  const admission = simulation.admitPlayer(client);
  const actor = admission.actor;
  out.push(`admitted ${actor.slot}:${actor.generation} view-height ${num(admission.viewHeight)}`);
  const player = simulation.movementPlayer(actor);
  let output = simulation.currentOutput();
  record(output);
  out.push(`players ${simulation.players().length}`);
  if (checkpoints.includes(0)) dump(0, output, actor);
  const last = Math.max(...checkpoints);
  for (let frame = 1; frame <= last; frame++) {
    clockMilliseconds += 100;
    // The command dialect follows the source's own client movement (a native guest's player state), as observeClientCommand reads it.
    const state = (simulation as unknown as { strategy: { commandMovement(client: unknown): MovementState | undefined } }).strategy.commandMovement(client) ?? player?.readState();
    // Native guests take their remote clients through the wire endpoint; the verification seat is local.
    const source = caseName === "q2nc" || caseName === "q2nr" ? { kind: "local-seat" as const, seat: identity.seat(0), client } : { kind: "remote-client" as const, client };
    const commands = state === undefined ? [] : [{ actor, source, sequence: frame, command: command_(state, frame, simulation.timeSeconds) }];
    output = simulation.step({ elapsedMilliseconds: 100, commands });
    record(output);
    drainPresentation();
    if (process.env["SIM_VERIFY_DRAWS"] === "1") out.push(`draws ${frame} ${(simulation as unknown as { random: { draws: number } }).random.draws}`);
    if (checkpoints.includes(frame)) dump(frame, output, actor);
  }
  }
} finally {
  if (qvm !== null) await qvm.shutdown();
  simulation.close();
  await content.close();
}
process.stdout.write(out.join("\n") + "\n");
