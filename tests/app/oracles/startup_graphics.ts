// Oracle for tests/app/test_startup_graphics.c: a hidden graphical StartupApplication over the retail Q1 classic id1
// content (StartupApplication.open -> openGraphics with the real NativeRenderer, CPU backend, 640x480), stepped and
// driven through seat key input. Every RenderFrame graphics.draw hands to NativeRenderer.execute is printed in order
// (frame sequence, then each RenderCommand: kind, rect, uv, colour, image source), with the active menu and status
// after each step and every line the host printed.
// Run from the TS checkout (SDL_AUDIODRIVER=dummy for both sides; s_outputRate 44100 is set on the command line):
//   cd ../quake-anthology-ts && SDL_AUDIODRIVER=dummy QTS_TEST_CORPUS=~/qfiles \
//     bun ../quake-anthology-c/tests/app/oracles/startup_graphics.ts > ../quake-anthology-c/tests/app/oracles/startup_graphics.txt
import { homedir, tmpdir } from "node:os";
import { join } from "node:path";
import { mkdirSync, rmSync } from "node:fs";
import { StartupApplication } from "../../../../quake-anthology-ts/src/app/bootstrap/startup.ts";
import { NativeRenderer } from "../../../../quake-anthology-ts/src/app/bootstrap/renderer.ts";
import { parseApplicationCommand } from "../../../../quake-anthology-ts/src/app/bootstrap/options.ts";
import type { RenderCommand, RenderFrame, RendererImage } from "../../../../quake-anthology-ts/src/contracts/render.ts";

const root = process.env["QTS_TEST_CORPUS"] ?? join(homedir(), "qfiles");
const user = join(tmpdir(), "startup-graphics-oracle");
rmSync(user, { recursive: true, force: true });
mkdirSync(user, { recursive: true });
const out = (line: string): void => {
  console.log(line);
};
const command = parseApplicationCommand([
  "--menu",
  "--content-root",
  root,
  "--user-content-root",
  join(user, "content"),
  "--game",
  "q1-classic-id1",
  "--hidden",
  "--renderer",
  "cpu",
  "--width",
  "640",
  "--height",
  "480",
  "+set",
  "s_outputRate",
  "44100",
]);
if (command.kind !== "menu") throw new Error("Expected menu options");

const number = (value: number): string => String(value);
const rect = (value: { x: number; y: number; width: number; height: number }): string =>
  `${number(value.x)},${number(value.y)},${number(value.width)},${number(value.height)}`;
// Long generated names (TrueType atlases list every code point) print as their first 48 characters and length.
const shortName = (name: string): string => (name.length > 48 ? `${name.slice(0, 48)}~${name.length}` : name);
const image = (value: RendererImage): string =>
  `${value.source.kind === "resource" ? `resource:${value.source.resource.id}` : `generated:${shortName(value.source.name)}`} ${number(value.width)}x${number(value.height)}`;
const describe = (command: RenderCommand): string => {
  switch (command.kind) {
    case "draw-buffer":
      return `draw-buffer ${command.buffer} clear ${String(command.clear)}`;
    case "image-resource":
      return command.operation.kind === "texture-mode"
        ? `image-resource texture-mode`
        : `image-resource ${command.operation.kind} ${image(command.operation.image)}`;
    case "set-color":
      return `set-color ${number(command.color.x)},${number(command.color.y)},${number(command.color.z)},${number(command.color.w)}`;
    case "stretch-pic":
      return `stretch-pic ${rect(command.rect)} uv ${number(command.uv.s1)},${number(command.uv.t1)},${number(command.uv.s2)},${number(command.uv.t2)} ${image(command.image)}`;
    default:
      return command.kind;
  }
};
let frames = 0;
const execute = NativeRenderer.prototype.execute;
NativeRenderer.prototype.execute = function (this: NativeRenderer, frame: RenderFrame): undefined {
  out(`frame ${frames++} commands ${frame.commands.length}`);
  for (const command of frame.commands) out(describe(command));
  return execute.call(this, frame);
};
const app = await StartupApplication.open(
  command.options,
  {
    print: (text: string): undefined => {
      out(`print ${JSON.stringify(text)}`);
      return undefined;
    },
  },
  join(user, "saves"),
  command.kind,
);
const seat = app.inputSeat;
if (seat === null) throw new Error("Missing startup seat");
// The private graphics record, read for the active menu and status after each step.
const graphics = (app as unknown as { graphics: { menu: { controller: { activeMenu: string | null }; status: string } } }).graphics;
const step = async (name: string): Promise<void> => {
  await app.step();
  out(`step ${name} active ${String(graphics.menu.controller.activeMenu)} status ${JSON.stringify(graphics.menu.status)}`);
};
const key = (code: number): void => {
  for (const down of [true, false]) app.input({ seat, kind: "key", code, down, repeat: false, timeMilliseconds: performance.now() });
};
await step("first");
await step("second");
key(133); // Down: Load Game
await step("down");
key(132); // Up: Play a game
await step("up");
key(13); // Enter: the game family menu
await step("enter");
key(27); // Escape: back to the main menu
await step("escape");
// graphics.captionCommands through the published client record (ClientBootstrap.captionCommands).
const client = (app as unknown as { client: { captionCommands: (captions: unknown[], viewport: unknown, time: number) => RenderCommand[] } }).client;
const cue = (id: string, text: string, speaker: string | null) => ({
  id,
  kind: "subtitle" as const,
  startMilliseconds: 0,
  durationMilliseconds: 1000,
  text,
  speaker,
  arguments: [],
  localizedText: text,
  localizedSpeaker: speaker,
});
for (const captions of [[], [cue("a", "The slipgate is open. Find the exit before the Shambler finds you.", "Ranger"), cue("b", "Second line\nwith a break", null)]]) {
  const commands = client.captionCommands(captions, { x: 0, y: 0, width: 640, height: 480 }, 500);
  out(`captions ${captions.length} commands ${commands.length}`);
  for (const command of commands) out(describe(command));
}
await app.close();
rmSync(user, { recursive: true, force: true });
