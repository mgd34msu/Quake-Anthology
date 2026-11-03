// Oracle for tests/app/test_startup_menu_draw.c: StartupMenu.draw() and captionCommands() over the retail Q1 classic id1
// content. The menu is built as StartupApplication.openGraphics builds it (StartupSelectionModel over the discovered
// catalog, loadMenuFont/loadMenuTypography, loadNativeUiArt with { frames: false }, CharacterTurntable), driven through
// seat input events, and every RenderCommand draw() emits is printed in order (kind, rect, uv, colour, image source).
// Run from the TS checkout:
//   cd ../quake-anthology-ts && QTS_TEST_CORPUS=~/qfiles bun ../quake-anthology-c/tests/app/oracles/startup_menu_draw.ts \
//     > ../quake-anthology-c/tests/app/oracles/startup_menu_draw.txt
import { homedir, tmpdir } from "node:os";
import { join } from "node:path";
import { discoverInstalledContent } from "../../../../quake-anthology-ts/src/content/catalog/index.ts";
import { parseApplicationCommand } from "../../../../quake-anthology-ts/src/app/bootstrap/options.ts";
import { StartupSelectionModel } from "../../../../quake-anthology-ts/src/app/bootstrap/startup-selection.ts";
import { StartupMenu } from "../../../../quake-anthology-ts/src/app/bootstrap/startup-menu.ts";
import { CharacterTurntable } from "../../../../quake-anthology-ts/src/app/bootstrap/character-turntable.ts";
import { createIdentityOwner } from "../../../../quake-anthology-ts/src/contracts/identity.ts";
import { createMountPlanId } from "../../../../quake-anthology-ts/src/contracts/content.ts";
import type { RenderCommand } from "../../../../quake-anthology-ts/src/contracts/render.ts";
import type { SeatInputEvent, UiDrawContext } from "../../../../quake-anthology-ts/src/contracts/ui.ts";
import { SceneImageRegistry } from "../../../../quake-anthology-ts/src/render/scene/resources.ts";
import { openMountPlan } from "../../../../quake-anthology-ts/src/content/mounts/index.ts";
import { loadMenuFont, loadMenuTypography } from "../../../../quake-anthology-ts/src/app/bootstrap/menu-font.ts";
import { loadNativeUiArt } from "../../../../quake-anthology-ts/src/ui/common/index.ts";
import { loadMenuArtImage } from "../../../../quake-anthology-ts/src/app/bootstrap/menu-art.ts";
import { defaultUiPreferences } from "../../../../quake-anthology-ts/src/ui/settings/accessibility.ts";
import type { UiPreferenceValues } from "../../../../quake-anthology-ts/src/ui/settings/index.ts";

const root = process.env["QTS_TEST_CORPUS"] ?? join(homedir(), "qfiles");
const out = (line: string): void => {
  console.log(line);
};
const command = parseApplicationCommand([
  "--content-root",
  root,
  "--user-content-root",
  join(tmpdir(), "startup-menu-draw-oracle"),
  "--game",
  "q1-classic-id1",
  "--map",
  "e1m2",
]);
if (command.kind !== "run") throw new Error("Expected run options");
const catalog = await discoverInstalledContent({ corpusRoot: root, discoverMods: false });
const model = new StartupSelectionModel(catalog, command.options);
await model.prepareMaps();
const identity = createIdentityOwner("startup-menu-draw"),
  seat = identity.seat(0),
  client = identity.client(0, 0);
const images = new SceneImageRegistry({ identity: Symbol("startup-menu-draw"), session: identity.session, generation: 0 });
const product = catalog.require("q1-classic-id1");
const mounts = await catalog.mountsFor(product.id);
const mounted = await openMountPlan({ id: createMountPlanId("startup", "font"), mounts, defaultOrder: mounts.map(mount => mount.identity.id), prefixOrders: [] });
const font = await loadMenuFont({ catalog, mounts: mounted, family: "q1", rerelease: false, images });
const typography = await loadMenuTypography(catalog, images, font.font.classic);
const fontSource = font.font.classic.picture.image.source;
if (fontSource.kind !== "resource") throw new Error("Startup font has no mounted resource identity");
const art = await loadNativeUiArt(fontSource.resource.id, images, loadMenuArtImage, { frames: false });
const turntable = new CharacterTurntable({ catalog: () => catalog, images, print: () => undefined });
let appearance: UiPreferenceValues = defaultUiPreferences;
const menu = new StartupMenu({
  seat,
  model,
  art,
  font: typography.body,
  titleFont: typography.title,
  characterTurntable: turntable,
  appearance: () => appearance,
  now: () => 0,
  play: () => undefined,
  load: () => undefined,
  saves: () => ({ rows: [], error: null }),
  refreshSaves: () => undefined,
  quit: () => undefined,
});

