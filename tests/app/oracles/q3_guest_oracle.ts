/* Oracle for tests/app/test_q3_source.c (guest part): prepareQ3Game over the
 * retail baseq3 (or missionpack) mounts, Q3QvmServerGame over the retail qagame
 * and map, initialize() with bots disabled, then a dump of the prints, the
 * configstring output and the prepared primary profile.
 *   cd ../quake-anthology-ts && bun ../quake-anthology-c/tests/app/oracles/q3_guest_oracle.ts <pak dir> <map> <provider> [extra pak dir]
 * GPL-2.0-or-later. */
import { mkdtemp, rm, readdir } from "node:fs/promises";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { createMountIdentity } from "../../../../quake-anthology-ts/src/contracts/content.ts";
import type { ArchiveMount, ResolvedMountPlan } from "../../../../quake-anthology-ts/src/contracts/content.ts";
import { createIdentityOwner } from "../../../../quake-anthology-ts/src/contracts/identity.ts";
import { digestFile, openMountPlan } from "../../../../quake-anthology-ts/src/content/mounts/index.ts";
import { decodeQ3World } from "../../../../quake-anthology-ts/src/formats/q3-map/index.ts";
import { createSceneQueries } from "../../../../quake-anthology-ts/src/world/collision/index.ts";
import { ActorCallbackTable, SessionActorRegistry } from "../../../../quake-anthology-ts/src/world/actors/index.ts";
import { SharedPhysics } from "../../../../quake-anthology-ts/src/app/bootstrap/simulation/physics.ts";
import { Q3_BINARY32_PROFILE } from "../../../../quake-anthology-ts/src/core/numeric.ts";
import { UserFileStore } from "../../../../quake-anthology-ts/src/core/files/writable.ts";
import { Q3QvmServerGame } from "../../../../quake-anthology-ts/src/app/bootstrap/simulation/q3/guest-runtime.ts";
import { prepareQ3Game } from "../../../../quake-anthology-ts/src/app/bootstrap/simulation/q3/guest-artifact.ts";
import { Q3ServerState } from "../../../../quake-anthology-ts/src/app/bootstrap/simulation/q3/server-state.ts";

const [directory, mapName, provider, extra] = process.argv.slice(2);
if (directory === undefined || mapName === undefined || provider === undefined) throw new Error("usage: <pak dir> <map> <provider> [extra dir]");
const content = "q3:classic:oracle:installed";
const mounts: ArchiveMount[] = [];
for (const dir of [extra, directory]) {
  if (dir === undefined) continue;
  const paks = (await readdir(dir)).filter(name => name.toLowerCase().endsWith(".pk3")).sort().reverse();
  for (const pak of paks) {
    const archivePath = join(dir, pak);
    mounts.push({ kind: "archive", format: "pk3", archivePath, archiveDigest: await digestFile(archivePath), identity: createMountIdentity(`mount:oracle:${mounts.length}`, content, 1) });
  }
}
const plan: ResolvedMountPlan = { id: "mount-plan:oracle:1", mounts, defaultOrder: mounts.map(mount => mount.identity.id), prefixOrders: [] };
const mounted = await openMountPlan(plan), map = await mounted.open(`maps/${mapName}.bsp`), qvm = await mounted.open("vm/qagame.qvm");
if (map === null || qvm === null) throw new Error("missing map or qagame");
const execution = { kind: "qvm", role: "server-game", owner: { provider, content }, api: { kind: "q3-qagame", version: 8 }, artifact: qvm.reference } as const;
const prepared = await prepareQ3Game(execution as never, mounted);
const out: string[] = [];
const text = (value: string): string => JSON.stringify(value);
out.push(`artifact ${prepared.artifact.module.id} ${prepared.artifact.module.digest} ${prepared.artifact.module.revision} ${prepared.artifact.abiProfile ?? "none"} ${prepared.artifact.known?.product ?? "unknown"}`);
out.push(`weapons ${prepared.weapons.map(weapon => `${weapon.weapon}:${weapon.item}:${weapon.ammo ?? "-"}`).join(",")}`);
out.push(`items ${prepared.items === undefined ? "none" : "declared"}`);
const primary = prepared.primary;
out.push(`primary declaration=${primary.declaration?.digest ?? "null"} input=${primary.input === null ? "null" : `${primary.input.entityStride}/${primary.input.clientStride}`} weapons=${primary.weapons === null ? "null" : "set"} inventory=${primary.inventory === null ? "null" : "set"} pickups=${primary.pickups === null ? "null" : "set"} combat=${primary.combat === null ? "null" : "set"}`);
const scene = createSceneQueries(decodeQ3World(map.bytes)), identities = createIdentityOwner("guest-oracle");
const actors = new SessionActorRegistry(identities), callbacks = new ActorCallbackTable(actors);
const physics = new SharedPhysics({ actors, callbacks, scene, numeric: Q3_BINARY32_PROFILE, sourceOrder: (a, b) => a.slot - b.slot, worldActor: () => null, onBlocked: () => undefined });
const state = new Q3ServerState({ session: identities.session, settings: { gameType: 0, singlePlayer: false, maxClients: 8, mapName }, now: () => 1000, print: text_ => { out.push(`print ${text(text_)}`); } });
state.cvars.set("bot_enable", "0", true);
state.cvars.set("fs_game", provider === "q3:missionpack" ? "missionpack" : "baseq3", true);
const root = await mkdtemp(join(tmpdir(), "q3-guest-oracle-"));
const decoded = decodeQ3World(map.bytes);
const game = new Q3QvmServerGame({ artifact: prepared.artifact, state, mounts: mounted, writable: new UserFileStore(root), maxClients: 8, seed: 42, entityText: decoded.entities, now: () => 1000, assertCurrent: () => {},
  records: { actors, bodies: physics.bodies, scene, provider, collision: (actor, collision) => physics.setCollision(actor, collision) },
  common: { milliseconds: () => 1, realTime: () => 0, commands: { executeNow: () => { out.push("execute"); }, append: value => { out.push(`append ${text(value)}`); }, insert: value => { out.push(`insert ${text(value)}`); } } } });
await game.initialize({ dropClient: async () => {}, sendServerCommand: (slot, value) => { out.push(`server ${slot} ${text(value)}`); },
  configstring: (index, value) => { out.push(`configstring ${index} ${text(value)}`); } });
out.push(`numEntities ${game.game.data.numEntities}`);
for (const name of ["g_gametype", "sv_maxclients", "dedicated", "bot_enable", "vm_game", "timelimit", "fraglimit", "g_motd", "mapname"])
  out.push(`cvar ${name} ${text(state.cvars.variableString(name))}`);
await game.shutdown();
actors.close(); mounted.close(); await rm(root, { recursive: true, force: true });
console.log(out.join("\n"));
