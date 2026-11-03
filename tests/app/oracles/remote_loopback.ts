// The TS side of tests/app/test_remote_loopback.c: a RemoteApplication driven over stdin/stdout in lockstep with the
// C harness, which owns the C server and the UDP relay. Requests: "open <now>\t<arg>...", "step <ms> <now>", "close";
// every answer ends with "done". performance.now is the harness's virtual clock and crypto.getRandomValues the
// harness's fixed byte pattern (byte i of every request is 0x5a + 7 * i), so both clients see the same time and
// randomness. Run from the quake-anthology-ts root: bun <this file>. GPL-2.0-or-later.
let virtualNow = 1000;
performance.now = () => virtualNow;
const pattern = <T extends ArrayBufferView | null>(array: T): T => {
  if (array === null) return array;
  const bytes = new Uint8Array(array.buffer, array.byteOffset, array.byteLength);
  for (let i = 0; i < bytes.length; i++) bytes[i] = (0x5a + 7 * i) & 255;
  return array;
};
crypto.getRandomValues = pattern as typeof crypto.getRandomValues;

const root = process.cwd();
const { parseApplicationCommand } = await import(`${root}/src/app/bootstrap/options.ts`);
const { RemoteApplication } = await import(`${root}/src/app/bootstrap/remote-application.ts`);

const out = (line: string): void => {
  process.stdout.write(`${line}\n`);
};
let console = "";
const flush = (all: boolean): void => {
  for (;;) {
    const end = console.indexOf("\n");
    if (end < 0) break;
    out(`console ${console.slice(0, end)}`);
    console = console.slice(end + 1);
  }
  if (all && console.length > 0) {
    out(`console ${console}`);
    console = "";
  }
};
const host = {
  print: (text: string): undefined => {
    console += text;
    return undefined;
  },
};
const fail = (operation: string, error: unknown): void => {
  flush(true);
  const name = error instanceof Error ? error.name : "Error";
  const message = error instanceof Error ? error.message : String(error);
  out(`error ${operation} ${name}: ${message.split("\n")[0]}`);
};
// biome-ignore lint/suspicious/noExplicitAny: the oracle reads the output structurally
const dump = (output: any): void => {
  const s = output.snapshot;
  out(
    `snapshot frame ${s.frame.frame} time ${s.frame.time.value} actors ${s.actors.length} bodies ${s.bodies.length} inventories ${s.inventories.length} configurations ${s.configurations.length} entities ${s.scene.entities.length} lights ${s.scene.lights.length} events ${output.events.length}`,
  );
  for (const actor of s.actors) out(`actor ${actor.id.slot}:${actor.id.generation} ${actor.definition ?? "-"}`);
  for (const body of s.bodies) {
    const b = body.body;
    out(
      `body ${body.actor.slot} origin ${b.origin.x} ${b.origin.y} ${b.origin.z} angles ${b.angles.x} ${b.angles.y} ${b.angles.z} velocity ${b.velocity.x} ${b.velocity.y} ${b.velocity.z}`,
    );
  }
  for (const event of output.events) out(`event ${event.sequence} ${event.payload.kind}`);
};

// biome-ignore lint/suspicious/noExplicitAny: RemoteApplication instance
let app: any = null;
const reader = Bun.stdin.stream().getReader();
const decoder = new TextDecoder();
let pending = "";
const nextLine = async (): Promise<string | null> => {
  for (;;) {
    const end = pending.indexOf("\n");
    if (end >= 0) {
      const line = pending.slice(0, end);
      pending = pending.slice(end + 1);
      return line;
    }
    const chunk = await reader.read();
    if (chunk.done) return null;
    pending += decoder.decode(chunk.value, { stream: true });
  }
};
for (;;) {
  const line = await nextLine();
  if (line === null) break;
  const [head, ...args] = line.split("\t");
  const words = (head ?? "").split(" ");
  if (words[0] === "open") {
    virtualNow = Number(words[1]);
    try {
      const parsed = parseApplicationCommand(args);
      if (parsed.kind !== "run") throw new Error("client: expected a run command");
      app = await RemoteApplication.open(parsed.options, host);
      flush(false);
    } catch (error) {
      fail("open", error);
    }
  } else if (words[0] === "step") {
    virtualNow = Number(words[2]);
    // Let the event loop deliver the datagrams the relay forwarded before this step.
    await Bun.sleep(2);
    try {
      const output = await app.step(Number(words[1]));
      flush(false);
      out(`phase ${app.networkPhase}`);
      if (output !== null) dump(output);
    } catch (error) {
      fail("step", error);
    }
  } else if (words[0] === "close") {
    try {
      if (app !== null) await app.close();
    } catch (error) {
      fail("close", error);
    }
    flush(true);
    out("done");
    break;
  }
  out("done");
}
process.exit(0);
