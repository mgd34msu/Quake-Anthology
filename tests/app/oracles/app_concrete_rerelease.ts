// Oracle for tests/app/test_app_concrete_rerelease.c: ApplicationRereleasePresentation (rerelease-presentation.ts) over
// real ApplicationAssets of the retail Q2 rerelease baseq2 content: retail localization catalogs per seat language,
// userinfo names, story / localized-print / sky / item-visibility / fog events, image refresh skies, the language binding
// and drawStory through a recording text layout. Run from the TS checkout:
//   cd ../quake-anthology-ts && QTS_TEST_CORPUS=~/qfiles bun ../quake-anthology-c/tests/app/oracles/app_concrete_rerelease.ts
import { homedir } from "node:os";
import { join } from "node:path";
import { ApplicationAssets } from "../../../../quake-anthology-ts/src/app/bootstrap/assets.ts";
import { loadApplicationContent } from "../../../../quake-anthology-ts/src/app/bootstrap/content.ts";
import { parseApplicationCommand } from "../../../../quake-anthology-ts/src/app/bootstrap/options.ts";
import { ApplicationRereleasePresentation } from "../../../../quake-anthology-ts/src/app/bootstrap/rerelease-presentation.ts";
import type { SimulationPresentationEvent } from "../../../../quake-anthology-ts/src/app/bootstrap/simulation/types.ts";
import { createIdentityOwner } from "../../../../quake-anthology-ts/src/contracts/identity.ts";
import { DEFAULT_IMAGE_POLICY } from "../../../../quake-anthology-ts/src/render/scene/image-policy.ts";
import type { RendererImage } from "../../../../quake-anthology-ts/src/contracts/render.ts";

const root = process.env["QTS_TEST_CORPUS"] ?? join(homedir(), "qfiles");
const out = (line: string): void => { console.log(line); };
const command = parseApplicationCommand(["--game", "q2-rerelease-baseq2", "--map", "base1", "--dedicated", "--content-root", root]);
if (command.kind !== "run") throw new Error("Expected run");
const content = await loadApplicationContent(command.options), identity = createIdentityOwner("rerelease-oracle");
const assets = new ApplicationAssets(content, { identity: Symbol("rerelease-oracle"), session: identity.session, generation: 0 });
const contentId = content.recipe.map.entities.content;
const first = identity.actor(1, 0), second = identity.actor(2, 0), item = identity.actor(40, 0);
const seat0 = identity.seat(0), seat1 = identity.seat(1);
const presentation = new ApplicationRereleasePresentation(assets, [
  { seat: seat0, actor: first },
  { seat: seat1, actor: second, language: "russian" },
]);
const images = (list: readonly RendererImage[]): string =>
  list.map(image => `${image.source.kind === "resource" ? image.source.resource.requestedPath : image.source.name}:${image.width}x${image.height}`).join(" ");
const showView = (label: string, actor: typeof first, seconds: number): void => {
  const view = presentation.view(actor, seconds);
  const fog = view.q2Fog;
  out(`${label} fog ${fog === undefined ? "-" : `${fog.density} ${fog.color.x} ${fog.color.y} ${fog.color.z}`} sky ${view.q2Sky === undefined ? "-" : `${view.q2Sky.rotation} ${view.q2Sky.autoRotate} ${view.q2Sky.axis.z} ${images(view.q2Sky.images)}`}`);
};
const showPrints = (label: string): void => {
  for (const print of presentation.drainPrints())
    if (print.kind === "q2-player" && print.event.kind === "print")
      out(`${label} print ${print.event.target.slot} ${print.event.level} ${JSON.stringify(print.event.text)} seq ${print.sequence} recipient ${print.recipient?.slot ?? "-"} entity ${print.sourceEntity ?? "-"}`);
};
const story = (label: string, actor: typeof first, seat: typeof seat0): void => {
  out(`${label} active ${presentation.storyActive(actor)}`);
  const text = {
    seat,
    layout: (options: { text: string; scale: number }) => ({ width: options.text.length * 8 * options.scale, height: 8 * options.scale }),
    draw: (_draw: unknown, options: { text: string; maxWidth: number }, position: { x: number; y: number }, scale: number) => {
      out(`${label} draw ${JSON.stringify(options.text)} max ${options.maxWidth} at ${position.x} ${position.y} scale ${scale}`);
    },
  };
  try {
    presentation.drawStory(actor, { width: 320, height: 120 } as never, text as never, 1);
  } catch (error) {
    out(`${label} error ${(error as Error).message}`);
  }
};
const event = (sequence: number, value: Record<string, unknown>, extra: Record<string, unknown> = {}): SimulationPresentationEvent =>
  ({ kind: "q2-rerelease", content: contentId, seconds: 1, sequence, ...extra, event: value }) as never;
