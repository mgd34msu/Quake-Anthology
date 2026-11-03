// Oracle for tests/app/test_app_concrete_seat_ui.c: the seat UI asset owners over real ApplicationAssets of the retail
// Q1 rerelease id1 content: ApplicationWeaponHudAssets (weapon-hud.ts: WAD lumps, qpic icons, cache), ApplicationQ1Wheel
// (q1-wheel.ts over id1/wwheel.txt) and SeatGamePrompt (game-prompt.ts, catalogs from loc_english.txt). Run from the TS
// checkout:
//   cd ../quake-anthology-ts && QTS_TEST_CORPUS=~/qfiles bun ../quake-anthology-c/tests/app/oracles/app_concrete_seat_ui.ts
import { homedir } from "node:os";
import { join } from "node:path";
import { ApplicationAssets } from "../../../../quake-anthology-ts/src/app/bootstrap/assets.ts";
import { loadApplicationContent } from "../../../../quake-anthology-ts/src/app/bootstrap/content.ts";
import { parseApplicationCommand } from "../../../../quake-anthology-ts/src/app/bootstrap/options.ts";
import { ApplicationWeaponHudAssets } from "../../../../quake-anthology-ts/src/app/bootstrap/weapon-hud.ts";
import { ApplicationQ1Wheel } from "../../../../quake-anthology-ts/src/app/bootstrap/q1-wheel.ts";
import { SeatGamePrompt, gamePromptMenu } from "../../../../quake-anthology-ts/src/app/bootstrap/game-prompt.ts";
import type { PlayerUi, SimulationPresentationEvent } from "../../../../quake-anthology-ts/src/app/bootstrap/simulation/types.ts";
import { NativeUiController } from "../../../../quake-anthology-ts/src/ui/common/index.ts";
import { createIdentityOwner } from "../../../../quake-anthology-ts/src/contracts/identity.ts";
import type { ItemId } from "../../../../quake-anthology-ts/src/contracts/gameplay.ts";

const root = process.env["QTS_TEST_CORPUS"] ?? join(homedir(), "qfiles");
const out = (line: string): void => { console.log(line); };
const command = parseApplicationCommand(["--game", "q1-rerelease-id1", "--map", "e1m1", "--dedicated", "--content-root", root]);
if (command.kind !== "run") throw new Error("Expected run");
const content = await loadApplicationContent(command.options), identity = createIdentityOwner("seat-ui-oracle");
const assets = new ApplicationAssets(content, { identity: Symbol("seat-ui-oracle"), session: identity.session, generation: 0 });
const contentId = content.recipe.map.entities.content;
const source = { provider: "q1:foundation", content: contentId } as const;

// ---- ApplicationWeaponHudAssets ----
const icons = new ApplicationWeaponHudAssets(assets);
const describe = (id: string | null): string => {
  if (id === null) return "null";
  const picture = icons.picture(id as never);
  const shape = picture === undefined ? "-" : picture.kind === "image" ? `image ${picture.name} ${picture.image.width}x${picture.image.height}` : `material ${picture.name}`;
  return `${id} aspect ${icons.aspect(id as never)} ${shape}`;
};
for (const item of ["q1:weapon/rocketlauncher", "q1:weapon/axe", "q1:weapon/lightning"] as ItemId[]) {
  const status = { source, item, label: item, ammo: { kind: "unmetered" } } as never;
  const prepared = await icons.prepare(status);
  out(`hud ${item} weapon ${describe(prepared.weapon)} ammo ${describe(prepared.ammo)}`);
}
out(`hud null ${JSON.stringify(await icons.prepare(null))}`);
const again = await icons.prepare({ source, item: "q1:weapon/rocketlauncher", label: "", ammo: { kind: "unmetered" } } as never);
out(`hud cached ${again.weapon} ${again.ammo}`);
out(`hud aspect-null ${icons.aspect(null)}`);
try {
  await icons.load({ kind: "image", resource: { content: contentId, path: "gfx/weapons/missing.lmp" } });
  out("hud missing loaded");
} catch (error) {
  out(`hud missing ${(error as Error).message}`);
}

// ---- ApplicationQ1Wheel ----
const owned = new Set(["q1:weapon/axe", "q1:weapon/shotgun", "q1:weapon/supershotgun", "q1:weapon/rocketlauncher"]);
const weapons: ItemId[] = ["q1:weapon/axe", "q1:weapon/shotgun", "q1:weapon/supershotgun", "q1:weapon/nailgun", "q1:weapon/supernailgun",
  "q1:weapon/grenadelauncher", "q1:weapon/rocketlauncher", "q1:weapon/lightning"] as ItemId[];
