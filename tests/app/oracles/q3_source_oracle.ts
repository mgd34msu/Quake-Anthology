/* Oracle for tests/app/test_q3_source.c: constructs the TS Q3SourceRuntime over
 * retail maps (baseq3 q3dm1, missionpack mpteam1) through createQ3SourceHost
 * and dumps its constructed state. Run from the TS checkout:
 *   cd ../quake-anthology-ts && bun ../quake-anthology-c/tests/app/oracles/q3_source_oracle.ts <pk3> <map> <product> <gametype>
 * GPL-2.0-or-later. */
import { SharedOriginalPickupAdmission } from "../../../../quake-anthology-ts/src/world/gameplay/original-pickups.ts";
import type { ExecutableRecipe, ProviderReference, ResolvedResourceReference } from "../../../../quake-anthology-ts/src/contracts/content.ts";
import { createContentDigest, createResourceId } from "../../../../quake-anthology-ts/src/contracts/content.ts";
import { createIdentityOwner } from "../../../../quake-anthology-ts/src/contracts/identity.ts";
import type { ClockProfile } from "../../../../quake-anthology-ts/src/contracts/time.ts";
import { Q3_BINARY32_PROFILE } from "../../../../quake-anthology-ts/src/core/numeric.ts";
import { SessionActorRegistry, ActorCallbackTable } from "../../../../quake-anthology-ts/src/world/actors/index.ts";
import { SharedInventoryTable, GameplayAuthority } from "../../../../quake-anthology-ts/src/world/gameplay/index.ts";
import { SharedSceneQueries } from "../../../../quake-anthology-ts/src/world/collision/index.ts";
import { SharedPhysics } from "../../../../quake-anthology-ts/src/app/bootstrap/simulation/physics.ts";
import { Q3SourceRuntime, createQ3SourceHost } from "../../../../quake-anthology-ts/src/app/bootstrap/simulation/q3/index.ts";
import type { Q3SourceEvent } from "../../../../quake-anthology-ts/src/app/bootstrap/simulation/q3/index.ts";
import { Q3DeathAnimationSequence } from "../../../../quake-anthology-ts/src/content/q3/foundation/character.ts";
import { q3GameCvarDefinitions } from "../../../../quake-anthology-ts/src/content/q3/base/settings.ts";
import { parseQ3Bsp, adaptQ3Bsp } from "../../../../quake-anthology-ts/src/formats/q3-map/index.ts";
import { openArchive } from "../../../../quake-anthology-ts/src/content/archive/index.ts";

const [pk3, mapName, product, gameTypeText] = process.argv.slice(2);
if (pk3 === undefined || mapName === undefined || (product !== "baseq3" && product !== "missionpack") || gameTypeText === undefined) throw new Error("usage: <pk3> <map> <product> <gametype>");
const clock: ClockProfile = { kind: "q3", serverFrameMilliseconds: 100, fixedMovementMilliseconds: null, maximumCommandMilliseconds: 200 };
function sourceRecipe(byteLength: number): ExecutableRecipe {
  const content = "q3:classic:baseq3:retail";
  const provider = (role: string): ProviderReference => ({ provider: `q3:${role}`, content });
  const raw: Omit<ResolvedResourceReference, "id"> = { requestedPath: `maps/${mapName}.bsp`, byteLength,
    provenance: { kind: "loose", memberPath: `maps/${mapName}.bsp`, mount: { kind: "loose", identity: { id: "mount:q3:oracle", content, generation: 0 }, rootPath: "/oracle" } },
    digest: createContentDigest("0".repeat(64)), resolution: { kind: "default-order", plan: "mount-plan:q3:oracle", rank: 0 } };
  const geometry = { ...raw, id: createResourceId(raw) };
  return { schemaVersion: 3, id: "recipe:q3:oracle", preset: "recipe:q3:oracle", map: { geometryContent: content, geometry, entities: provider("game") },
    campaign: { kind: "campaign", mission: provider("mission"), gamecode: provider("game") }, movement: provider("movement"),
    character: { definition: provider("character"), appearance: provider("appearance") }, weapons: [provider("weapons")], equipment: { grapple: { kind: "disabled" }, handGrenades: { kind: "disabled" } }, enemies: { kind: "map-defined" },
    presentation: { doppler: { kind: "source" }, environment: { kind: "audio-content" }, assets: content, hud: provider("hud"), effects: provider("effects"), audio: provider("audio") }, engineBehavior: provider("engine"), combat: provider("combat"),
    inventory: provider("inventory"), match: provider("match"), transition: provider("transition"), execution: [],
    mounts: { id: "mount-plan:q3:oracle", mounts: [raw.provenance.mount], defaultOrder: [raw.provenance.mount.identity.id], prefixOrders: [] }, resources: [geometry],
    timing: ["game", "character", "movement", "combat", "weapons", "inventory"].map(role => ({ provider: provider(role).provider, clock, numeric: Q3_BINARY32_PROFILE })),
    ordering: { kind: "native", traversal: "source-slot-order", clock } };
}
const archive = await openArchive(pk3, "pk3");
const entry = archive.findEntries(`maps/${mapName}.bsp`)[0];
if (entry === undefined) throw new Error("map missing");
const bytes = await archive.readEntry(entry); archive.close();
const map = adaptQ3Bsp(parseQ3Bsp(bytes)), recipe = sourceRecipe(bytes.length), identities = createIdentityOwner("q3-source-oracle");
const actors = new SessionActorRegistry(identities), callbacks = new ActorCallbackTable(actors), scene = new SharedSceneQueries(map);
const physics = new SharedPhysics({ actors, callbacks, scene, numeric: Q3_BINARY32_PROFILE,
  sourceOrder: (left, right) => (actors.sourceOf(left)?.slot ?? left.slot) - (actors.sourceOf(right)?.slot ?? right.slot),
  worldActor: () => actors.atSource("q3:game", 1022)?.id ?? null, onBlocked: () => undefined });
