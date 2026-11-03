/* Oracle for tests/app/test_guest_bots_verify.c: the retail baseq3 qagame.qvm on
 * q3dm1 with the engine botlib attached the way Application.prepareGuestBots
 * attaches it (loadApplicationBotAssets, createApplicationBotNavigation,
 * Q3ServerGame.attachBots), bot_enable 1, one human player, then
 * "addbot sarge 1" and a deterministic run. Prints the console, the bot slot,
 * per-frame bot command times (the bot think count) and the bot body at the
 * checkpoint frames. snapshotEntities is the empty list on both sides (no
 * session authority), clients come from the identity owner.
 * Run from the TS checkout:
 *   cd ../quake-anthology-ts && bun ../quake-anthology-c/tests/app/oracles/guest_bots_verify.ts <corpus> [frames...]
 * GPL-2.0-or-later. */
import { mkdirSync, rmSync } from "node:fs";
import { join } from "node:path";
import { tmpdir } from "node:os";
import { loadApplicationContent } from "../../../../quake-anthology-ts/src/app/bootstrap/content.ts";
import { parseApplicationCommand } from "../../../../quake-anthology-ts/src/app/bootstrap/options.ts";
import { loadSimulation } from "../../../../quake-anthology-ts/src/app/bootstrap/simulation/index.ts";
import type { SharedSimulation } from "../../../../quake-anthology-ts/src/app/bootstrap/simulation/runtime.ts";
import { loadApplicationBotAssets } from "../../../../quake-anthology-ts/src/app/bootstrap/simulation/bot-assets.ts";
import { createApplicationBotNavigation } from "../../../../quake-anthology-ts/src/app/bootstrap/simulation/navigation.ts";
import { openApplicationBotLog, resumeApplicationBotLog } from "../../../../quake-anthology-ts/src/app/bootstrap/simulation/bots.ts";
import { createIdentityOwner } from "../../../../quake-anthology-ts/src/contracts/identity.ts";
import type { ExecutableRecipe } from "../../../../quake-anthology-ts/src/contracts/content.ts";
import { UserFileStore } from "../../../../quake-anthology-ts/src/core/files/writable.ts";

const [corpus, ...frameArgs] = process.argv.slice(2);
if (corpus === undefined) throw new Error("usage: <corpus> [frames...]");
const checkpoints = (frameArgs.length === 0 ? ["1", "10", "50"] : frameArgs).map(Number);
const userRoot = process.env["GUEST_BOTS_USER_ROOT"] ?? join(process.env["TMPDIR"] ?? tmpdir(), "guest-bots-user");
rmSync(userRoot, { recursive: true, force: true });
mkdirSync(userRoot, { recursive: true });
const command = parseApplicationCommand(["--content-root", corpus, "--user-content-root", userRoot,
  "--game", "q3-baseq3", "--map", "q3dm1", "--movement", "q3", "--character", "q3", "--mode", "deathmatch", "--dedicated"]);
if (command.kind !== "run") throw new Error("expected run");
const out: string[] = [];
const console_: string[] = [];
const num = (value: number): string => String(value);
const vec = (value: { readonly x: number; readonly y: number; readonly z: number }): string => `${num(value.x)} ${num(value.y)} ${num(value.z)}`;

let content = await loadApplicationContent(command.options);
const qagame = await content.mounts.open("vm/qagame.qvm");
if (qagame === null) throw new Error("vm/qagame.qvm is not mounted");
const recipe: ExecutableRecipe = { ...content.recipe, execution: [{ kind: "qvm", owner: content.recipe.map.entities, role: "server-game",
  artifact: qagame.reference, api: { kind: "q3-qagame", version: 8 } }] };
await content.close();
content = await loadApplicationContent(command.options, recipe);
const options = command.options;
const identity = createIdentityOwner("guest-bots-verify");
let clockMilliseconds = 0;
const product = content.catalog.product(content.recipe.map.entities.content);
const q3Guest = { prepared: content.preparedQ3Game, gameType: 0,
  writable: new UserFileStore(join(userRoot, product.expectation.contentDirectory)), gameDirectory: "baseq3",
  print: (text: string) => { console_.push(text); },
  common: { milliseconds: () => clockMilliseconds, realTime: () => 1704067200, commands: { executeNow: (text: string) => { console_.push(`[now] ${text}`); }, append: (text: string) => { console_.push(`[append] ${text}`); }, insert: (text: string) => { console_.push(`[insert] ${text}`); } } } };
