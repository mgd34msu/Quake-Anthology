// Oracle for tests/app/test_app_concrete_cinematic.c: CampaignCinematic over retail Q2 content (baseq2 video/*.cin),
// ApplicationAssets images, the ApplicationAudio engine and a renderer that records executed frames.
// Run: SDL_AUDIODRIVER=dummy QTS_TEST_CORPUS=~/qfiles bun tests/app/oracles/app_concrete_cinematic.ts > tests/app/oracles/app_concrete_cinematic.txt
import { parseApplicationCommand } from "../../../../quake-anthology-ts/src/app/bootstrap/options.ts";
import { loadApplicationContent } from "../../../../quake-anthology-ts/src/app/bootstrap/content.ts";
import { ApplicationAssets } from "../../../../quake-anthology-ts/src/app/bootstrap/assets.ts";
import { ApplicationAudio } from "../../../../quake-anthology-ts/src/app/bootstrap/audio.ts";
import { CampaignCinematic } from "../../../../quake-anthology-ts/src/app/bootstrap/campaign-cinematic.ts";
import { createIdentityOwner } from "../../../../quake-anthology-ts/src/contracts/identity.ts";

const root = process.env["QTS_TEST_CORPUS"] ?? "";
const out = (line: string): void => { console.log(line); };
const command = parseApplicationCommand(["--game", "q2-classic-baseq2", "--map", "base1", "--dedicated", "--content-root", root]);
if (command.kind !== "run") throw new Error("Expected run");
const content = await loadApplicationContent(command.options), identity = createIdentityOwner("campaign-cinematic");
const assets = new ApplicationAssets(content, { identity: Symbol("campaign-cinematic"), session: identity.session, generation: 0 });
let now = 0;
const audio = new ApplicationAudio(content, () => now, 1, "missing-custom-model", () => undefined, {});
const frames: string[] = [];
const renderer = {
  window: { drawableSize: { width: 64, height: 48 } },
  execute: (frame: { sequence: number; commands: readonly { kind: string }[] }) => { frames.push(`${frame.sequence}:${frame.commands.map(c => c.kind).join(",")}`); },
};
const seat = identity.seat(0);
let current = true;
const live = (): boolean => current;
const attempt = async (label: string, name: string): Promise<void> => {
  try {
    await CampaignCinematic.prepare({ name, loop: false, hold: false, silent: false }, content, assets, audio, renderer as never, seat, live);
    out(`${label}: prepared`);
  } catch (error) { out(`${label}: ${(error as Error).message}`); }
};
await attempt("dotdot", "../eou1_.cin");
await attempt("missing", "none");
await attempt("cin-fallback", "none.cin");
await attempt("format", "pics/colormap.bad");
current = false;
await attempt("retired", "eou1_.cin");
current = true;
const movie = await CampaignCinematic.prepare({ name: "eou1_.cin", loop: false, hold: false, silent: false }, content, assets, audio, renderer as never, seat, live);
out(`prepared status=${movie.status}`);
movie.activate();
out(`active status=${movie.status}`);
let ended = false;
for (let i = 0; i < 6; i++) ended = movie.frame(i === 0 ? 0 : 50, i + 1);
out(`frames ended=${ended} status=${movie.status} elapsed=${movie.timeline.elapsedMilliseconds} source=${movie.timeline.sourceTimeMilliseconds}`);
out(`executed ${frames.join(" ")}`);
movie.pause(true);
out(`paused status=${movie.status}`);
movie.pause(false);
const saved = movie.captureCheckpoint() as Record<string, unknown>;
out(`checkpoint keys=${Object.keys(saved).join(",")} version=${saved["version"]} clock=${saved["clock"]} audioStarted=${saved["audioStarted"]} request=${JSON.stringify(saved["request"])}`);
out(`checkpoint resource=${(saved["resource"] as { requestedPath: string }).requestedPath} initialAudio=${(saved["initialAudio"] as unknown[]).length}`);
const input = movie.input({ kind: "key", down: true, repeat: false } as never);
out(`input=${input} status=${movie.status}`);
movie.close(9);
out(`closed status=${movie.status}`);
out(`executed close ${frames.at(-1)}`);
const restored = await CampaignCinematic.restore(saved, content, assets, audio, renderer as never, seat, live);
out(`restored status=${restored.status}`);
restored.activate();
restored.frame(50, 10);
out(`restored frame status=${restored.status} elapsed=${restored.timeline.elapsedMilliseconds}`);
restored.close(11);
try { restored.activate(); } catch (error) { out(`activate closed: ${(error as Error).message}`); }
const changed = { ...saved, request: { ...(saved["request"] as object), name: "eou2_.cin" } };
try { await CampaignCinematic.restore(changed, content, assets, audio, renderer as never, seat, live); out("changed: restored"); }
catch (error) { out(`changed: ${(error as Error).message}`); }
await audio.close();
assets.close();
content.close();