const combat = new GameplayAuthority(actors, callbacks, { impulse: () => undefined, beforeReaction: () => undefined, confirmed: () => undefined });
const inventory = new SharedInventoryTable(actors);
const out: string[] = [];
const text = (value: string): string => JSON.stringify(value);
const events: Q3SourceEvent[] = [];
const host = createQ3SourceHost({ actors, bodies: physics.bodies, callbacks, combat, inventory, scene, originalPickups: new SharedOriginalPickupAdmission(actors, combat, inventory),
  moverActors: { observe: () => null, write: () => undefined, link: actor => physics.bodies.link(actor), release: actor => actors.release(actor) },
  bots: { kind: "unavailable", reason: "oracle" }, deathAnimations: new Q3DeathAnimationSequence(), now: () => 100,
  emit: event => { events.push(event); }, clientNumber: () => -1, schedule: () => undefined, runThink: () => undefined,
  collision: (actor, collision) => physics.setCollision(actor, collision), armorContext: () => ({ screenFacingDot: 1, arithmetic: "binary32" }), foreign: () => null, isPlayer: () => false,
  sourceCommand: () => { throw new Error("unused"); }, moveClient: () => { throw new Error("unused"); }, selectSpawnWeapon: () => undefined, spawnPlayer: () => undefined,
}, { gameType: Number(gameTypeText), singlePlayer: false, maxClients: 8, mapName });
for (const definition of q3GameCvarDefinitions(product)) host.cvars.register(definition.name, definition.value, definition.flags);
const runtime = new Q3SourceRuntime({ recipe, weaponProvider: { provider: "q3:weapons", content: recipe.map.entities.content }, product,
  entities: map.entities, seed: 42, maxClients: 8, buildDate: "TypeScript port" }, host);
const report = runtime.load();
for (const event of events) {
  if (event.kind === "print" || event.kind === "log") out.push(`${event.kind} ${text(event.text)}`);
  else if (event.kind === "configstring") out.push(`configstring ${event.index} ${text(event.value)}`);
  else out.push(`event ${event.kind}`);
}
report.outcomes.forEach((outcome, index) => out.push(`outcome ${index} ${outcome.kind} ${outcome.slot} ${outcome.kind === "filtered" ? outcome.reason : text(outcome.classname ?? "")}`));
for (const pair of report.worldVariables.entries) out.push(`world ${text(pair.key)} ${text(pair.value)}`);
for (let slot = 0; slot < runtime.pool.numEntities; slot++) {
  const entity = runtime.pool.at(slot);
  if (!entity.inuse) continue;
  const o = entity.r.currentOrigin;
  out.push(`entity ${slot} ${text(entity.classname)} ${o.x} ${o.y} ${o.z} ${entity.s.eType} ${entity.s.modelindex} ${entity.r.contents} ${entity.s.solid}`);
}
for (const name of ["g_gametype", "sv_maxclients", "timelimit", "fraglimit", "g_motd", "dmflags", "g_speed", "g_gravity", "session", "g_restarted", "mapname"])
  out.push(`cvar ${name} ${text(host.cvars.variableString(name))}`);
out.push(`numEntities ${runtime.pool.numEntities}`);
console.log(out.join("\n"));