const simulation: SharedSimulation = await loadSimulation({ dedicated: true, q3Guest,
  identity, weaponBehaviors: content.preparedWeaponBehaviors, recipe: content.recipe, world: content.world, mounts: content.mounts,
  preparedMods: content.preparedMods, enabledMods: content.recipe.mods?.map(mod => mod.selection) ?? [],
  skill: options.skill, mode: options.mode, seed: options.seed, maxClients: 4,
  playerColors: { top: 0, bottom: 0 }, promptSupported: () => false,
  playerIdentity: () => ({ seat: 0, socialId: "" }) } as Parameters<typeof loadSimulation>[0], async () => undefined);

const guest = simulation.q3Guest();
if (guest === null) throw new Error("expected the Q3 QVM guest");
// Application.prepareGuestBots, with the identity owner standing in for the session's clients.
const assets = await loadApplicationBotAssets(content, simulation, true);
const selected = await createApplicationBotNavigation({ content, simulation });
guest.attachBots({
  selected,
  library: {
    files: assets.files,
    random: { nextInt: () => simulation.random.nextInteger() },
    debug: false,
    milliseconds: () => Math.trunc(simulation.timeSeconds * 1000),
    print: (_severity, text) => { console_.push(text); return undefined; },
    openLog: openApplicationBotLog,
    resumeLog: resumeApplicationBotLog,
  },
  createClient: slot => identity.client(slot, 0),
  freeClient: () => {},
  snapshotEntities: () => [],
});
try {
  guest.state.cvars.set("bot_enable", "1", true);
  await guest.initialize({ configstring: (index, value) => { console_.push(`[configstring] ${index} ${value}`); },
    sendServerCommand: (slot, text) => { console_.push(`[server-command] ${slot} ${text}`); }, dropClient: async (slot, reason) => { console_.push(`[drop] ${slot} ${reason}`); } });
  const wire = (frame: number) => {
    const forward = frame % 40 < 30 ? 127 : -127, side = frame % 16 < 8 ? 64 : -64, yaw = (frame * 7) % 360;
    return { serverTime: clockMilliseconds, angles: [0, Math.trunc(yaw * 65536 / 360) & 65535, 0] as const, forwardmove: forward, rightmove: side,
      upmove: frame % 33 === 32 ? 127 : 0, buttons: frame % 25 === 24 ? 1 : 0, weapon: 2 };
  };
  const admission = await guest.connect(identity.client(0, 0), "\\name\\Player 1\\model\\sarge/default\\rate\\25000\\snaps\\20\\ip\\localhost");
  if (admission.kind !== "accepted") throw new Error(admission.reason);
  const player = admission.player;
  await guest.begin(player, wire(0));
  out.push(`admitted ${player.actor.slot}:${player.actor.generation} entity ${player.sourceEntity}`);
  out.push(`addbot ${await guest.consoleCommand(["addbot", "sarge", "1"])}`);
  const bots = guest.players().filter(entry => guest.isBot(entry.client));
  for (const bot of bots) out.push(`bot ${bot.client.slot} actor ${bot.actor.slot}:${bot.actor.generation} entity ${bot.sourceEntity}`);
  const bot = bots[0] ?? null;
  let thinks = 0, lastCommandTime = bot === null ? 0 : guest.records.player(bot.sourceEntity).commandTimeMilliseconds;
  for (let frame = 1; frame <= Math.max(...checkpoints); frame++) {
    clockMilliseconds += 100;
    await guest.think(player, wire(frame));
    await simulation.stepAsync({ elapsedMilliseconds: 100, commands: [] });
    if (bot !== null) {
      const state = guest.records.player(bot.sourceEntity);
      if (state.commandTimeMilliseconds !== lastCommandTime) thinks++;
      lastCommandTime = state.commandTimeMilliseconds;
      if (checkpoints.includes(frame)) {
        out.push(`== frame ${frame}`);
        out.push(`bot ct ${num(state.commandTimeMilliseconds)} o ${vec(state.origin)} v ${vec(state.velocity)} thinks ${thinks}`);
        const body = simulation.bodies.read(bot.actor);
        out.push(`bot body ${body === null ? "-" : `o ${vec(body.origin)} a ${vec(body.angles)}`}`);
        const human = guest.records.player(player.sourceEntity);
        out.push(`player ct ${num(human.commandTimeMilliseconds)} o ${vec(human.origin)}`);
      }
    }
  }
  out.push(`thinks ${thinks}`);
  for (const line of console_) out.push(`console ${JSON.stringify(line)}`);
} finally {
  await guest.shutdown();
  simulation.close();
  await content.close();
}
process.stdout.write(out.join("\n") + "\n");