const number = (value: number): string => String(value);
const rect = (value: { x: number; y: number; width: number; height: number }): string =>
  `${number(value.x)},${number(value.y)},${number(value.width)},${number(value.height)}`;
// Long generated names (TrueType atlases list every code point) print as their first 48 characters and length.
const shortName = (name: string): string => (name.length > 48 ? `${name.slice(0, 48)}~${name.length}` : name);
const describe = (command: RenderCommand): string => {
  switch (command.kind) {
    case "set-color":
      return `set-color ${number(command.color.x)},${number(command.color.y)},${number(command.color.z)},${number(command.color.w)}`;
    case "stretch-pic": {
      const source = command.image.source.kind === "resource" ? `resource:${command.image.source.resource.id}` : `generated:${shortName(command.image.source.name)}`;
      return `stretch-pic ${rect(command.rect)} uv ${number(command.uv.s1)},${number(command.uv.t1)},${number(command.uv.s2)},${number(command.uv.t2)} ${source} ${number(command.image.width)}x${number(command.image.height)}`;
    }
    default:
      return command.kind;
  }
};
const context = (width: number, height: number, time = 0): UiDrawContext => ({
  binding: {
    seat,
    client,
    viewport: { x: 0, y: 0, width, height },
    safeArea: { x: 0, y: 0, width, height },
    hudScale: 1,
    presentation: {
      doppler: { kind: "source" },
      environment: { kind: "audio-content" },
      assets: product.id,
      hud: { provider: "q1:official", content: product.id },
      effects: { provider: "q1:official", content: product.id },
      audio: { provider: "q1:official", content: product.id },
    },
  },
  timeMilliseconds: time,
});
const draw = (name: string, width = 640, height = 480): void => {
  const commands: string[] = [];
  menu.draw(
    context(width, height),
    command => {
      commands.push(describe(command));
    },
    () => {
      throw new Error("material draw");
    },
  );
  out(`state ${name} active ${menu.controller.activeMenu} commands ${commands.length}`);
  for (const line of commands) out(line);
};
const input = (event: Omit<SeatInputEvent, "seat" | "timeMilliseconds"> & Record<string, unknown>): void => {
  menu.input({ seat, timeMilliseconds: 0, ...event } as SeatInputEvent);
};
const move = (x: number, y: number): void => input({ kind: "mouse-motion", position: { x, y }, delta: { x: 0, y: 0 } });
const click = (x: number, y: number): void => {
  move(x, y);
  input({ kind: "mouse-button", button: 1, down: true });
  input({ kind: "mouse-button", button: 1, down: false });
};
const key = (code: number): void => {
  input({ kind: "key", code, down: true, repeat: false });
  input({ kind: "key", code, down: false, repeat: false });
};

draw("main");
click(100, 114 + 12); // Play a game
draw("family");
click(100, 114 + 12); // Quake
draw("campaign");
key(27);
key(27);
click(100, 114 + 2 * 33 + 12); // Options
draw("options");
key(27);
click(100, 114 + 33 + 12); // Load Game
draw("load-empty");
key(27);
click(100, 114 + 12);
click(100, 114 + 3 * 33 + 12); // Custom game
draw("session");
move(100, 140); // hover the first WORLD row
draw("session-hover");
click(100, 135); // Game picker
draw("dropdown-product");
move(340, 150);
draw("dropdown-product-hover");
key(27);
click(100, 158); // Map set picker
draw("dropdown-mapset");
input({ kind: "mouse-wheel", delta: { x: 0, y: -1 } });
draw("dropdown-mapset-scrolled");
key(27);
// Character picker (appearance dropdown with the turntable viewer).
const characterRow = 110 + 15 + 3 * 23 + 8 + 15; // PLAYER section's first row (WORLD has three rows)
click(100, characterRow + 5);
draw("dropdown-character");
key(9);
draw("dropdown-character-focus");
key(27);
draw("session-closed");
menu.setStatus("Checking map rules...", true);
draw("status-busy");
menu.setStatus("");
appearance = { ...defaultUiPreferences, highContrast: true, typeface: "bold", menuScale: 0.8 };
draw("session-appearance", 800, 600);
appearance = defaultUiPreferences;
draw("session-wide", 1280, 720);

const captions = (width: number, height: number): void => {
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
  const commands = menu.captionCommands(
    [cue("a", "The slipgate is open. Find the exit before the Shambler finds you.", "Ranger"), cue("b", "Second line\nwith a break", null)],
    context(width, height, 500),
  );
  out(`captions ${width}x${height} commands ${commands.length}`);
  for (const command of commands) out(describe(command));
};
captions(640, 480);
captions(1280, 720);
appearance = { ...defaultUiPreferences, captions: false };
captions(640, 480);
appearance = defaultUiPreferences;

menu.close();
turntable.close();
art.close();
typography.close();
font.close();
images.close();
mounted.close();
