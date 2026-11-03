/* Oracle for tests/app/test_match_results.c: match-results.ts over a real
 * Q1 dm1 deathmatch SharedSimulation (built as tests/app/oracles/sim_verify.ts
 * builds it), the TS local-match-rankings Q1 case without the Application:
 * fraglimit 3, the local player's score 4, steps until the match ends, then
 * matchResultOf, ApplicationMatchRankings (bind, observe, settle, standings) and
 * a second ApplicationMatchRankings attached to the reopened progress store.
 * Run from the TS checkout:
 *   cd ../quake-anthology-ts && bun ../quake-anthology-c/tests/app/oracles/match_results.ts <corpus> <user-root>
 * GPL-2.0-or-later. */
import { mkdirSync, rmSync } from "node:fs";
import { join } from "node:path";
import { loadApplicationContent } from "../../../../quake-anthology-ts/src/app/bootstrap/content.ts";
import { parseApplicationCommand } from "../../../../quake-anthology-ts/src/app/bootstrap/options.ts";
import { loadSimulation } from "../../../../quake-anthology-ts/src/app/bootstrap/simulation/index.ts";
import { ApplicationMatchRankings, matchResultOf, type MatchResult } from "../../../../quake-anthology-ts/src/app/bootstrap/match-results.ts";
import { PlayerProgressStore } from "../../../../quake-anthology-ts/src/app/bootstrap/player-progress.ts";
import { createIdentityOwner } from "../../../../quake-anthology-ts/src/contracts/identity.ts";

const [corpus, userRoot] = process.argv.slice(2);
if (corpus === undefined || userRoot === undefined) throw new Error("usage: <corpus> <user-root>");
rmSync(userRoot, { recursive: true, force: true });
mkdirSync(userRoot, { recursive: true });
const command = parseApplicationCommand([
  "--content-root", corpus, "--user-content-root", userRoot,
  "--game", "q1-classic-id1", "--map", "dm1", "--movement", "q1", "--character", "q1", "--mode", "deathmatch", "--dedicated",
]);
if (command.kind !== "run") throw new Error("expected run");
const content = await loadApplicationContent(command.options);
const options = command.options;
const identity = createIdentityOwner("match-results");
const simulation = await loadSimulation({ dedicated: true,
  identity, weaponBehaviors: content.preparedWeaponBehaviors, recipe: content.recipe, world: content.world, mounts: content.mounts,
  preparedMods: content.preparedMods, enabledMods: [],
  skill: options.skill, mode: options.mode, seed: options.seed, maxClients: 1,
  playerColors: { top: 0, bottom: 0 }, promptSupported: () => false,
  playerIdentity: () => ({ seat: 0, socialId: "" }) } as Parameters<typeof loadSimulation>[0], async () => undefined);
const out: string[] = [];
const result = (label: string, value: MatchResult | null, uuid: boolean): void => {
  if (value === null) { out.push(`${label} null`); return; }
  const match = uuid ? `${/^[0-9a-f]{8}-[0-9a-f]{4}-4[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}:/.test(value.match) ? "<uuid>" : "?"}${value.match.slice(36)}` : value.match;
  out.push(`${label} ${value.family} ${value.rules} ${match} ${value.map} ${value.players.length}`);
  for (const player of value.players) out.push(`${label}-player ${player.slot} ${player.score} ${player.won} ${player.identity ?? "null"}`);
};
const client = identity.client(0, 0);
const admission = simulation.admitPlayer(client);
for (let index = 0; index < 10; index++) simulation.step({ elapsedMilliseconds: 100, commands: [] });
result("playing", matchResultOf(simulation, "probe"), false);
const internals = simulation as unknown as { source: { cvars: { set(name: string, value: string, force: boolean): unknown } };
  sourceMatch: { player(actor: unknown): { setScore(value: number): void } | null } };
internals.source.cvars.set("fraglimit", "3", true);
const player = internals.sourceMatch.player(admission.actor);
if (player === null) throw new Error("no match player");
player.setScore(4);
let frames = 0;
for (; frames < 200 && matchResultOf(simulation, "probe") === null; frames++) simulation.step({ elapsedMilliseconds: 100, commands: [] });
out.push(`ended-after ${frames}`);
result("ended", matchResultOf(simulation, "probe"), false);
const rankings = new ApplicationMatchRankings();
const progressPath = join(userRoot, "player-progress.json");
await rankings.attach(await PlayerProgressStore.open(progressPath));
await rankings.bind(0, "Host");
result("observed", rankings.observe(simulation), true);
out.push(`observe-again ${rankings.observe(simulation) === null ? "null" : "result"}`);
for (const entry of await rankings.settle()) out.push(`settled ${entry.matches} ${entry.wins} ${entry.score}`);
for (const entry of rankings.standings(simulation.recipe.match.provider)) out.push(`standing ${entry.matches} ${entry.wins} ${entry.score} ${[...entry.statistics].map(([k, v]) => `${k}=${v}`).join(",")}`);
out.push(`dropped ${rankings.droppedResults}`);
const restarted = new ApplicationMatchRankings();
await restarted.attach(await PlayerProgressStore.open(progressPath));
for (const entry of restarted.standings(simulation.recipe.match.provider)) out.push(`restarted ${entry.matches} ${entry.wins} ${entry.score}`);
const reopened = await PlayerProgressStore.open(progressPath);
for (const event of reopened.all()) out.push(`progress ${event.kind} ${event.source} ${event.participant} ${"map" in event ? event.map : "-"} ${"score" in event ? event.score : "-"} ${"won" in event ? event.won : "-"} ${"rules" in event ? event.rules : "-"}`);
await simulation.close();
await content.close();
for (const line of out) console.log(line);