const player = (active: ItemId | null): PlayerUi => ({
  weaponStatus: { source, item: active ?? ("q1:weapon/axe" as ItemId), label: "", ammo: { kind: "unmetered" } },
  activeWeapon: active,
  items: weapons.map((id, index) => ({
    id, label: `W${index}`, kind: "weapon", sourceOrdinal: index, owned: owned.has(id), hasAmmo: id !== "q1:weapon/supershotgun",
    count: index * 3, warningCount: 0,
  })),
} as unknown as PlayerUi);
const wheel = new ApplicationQ1Wheel();
out(`wheel before ${JSON.stringify(wheel.items(player(null)))}`);
await wheel.prepare(assets, icons, player("q1:weapon/rocketlauncher" as ItemId));
for (const item of wheel.items(player("q1:weapon/rocketlauncher" as ItemId)) ?? [])
  out(`wheel item ${JSON.stringify(item)}`);
for (const [first, second] of [[0, 1], [5, 1], [6, 0], [7, 2], [1.5, 3], [-1, 1], [3, 4]] as const)
  out(`wheel switch ${first} ${second} ${wheel.switchWeapon(player("q1:weapon/rocketlauncher" as ItemId), first, second)}`);
await wheel.prepare(assets, icons, { ...player(null), weaponStatus: null } as PlayerUi);
out(`wheel cleared ${JSON.stringify(wheel.items(player(null)))}`);

// ---- SeatGamePrompt ----
const seat = identity.seat(0), actor = identity.actor(1, 0), other = identity.actor(2, 0);
const impulses: number[] = [];
const controller = new NativeUiController({
  seat,
  skin: () => ({}) as never,
  measureText: () => 0,
  now: () => 0,
  bindings: () => [],
  focus: () => undefined,
  sound: () => undefined,
} as never);
let language = "english";
const prompt = new SeatGamePrompt(seat, () => actor, controller, value => { impulses.push(value); }, () => language);
const promptEvent = (who: typeof actor, kind: "prompt" | "clear-prompt", title = "", choices: { label: string; impulse: number }[] = []) =>
  ({ kind: "q1-composition", content: contentId, event: { kind, actor: who, title, choices } }) as unknown as SimulationPresentationEvent;
const show = (label: string): void => {
  const menu = (controller as unknown as { menus: Map<string, () => { title: string; controls: { id: string; label: string; enabled: boolean }[] }> }).menus.get(gamePromptMenu)?.();
  out(`${label} active ${controller.activeMenu} title ${JSON.stringify(menu?.title)} controls ${JSON.stringify(menu?.controls.map(control => [control.id, control.label, control.enabled]))}`);
};
prompt.receive([promptEvent(other, "prompt", "$qc_exited", [{ label: "x", impulse: 1 }])]);
await prompt.prepare(assets, () => ({ kind: "game" }) as never);
show("prompt other");
prompt.receive([promptEvent(actor, "prompt", "$qc_quad_fade", [
  { label: "$qc_suicides", impulse: 11 }, { label: "Plain", impulse: 12 }, { label: "$qc_ring_fade", impulse: 13 },
  { label: "Four", impulse: 14 }, { label: "Five", impulse: 15 },
])]);
await prompt.prepare(assets, () => ({ kind: "console" }) as never);
show("prompt console");
await prompt.prepare(assets, () => ({ kind: "game" }) as never);
show("prompt game");
const key = (code: number) => ({ kind: "key", seat, code, down: true, repeat: false }) as never;
out(`prompt key-wrong-seat ${prompt.input({ kind: "key", seat: identity.seat(1), code: 50, down: true, repeat: false } as never)}`);
out(`prompt key 2 ${prompt.input(key(50))} impulses ${JSON.stringify(impulses)}`);
show("prompt after");
prompt.receive([promptEvent(actor, "prompt", "Again", [{ label: "Only", impulse: 21 }])]);
await prompt.prepare(assets, () => ({ kind: "game" }) as never);
show("prompt again");
out(`prompt key 9 ${prompt.input(key(57))} impulses ${JSON.stringify(impulses)}`);
prompt.receive([promptEvent(actor, "clear-prompt")]);
show("prompt cleared");
prompt.close();
