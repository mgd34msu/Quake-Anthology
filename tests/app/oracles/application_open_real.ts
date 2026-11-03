/* Oracle for tests/app/test_application_open_real.c: opens the real TS Application dedicated (openApplication, as
 * src/main.ts does for --dedicated) on retail data, steps it, closes it and prints the console output and a summary.
 * Run from the TS checkout:
 *   cd ../quake-anthology-ts && bun ../quake-anthology-c/tests/app/oracles/application_open_real.ts <case> <corpus> <user-root> <frames> <ms>
 * Cases: q1 (id1 e1m1), q2 (baseq2 base1), q3 (baseq3 q3dm1). GPL-2.0-or-later. */
import { mkdirSync, rmSync } from "node:fs";
import { openApplication } from "../../../../quake-anthology-ts/src/app/bootstrap/application.ts";
import { parseApplicationCommand } from "../../../../quake-anthology-ts/src/app/bootstrap/options.ts";

const [caseName, corpus, userRoot, framesText, millisecondsText] = process.argv.slice(2);
if (caseName === undefined || corpus === undefined || userRoot === undefined || framesText === undefined || millisecondsText === undefined)
  throw new Error("usage: <case> <corpus> <user-root> <frames> <ms>");
const argvOf: Record<string, string[]> = {
  q1: ["--game", "q1-classic-id1", "--map", "e1m1"],
  q2: ["--game", "q2-classic-baseq2", "--map", "base1"],
  q3: ["--game", "q3-baseq3", "--map", "q3dm1"],
};
const base = argvOf[caseName];
if (base === undefined) throw new Error(`unknown case ${caseName}`);
rmSync(userRoot, { recursive: true, force: true });
mkdirSync(userRoot, { recursive: true });
const command = parseApplicationCommand(["--content-root", corpus, "--user-content-root", userRoot, ...base, "--dedicated"]);
if (command.kind !== "run") throw new Error("expected run");
const printed: string[] = [];
const application = await openApplication(command.options, {
  print: (text: string): undefined => {
    printed.push(text);
    return undefined;
  },
});
const frames = Number(framesText),
  milliseconds = Number(millisecondsText);
let snapshot = "";
for (let frame = 0; frame < frames; frame++) {
  const output = await application.step(milliseconds);
  snapshot = `snapshot frame ${output.snapshot.frame.frame} actors ${output.snapshot.actors.length} bodies ${output.snapshot.bodies.length} inventories ${output.snapshot.inventories.length} events ${output.events.length}`;
}
const recipe = application.content.recipe,
  simulation = application.simulation;
const summary = [
  `recipe ${recipe.engineBehavior.content} ${recipe.map.geometry.requestedPath} ${recipe.map.entities.provider} ${recipe.movement.provider} ${recipe.character.appearance.provider}`,
  `source ${(simulation as unknown as { readonly strategy: { readonly kind: string } }).strategy.kind}`,
  `frames ${application.frameCount} finished ${application.finished}`,
  `actors ${simulation.actors.observations().length}`,
  `players ${simulation.players().length}`,
  snapshot,
];
await application.close();
const lines = printed.join("").split("\n");
if (lines.at(-1) === "") lines.pop();
for (const line of lines) process.stdout.write(`console ${line}\n`);
for (const line of summary) process.stdout.write(`${line}\n`);
