// Oracle for tests/app/test_application_services.c: openInitialConfigurationContent and
// createStartupSource on retail data. Run from ../quake-anthology-ts:
//   QTS_TEST_CORPUS=~/qfiles bun ../quake-anthology-c/tests/app/oracles/application_services.ts
import { parseApplicationCommand } from "../../../../quake-anthology-ts/src/app/bootstrap/options.ts";
import { openInitialConfigurationContent, configurationDialect } from "../../../../quake-anthology-ts/src/app/bootstrap/configuration.ts";
import { createStartupSource } from "../../../../quake-anthology-ts/src/app/bootstrap/startup-source.ts";
import { createIdentityOwner } from "../../../../quake-anthology-ts/src/contracts/identity.ts";
const corpus = process.env.QTS_TEST_CORPUS!;
const cases: readonly (readonly string[])[] = [
  ["--game", "q1-classic-id1", "--map", "e1m1", "--dedicated"],
  ["--game", "q2-classic-baseq2", "--map", "base1", "--dedicated"],
  ["--game", "q3-baseq3", "--map", "q3dm1", "--dedicated", "--mode", "deathmatch"],
];
for (const argv of cases) {
  const command = parseApplicationCommand(["--content-root", corpus, ...argv]);
  if (command.kind !== "run") throw new Error(`not a launch: ${command.kind}`);
  const options = command.options;
  const content = await openInitialConfigurationContent(options);
  const s = content.selection;
  const dialect = configurationDialect(content);
  const owner = createIdentityOwner("oracle");
  const source = createStartupSource(options, { source: s.source, match: s.match, rules: options.rules ?? "standard" }, dialect,
    { session: owner.session, origin: { kind: "server-console" } }, 16, () => {});
  const names = ["skill", "deathmatch", "coop", "maxclients", "sv_maxclients", "g_gametype", "sv_fps", "fs_restrict", "com_prereleaseDemo"];
  console.log([options.product, s.source.provider, s.source.content, s.engineBehavior.provider, s.match.provider, s.movement.provider,
    dialect, String(content.q3Product !== undefined),
    ...names.map(name => `${name}=${source.find(name) === undefined ? "-" : source.variableString(name)}`)].join("|"));
  await content.close();
}
