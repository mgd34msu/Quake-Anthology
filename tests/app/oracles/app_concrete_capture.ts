// Oracle for tests/app/test_app_concrete_capture.c: ApplicationCapture (capture.ts) over the console commands,
// FrameCapture and a renderer whose captureNextFrame resolves on the next presented frame.
// Run: bun tests/app/oracles/app_concrete_capture.ts > tests/app/oracles/app_concrete_capture.txt
import { mkdtemp, readFile, rm, stat } from "node:fs/promises";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { ApplicationCapture, applicationCaptureRoot } from "../../../../quake-anthology-ts/src/app/bootstrap/capture.ts";
import { CommandBuffer } from "../../../../quake-anthology-ts/src/core/commands/index.ts";
import { createIdentityOwner } from "../../../../quake-anthology-ts/src/contracts/identity.ts";
import type { CommandContext } from "../../../../quake-anthology-ts/src/contracts/common.ts";

const out = (line: string): void => { console.log(line); };
const tick = async (): Promise<void> => { for (let i = 0; i < 20; i++) await Promise.resolve(); await Bun.sleep(60); };
const base = await mkdtemp(join(tmpdir(), "app-concrete-capture-"));
const root = applicationCaptureRoot(base);
const show = (text: string): string => text.split(root).join("<root>").replace(/\n/g, "\\n");
const identity = createIdentityOwner("app-concrete-capture");
const seatContext: CommandContext = { session: identity.session, origin: { kind: "local-seat", seat: identity.seat(0), client: identity.client(0, 0) } };
const pending: { resolve(image: { width: number; height: number; pixels: Uint8Array }): void; signal: AbortSignal | undefined }[] = [];
const renderer = {
  captureNextFrame(signal?: AbortSignal) {
    return new Promise((resolve, reject) => {
      const entry = { resolve, signal };
      pending.push(entry);
      signal?.addEventListener("abort", () => { pending.splice(pending.indexOf(entry), 1); reject(signal.reason); }, { once: true });
    });
  },
};
const present = (): void => {
  for (const entry of pending.splice(0)) entry.resolve({ width: 4, height: 2, pixels: new Uint8Array(32).map((_, i) => i * 7) });
};
const commands = new CommandBuffer({ dialect: "q2-classic", context: seatContext, print: text => out(`buffer: ${show(text)}`) });
const seatConsole = {} as never;
let writes = 0;
const capture = new ApplicationCapture(
  {
    commands,
    console: seat => (seat.equals(identity.seat(0)) ? seatConsole : null),
    input: () => null,
    canChat: () => false,
    root: () => root,
    mapName: () => "maps/q2dm1.bsp",
    write: async operation => { writes++; await operation(); },
    print: text => out(`print: ${show(text)}`),
  },
  renderer as never,
);
capture.activate();
capture.activate();
out(`registered writeconfig=${commands.exists("writeconfig")} screenshot=${commands.exists("screenshot")} levelshot=${commands.exists("levelshot")}`);
commands.executeNow("writeconfig test", seatContext);
await tick();
out(`writes=${writes}`);
out(`config=${show(await readFile(join(root, "settings/seat-0/test.cfg"), "utf8"))}`);
commands.executeNow("screenshot", seatContext);
await tick();
out(`pending=${capture.pendingReadback}`);
present();
await tick();
out(`pending=${capture.pendingReadback}`);
out(`shot0000.tga bytes=${(await stat(join(root, "screenshots/shot0000.tga"))).size}`);
commands.executeNow("screenshotPNG named", seatContext);
commands.executeNow("levelshot", seatContext);
await tick();
out(`pending=${capture.pendingReadback}`);
await capture.beforeWorldChange();
await tick();
out(`pending=${capture.pendingReadback}`);
commands.executeNow("levelshot", seatContext);
await tick();
present();
await tick();
out(`levelshot bytes=${(await stat(join(root, "levelshots/q2dm1.tga")).catch(() => ({ size: -1 }))).size}`);
capture.captureFrame();
await tick();
out(`pending=${capture.pendingReadback}`);
present();
await tick();
out(`shot0001.tga bytes=${(await stat(join(root, "screenshots/shot0001.tga"))).size}`);
commands.executeNow("screenshot", seatContext);
await tick();
await capture.close();
await tick();
out(`closed pending=${capture.pendingReadback} writeconfig=${commands.exists("writeconfig")}`);
capture.captureFrame();
await tick();
try { capture.activate(); } catch (error) { out(`activate: ${(error as Error).message}`); }
await rm(base, { recursive: true, force: true });