const userinfo = (sequence: number, slot: number, name: string, recipient?: typeof first): SimulationPresentationEvent =>
  ({ kind: "q2-player", content: contentId, seconds: 1, sequence, ...(recipient === undefined ? {} : { recipient }),
    event: { kind: "userinfo", slot, name } }) as never;

out(`language ${presentation.selectedLanguage(seat0)} ${presentation.selectedLanguage(seat1)} ${presentation.selectedLanguage(identity.seat(3))}`);
presentation.receive([
  userinfo(1, 0, "Ranger"),
  userinfo(2, 1, "Private", first),
  event(3, { kind: "story", text: "$m_single_player" }),
  event(4, { kind: "localized-print", actor: first, level: "chat", text: "$g_exited_level", args: ["##P0"] }),
  event(5, { kind: "localized-print", actor: null, level: "medium", text: "$g_exited_level", args: ["##P1"] }),
  event(6, { kind: "sky", name: "unit1_", rotation: 90, autoRotate: false, axis: { x: 0, y: 0, z: 1 } }),
  event(7, { kind: "item-visibility", actor: second, item, visible: false }),
  event(8, { kind: "fog", actor: first, value: { fog: { density: 1, color: { x: 0.5, y: 1, z: 0 }, skyFactor: 0.5 },
    heightFog: { startColor: { x: 1, y: 0, z: 0 }, startDistance: 0, endColor: { x: 0, y: 0, z: 1 }, endDistance: 64, falloff: 0, density: 0 } },
    transitionMilliseconds: 1000 }),
]);
out(`visible ${presentation.itemVisible(second, item)} ${presentation.itemVisible(first, item)} ${presentation.itemVisible(identity.actor(9, 0), item)}`);
await presentation.prepare();
showPrints("first");
story("story0", first, seat0);
story("story1", second, seat1);
story("wrong", first, seat1);
showView("view0", first, 1.5);
showView("view1", second, 1.5);
presentation.receive([
  event(9, { kind: "sky", name: "space1", rotation: 45, autoRotate: true, axis: { x: 0, y: 0, z: 1 } }, { recipient: second }),
  event(10, { kind: "story", text: "" }, { recipient: first }),
  event(11, { kind: "localized-print", actor: null, level: "chat", text: "Private text", args: [] }, { recipient: first, sourceEntity: 17 }),
]);
await presentation.prepare();
showPrints("second");
story("story0b", first, seat0);
showView("view0b", first, 2);
showView("view1b", second, 2);
out(`localize ${JSON.stringify(await presentation.localizeMessage(seat1, contentId, "$g_exited_level", ["##P1"]))}`);
const localizer = await presentation.sourceLocalizer(seat0, contentId);
out(`source ${JSON.stringify(localizer("$m_single_player"))} ${JSON.stringify(localizer("plain"))}`);
try {
  await presentation.localizeMessage(identity.seat(5), contentId, "x");
} catch (error) {
  out(`unknown ${(error as Error).message}`);
}
const failures: string[] = [];
const binding = await presentation.languageBinding(seat0, contentId, error => { failures.push(String(error)); });
if (binding === undefined || binding.kind !== "choice") throw new Error("Expected a language choice");
out(`binding ${binding.id} ${binding.label} ${binding.read()} ${binding.choices().map(choice => `${choice.id}/${choice.label}`).join(",")}`);
binding.write("french");
out(`written ${binding.read()} ${presentation.selectedLanguage(seat0)} ${JSON.stringify(localizer("$m_single_player"))}`);
out(`relocalize ${JSON.stringify(await presentation.localizeMessage(seat0, contentId, "$m_single_player"))}`);
try {
  binding.write("klingon");
} catch (error) {
  out(`write ${(error as Error).message}`);
}
out(`failures ${failures.length}`);
await presentation.prepare();
story("story1c", second, seat1);
const refresh = await presentation.prepareImageRefresh(await assets.prepareImageRefresh(DEFAULT_IMAGE_POLICY));
refresh();
showView("refresh0", first, 2);
showView("refresh1", second, 2);
presentation.publishSeats([{ seat: seat1, actor: second }, { seat: identity.seat(2), actor: identity.actor(3, 0) }]);
out(`published ${presentation.storyActive(first)} ${presentation.storyActive(second)} ${presentation.selectedLanguage(seat1)} ${presentation.selectedLanguage(seat0)}`);
showView("published1", second, 2);
assets.close();
await content.close();
