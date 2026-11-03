/* Oracle for the retail case of tests/app/test_mod_declarations.c: a gameplay mod
 * recipe through loadApplicationContent. The user content root gets
 * q1/id1/gameplay-mods.json and a QuakeC declaration over the retail id1 progs.dat;
 * `--mod q1-classic-id1/count` selects it, applyApplicationMods puts it in the
 * recipe, prepareApplicationMods prepares it, and a SharedSimulation over e1m1 with
 * the prepared mod runs 10 frames. Prints the recipe mod as JSON.stringify, the
 * prepared identity, the enabled session mods and their checkpointed modules.
 * Run from the TS checkout:
 *   cd ../quake-anthology-ts && bun ../quake-anthology-c/tests/app/oracles/mod_retail.ts <corpus> <user-root>
 * GPL-2.0-or-later. */
import { mkdirSync, rmSync, writeFileSync } from "node:fs";
import { join } from "node:path";
import { loadApplicationContent } from "../../../../quake-anthology-ts/src/app/bootstrap/content.ts";
import { parseApplicationCommand } from "../../../../quake-anthology-ts/src/app/bootstrap/options.ts";
import { loadSimulation } from "../../../../quake-anthology-ts/src/app/bootstrap/simulation/index.ts";
import { discoverInstalledContent } from "../../../../quake-anthology-ts/src/content/catalog/index.ts";
import { openMountPlan } from "../../../../quake-anthology-ts/src/content/mounts/index.ts";
import { createIdentityOwner } from "../../../../quake-anthology-ts/src/contracts/identity.ts";

const [corpus, userRoot] = process.argv.slice(2);
if (corpus === undefined || userRoot === undefined) throw new Error("usage: <corpus> <user-root>");
rmSync(userRoot, { recursive: true, force: true });
mkdirSync(join(userRoot, "q1/id1"), { recursive: true });
const catalog = await discoverInstalledContent({ corpusRoot: corpus, userContentRoot: userRoot });
const product = catalog.require("q1-classic-id1"),
  mounts = await catalog.mountsFor(product.id);
const mounted = await openMountPlan({ id: "mount-plan:oracle:mod-retail", mounts, defaultOrder: mounts.map(mount => mount.identity.id), prefixOrders: [] });
const program = await mounted.open("progs.dat");
if (program === null) throw new Error("no progs.dat");
mounted.close();
const declaration = {
  version: 1,
  runtime: "quakec",
  program: { path: "progs.dat", digest: program.reference.digest },
  actorFields: [{ field: "health", binding: "health" }],
  callbacks: [],
  cvars: [{ name: "count_mod", value: "7" }],
};
writeFileSync(join(userRoot, "q1/id1/count-mod.json"), JSON.stringify(declaration));
writeFileSync(
  join(userRoot, "q1/id1/gameplay-mods.json"),
  JSON.stringify({ version: 1, components: [{ id: "count", title: "Count", purpose: "addition", callbacks: "count-mod.json", requires: [], conflicts: [] }] }),
);
const command = parseApplicationCommand([
  "--content-root", corpus, "--user-content-root", userRoot,
  "--game", "q1-classic-id1", "--map", "e1m1", "--mod", "q1-classic-id1/count", "--dedicated",
]);
if (command.kind !== "run") throw new Error("expected run");
const content = await loadApplicationContent(command.options);
const out: string[] = [];
out.push(`recipe-mods ${JSON.stringify(content.recipe.mods)}`);
out.push(`prepared ${content.preparedMods.length}`);
for (const mod of content.preparedMods) {
  const identity = mod.identity;
  out.push(`identity ${identity.selection.product}/${identity.selection.id} ${identity.source.provider} ${identity.source.content} ${identity.declarationDigest}`);
  for (const module of identity.modules) out.push(`module ${module.id} ${module.artifactPath} ${module.digest} ${module.revision}`);
  out.push(`description ${mod.description.title} ${mod.description.purpose} ${mod.description.requires.length} ${mod.description.conflicts.length}`);
}
const options = command.options;
const identity = createIdentityOwner("mod-retail");
const simulation = await loadSimulation({ dedicated: true,
  identity, weaponBehaviors: content.preparedWeaponBehaviors, recipe: content.recipe, world: content.world, mounts: content.mounts,
  preparedMods: content.preparedMods, enabledMods: content.recipe.mods?.map(mod => mod.selection) ?? [],
  skill: options.skill, mode: options.mode, seed: options.seed, maxClients: 1,
  playerColors: { top: 0, bottom: 0 }, promptSupported: () => false,
  playerIdentity: () => ({ seat: 0, socialId: "" }) } as Parameters<typeof loadSimulation>[0], async () => undefined);
simulation.admitPlayer(identity.client(0, 0));
for (let index = 0; index < 10; index++) simulation.step({ elapsedMilliseconds: 100, commands: [] });
const owner = (simulation as unknown as {
  modOwner: {
    enabled(): readonly { product: string; id: string }[];
    checkpoint(): Promise<{ readonly mods: readonly { identity: { modules: readonly { id: string }[] } }[] }>;
  } | null;
}).modOwner;
out.push(`mods-enabled ${owner === null ? "none" : owner.enabled().map(selection => `${selection.product}/${selection.id}`).join(",")}`);
for (const mod of owner === null ? [] : (await owner.checkpoint()).mods) out.push(`checkpoint-module ${mod.identity.modules.map(module => module.id).join(",")}`);
await simulation.close();
await content.close();
for (const line of out) console.log(line);
