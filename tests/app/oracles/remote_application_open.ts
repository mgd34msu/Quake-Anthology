// Oracle for tests/app/test_remote_application_open.c: the openSource errors raised
// before any content, renderer or transport is opened. Run from the TS tree:
//   bun ../quake-anthology-c/tests/app/oracles/remote_application_open.ts
import { RemoteApplication } from "../../../../quake-anthology-ts/src/app/bootstrap/remote-application.ts";
import { parseApplicationCommand } from "../../../../quake-anthology-ts/src/app/bootstrap/options.ts";

const host = { print: () => {} };
const optionsFor = (argv: string[]) => {
  const command = parseApplicationCommand(argv);
  if (command.kind !== "run" && command.kind !== "menu") throw new Error("expected run");
  return command.options;
};
async function report(label: string, run: () => Promise<unknown>): Promise<void> {
  try {
    await run();
    console.log(`${label}\tok`);
  } catch (error) {
    console.log(`${label}\t${error instanceof Error ? error.message : String(error)}`);
  }
}
await report("offline", () => RemoteApplication.open(optionsFor([]), host));
await report("q2-model", () => RemoteApplication.open({ ...optionsFor(["--connect-q2", "127.0.0.1:27910"]), characterModel: "grunt" }, host));
await report("recorded-browser", () =>
  RemoteApplication.openDemoBorrowed({} as never, optionsFor([]), host as never, { resource: { kind: "q1", path: "demo1.dem", bytes: new Uint8Array() }, timedemo: false }, () => {}),
);
