// Oracle for tests/app/test_app_concrete_original_save.c: prepareApplicationSave (original-save.ts) on retail id1 e1m1.
// A QuakeC world is stepped 30 frames with no input, captured as an original v5 save and written under
// <tmp>/q1-classic-id1/manual.sav (the directory names the product), then prepared through the real catalog, content
// loader and SharedSimulation. Also the error paths and a shared save. Run from the TS checkout:
//   cd ../quake-anthology-ts && QTS_TEST_CORPUS=~/qfiles bun ../quake-anthology-c/tests/app/oracles/app_concrete_original_save.ts
import { createHash } from "node:crypto";
import { mkdirSync, mkdtempSync, rmSync, writeFileSync } from "node:fs";
import { homedir, tmpdir } from "node:os";
import { join } from "node:path";
import { loadApplicationContent } from "../../../../quake-anthology-ts/src/app/bootstrap/content.ts";
import { parseApplicationCommand } from "../../../../quake-anthology-ts/src/app/bootstrap/options.ts";
import type { ApplicationOptions } from "../../../../quake-anthology-ts/src/app/bootstrap/options.ts";
import { prepareApplicationSave } from "../../../../quake-anthology-ts/src/app/bootstrap/original-save.ts";
import { createSimulation } from "../../../../quake-anthology-ts/src/app/bootstrap/simulation/index.ts";
import { createIdentityOwner } from "../../../../quake-anthology-ts/src/contracts/identity.ts";
import type { SaveImage } from "../../../../quake-anthology-ts/src/contracts/session.ts";
import { encodeQ1Save } from "../../../../quake-anthology-ts/src/persistence/q1.ts";
import { encodeSaveImage } from "../../../../quake-anthology-ts/src/persistence/save-image.ts";

const corpus = process.env["QTS_TEST_CORPUS"] ?? join(homedir(), "qfiles");
const tmp = mkdtempSync(join(process.env["TMPDIR"] ?? tmpdir(), "app-concrete-original-save-"));
const out = (line: string): void => { console.log(line); };
const sha = (bytes: Uint8Array): string => createHash("sha256").update(bytes).digest("hex").slice(0, 16);
const user = join(tmp, "user");
mkdirSync(user, { recursive: true });
const parse = (game: string[]): ApplicationOptions => {
  const command = parseApplicationCommand(["--content-root", corpus, "--user-content-root", user, ...game, "--dedicated", "--skill", "2"]);
  if (command.kind !== "run") throw new Error("expected run");
  return command.options;
};
const options = parse(["--game", "q1-classic-id1", "--map", "e1m1", "--progs", "progs.dat", "--movement", "q1", "--character", "q1"]);
const content = await loadApplicationContent(options);
const identity = createIdentityOwner("original-save-oracle");
const simulation = createSimulation({
  identity, recipe: content.recipe, world: content.world, mounts: content.mounts,
  ...(content.preparedQuakeC === null ? {} : { preparedQuakeC: content.preparedQuakeC }),
  dedicated: true, skill: options.skill, mode: options.mode, seed: options.seed, maxClients: 1,
});
simulation.admitPlayer(identity.client(0, 0));
for (let frame = 0; frame < 30; frame++) simulation.step({ elapsedMilliseconds: 100, commands: [] });
const data = simulation.captureOriginalSave({ version: 5 }, "manual");
const bytes = encodeQ1Save(data);
out(`capture time ${data.time} skill ${data.skill} map ${data.map} sha ${sha(bytes)}`);
const directory = join(tmp, "q1-classic-id1");
mkdirSync(directory);
const path = join(directory, "manual.sav");
writeFileSync(path, bytes);
const ambiguous = join(tmp, "saves");
mkdirSync(ambiguous);
writeFileSync(join(ambiguous, "manual.sav"), bytes);
// A shared save of the same world (createSimulation checkpoint).
const shared = encodeSaveImage(simulation.checkpoint());
writeFileSync(join(tmp, "shared.sav"), shared);
simulation.close();
await content.close();

const showOptions = (o: ApplicationOptions): void => {
  out(`options product ${o.product} map ${o.map} skill ${o.skill} mode ${o.mode} movement ${o.movement} character ${o.character}`);
  out(`options model ${o.characterModel} seats ${o.seats} rules ${o.rules ?? "-"} progs ${o.quakeCProgram ?? "-"} network ${o.network.kind}`);
  out(`options mapProduct ${o.mapProduct ?? "-"} movementProduct ${o.movementProduct ?? "-"} q2 ${o.q2GameLibrary ?? "-"} startup ${o.startupCommands?.length ?? "-"} authored ${o.authoredCampaignStart ?? "-"} explicitRules ${o.explicitRules ?? "-"}`);
};
const showImage = (image: SaveImage): void => {
  out(`image entities ${image.recipe.map.entities.content} ${image.recipe.map.entities.provider} map ${image.recipe.map.geometry.requestedPath}`);
  out(`image time ${image.frame.time.kind} ${image.frame.time.value} actors ${image.actors.length} bodies ${image.bodies.length} guests ${image.guests.length}`);
  out(`image providers ${image.providers.map(p => `${p.schema}@${p.version}`).join(",")}`);
};
const attempt = async (label: string, path: string, source?: string, input = options): Promise<void> => {
  try {
    const prepared = await prepareApplicationSave(input, content.catalog, path, source);
    out(`${label} ok`);
    showOptions(prepared.options);
    showImage(prepared.image);
  } catch (error) {
    out(`${label} error ${(error as Error).message.split(tmp).join("<tmp>")}`);
  }
};
// The launch options carry fields the original import drops.
const launch: ApplicationOptions = { ...options, authoredCampaignStart: true, startupCommands: ["echo hi"], explicitRules: true, mapProduct: "q1-classic-id1" };
await attempt("contextual", path, undefined, launch);
await attempt("explicit", join(ambiguous, "manual.sav"), "q1-classic-id1");
await attempt("ambiguous", join(ambiguous, "manual.sav"));
await attempt("mismatch", path, "q2-classic-baseq2");
await attempt("missing", join(tmp, "absent.sav"));
writeFileSync(join(tmp, "broken.pak.gz"), new Uint8Array([1, 2, 3]));
await attempt("kex", join(tmp, "broken.pak.gz"));
await attempt("shared", join(tmp, "shared.sav"), undefined, launch);
rmSync(tmp, { recursive: true, force: true });
