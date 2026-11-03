import { Application } from "../../../../quake-anthology-ts/src/app/bootstrap/application.ts";
import { EventRing } from "../../../../quake-anthology-ts/src/foundation/event-ring.ts";
import { createIdentityOwner } from "../../../../quake-anthology-ts/src/contracts/identity.ts";
import { userDataDirectory } from "../../../../quake-anthology-ts/src/content/user-data.ts";
import { minimumFrameMilliseconds } from "../../../../quake-anthology-ts/src/app/bootstrap/frame-time.ts";
import { runFrameLoop } from "../../../../quake-anthology-ts/src/app/bootstrap/frame-loop.ts";
import { CvarRegistry } from "../../../../quake-anthology-ts/src/core/cvars/index.ts";
const P: any = Application.prototype;
const make = (fields: Record<string, unknown>): any => Object.assign(Object.create(P), fields);
const out = (label: string, value: unknown) => console.log(`${label}=${typeof value === "string" ? value : JSON.stringify(value)}`);
const owner = createIdentityOwner("oracle");
const seat0 = owner.seat(0), seat1 = owner.seat(1);
// takeRankingMenuRequest
{ const app = make({ rankingMenuRequests: new Set([seat0, seat1]) });
  out("take.0", app.takeRankingMenuRequest(seat0)); out("take.0again", app.takeRankingMenuRequest(seat0)); out("take.1", app.takeRankingMenuRequest(seat1)); }
// overrideSeats
{ const c0 = owner.client(3, 0), c1 = owner.client(5, 2);
  const s0 = { id: seat0, client: { id: c0 } }, s1 = { id: seat1, client: { id: c1 } };
  const r0 = { tag: "r0" }, r1 = { tag: "r1" };
  const app = make({ localSeats: new Map([[c0, s0], [c1, s1]]), clientCvars: new Map([[seat1, r1], [seat0, r0]]) });
  out("override", app.overrideSeats().map((row: any) => [row.seat, row.client, row.cvars.tag]));
  try { make({ localSeats: new Map([[c0, s0]]), clientCvars: new Map() }).overrideSeats(); } catch (error) { out("override.error", (error as Error).message); } }
// levelRecoveryAvailable
{ const sim = (mode: string, q1: boolean, q2: boolean, qc: boolean) => ({ options: { mode }, q1Source: () => q1 ? {} : null, q2Source: () => q2 ? {} : null, quakecSource: () => qc ? {} : null });
  const opts = (dedicated: boolean, kind: string) => ({ dedicated, network: { kind } });
  const app = make({});
  out("recovery", [
    app.levelRecoveryAvailable(sim("singleplayer", true, false, false), opts(false, "offline")),
    app.levelRecoveryAvailable(sim("singleplayer", false, true, false), opts(false, "offline")),
    app.levelRecoveryAvailable(sim("singleplayer", false, false, true), opts(false, "offline")),
    app.levelRecoveryAvailable(sim("singleplayer", false, false, false), opts(false, "offline")),
    app.levelRecoveryAvailable(sim("deathmatch", true, false, false), opts(false, "offline")),
    app.levelRecoveryAvailable(sim("singleplayer", true, false, false), opts(true, "offline")),
    app.levelRecoveryAvailable(sim("singleplayer", true, false, false), opts(false, "q1-server")) ]); }
// sourceCvars precedence
{ const tag = (name: string) => ({ name });
  const sim = (q2: unknown, q1: unknown, qc: unknown) => ({ q3Guest: () => null, q3Source: () => null, q2ServerCvars: () => q2, q1Source: () => q1 === null ? null : { cvars: q1 }, quakecSource: () => qc === null ? null : { cvars: qc } });
  const app = make({});
  out("sourceCvars", [ app.sourceCvars(sim(tag("q2"), tag("q1"), null))?.name ?? null, app.sourceCvars(sim(null, tag("q1"), null))?.name ?? null,
    app.sourceCvars(sim(null, null, tag("qc")))?.name ?? null, app.sourceCvars(sim(null, null, null)) ]); }
// queueCommand / requestQuit
{ const ring = new EventRing<any>(16384); const cursor = ring.openCursor();
  const app = make({ requestedCommands: ring, stopping: false });
  app.queueCommand("map", ["e1m1", "x"], seat1); app.queueCommand("quit", [], null); app.requestQuit();
  out("queued", ring.take(cursor).map((r: any) => [r.target, r.name, r.arguments_, r.seat === null ? null : r.seat.index]));
  out("stopping", app.stopping); }
// input guards
{ try { make({ closed: true }).input({ seat: seat0 }); } catch (error) { out("input.closed", (error as Error).message); }
  const deferred: unknown[] = [];
  out("input.saving.headless", make({ closed: false, worldOperation: "saving", graphical: null, deferredInput: deferred }).input({ seat: seat0 }));
  out("input.idle.headless", make({ closed: false, worldOperation: "idle", graphical: null, deferredInput: deferred }).input({ seat: seat0 })); }
// finished
{ const f = (stopping: boolean, closed: boolean, frameLimit: number | null, frames: number) => make({ stopping, closed, launchOptions: { frameLimit }, frames }).finished;
  out("finished", [f(false, false, null, 10), f(true, false, null, 0), f(false, true, null, 0), f(false, false, 5, 4), f(false, false, 5, 5), f(false, false, 0, 0)]); }
// campaignMovie
{ const simA = {}, simB = {}; const movies = new Map(), movieRequests = new WeakMap();
  const app = make({ movies, movieRequests, worldSimulation: simA });
  const m1 = { playback: "p1", request: {} };
  out("movie.none", app.campaignMovie === null); app.campaignMovie = m1; out("movie.set", app.campaignMovie === m1);
  app.worldSimulation = simB; out("movie.other", app.campaignMovie === null); app.worldSimulation = simA;
  movieRequests.set(simA, m1.request); app.campaignMovie = null; out("movie.cleared", [app.campaignMovie === null, movieRequests.has(simA), movies.size]); }
// directories
out("dir.saves", userDataDirectory("saves", "/tmp/qa-root/../qa-user")); out("dir.settings", userDataDirectory("settings", "relative/root"));
out("dir.saves.default", userDataDirectory("saves").replace(process.env.HOME ?? "", "~"));
{ const app = make({ host: { saveDirectory: "/tmp/x/saves" }, launchOptions: {}, serverProfileStoreValue: null });
  out("store.root", (app.serverProfileStore as any).root ?? (app.serverProfileStore as any).directory);
  out("saveDirectory.host", app.saveDirectory);
  out("saveDirectory.root", make({ host: {}, launchOptions: { userContentRoot: "/tmp/u" } }).saveDirectory); }
// minimumFrameMilliseconds
{ const q1 = new CvarRegistry({ dialect: "q1-netquake" }), q3 = new CvarRegistry({ dialect: "q3" });
  const q1t = new CvarRegistry({ dialect: "q1-quakeworld" }); q1t.register("sys_ticrate", "0.1", 0);
  out("minimum", [minimumFrameMilliseconds(null, true), minimumFrameMilliseconds(q3, true), minimumFrameMilliseconds(q1, false), minimumFrameMilliseconds(q1, true),
    minimumFrameMilliseconds(q1t, true), minimumFrameMilliseconds(q1t, false)]); }
// runFrameLoop: alive count, recover, rethrow without recover
{ let steps = 0, recovered = 0; const states: string[] = [];
  await runFrameLoop({ alive: () => steps < 4, minimumMilliseconds: () => 0, step: async () => { steps++; if (steps === 2) throw new Error("boom"); },
    clockState: () => { states.push(String(steps)); return "x"; }, recover: async (error) => { recovered++; out("loop.recover.message", (error as Error).message); } });
  out("loop", [steps, recovered, states.length]);
  try { await runFrameLoop({ alive: () => true, minimumMilliseconds: () => 0, step: async () => { throw new Error("fatal frame"); }, clockState: () => "s" }); }
  catch (error) { out("loop.rethrow", (error as Error).message); } }
// ---- P1 / P7 members ----
const { userProductDirectory } = await import("../../../../quake-anthology-ts/src/content/user-data.ts");
const AppStatic: any = Application;
const product = (id: string, family: string, edition: string, contentDirectory: string, userRoot?: string) =>
  ({ id, expectation: { id, family, edition, campaign: "", contentDirectory, baseProduct: null }, userContent: userRoot === undefined ? null : { root: userRoot }, looseRoot: null });
const catalogOf = (...products: any[]) => ({ products, product: (id: string) => { const found = products.find(p => p.id === id); if (found === undefined) throw new RangeError(`Unknown requested content or mod: ${id}`); return found; } });
const contentOf = (catalog: any, engine: string, map = engine) => ({ catalog, recipe: { engineBehavior: { content: engine, provider: "p:engine" }, map: { entities: { content: map, provider: "p:map" } }, match: { provider: "m:ffa" } },
  mounts: { plan: { id: "mount-plan:test:0", mounts: [], defaultOrder: [], prefixOrders: [] }, borrowOrderedReader: () => ({ open: async () => null }) }, q3Product: null });
// contentDialect
{ const catalog = catalogOf(product("q1/id1", "q1", "classic", "id1"), product("q1/qw", "q1", "quakeworld", "qw"), product("q2/base", "q2", "classic", "baseq2"),
    product("q2/rr", "q2", "rerelease", "rr"), product("q3/base", "q3", "retail", "baseq3"));
  out("dialect", ["q1/id1", "q1/qw", "q2/base", "q2/rr", "q3/base"].map(id => AppStatic.contentDialect(contentOf(catalog, id)))); }
// sourceConfig / clientConfig
{ const catalog = catalogOf(product("q1/id1", "q1", "classic", "id1", "/tmp/qa-user/id1-own"), product("q3/base", "q3", "retail", "baseq3"));
  const content = contentOf(catalog, "q3/base", "q1/id1");
  out("config.source", (AppStatic.sourceConfig({ userContentRoot: "/tmp/qa-user" }, content) as any).root);
  out("config.client", (AppStatic.clientConfig({ userContentRoot: "/tmp/qa-user" }, content) as any).root);
  out("config.product", userProductDirectory("/tmp/qa-user", "baseq3")); }
// debugGraphSettings
{ const cvars = new CvarRegistry({ dialect: "q1-netquake" });
  for (const [name, value] of [["debuggraph", "1"], ["timegraph", "0"], ["netgraph", "0"], ["graphheight", "48"], ["graphscale", "2.5"], ["graphshift", "-3"]]) cvars.register(name, value, 0);
  out("graph.cvars", make({ worldSimulation: { q3Guest: () => null, q3Source: () => null, q2ServerCvars: () => null, q1Source: () => ({ cvars }), quakecSource: () => null } }).debugGraphSettings());
  out("graph.none", make({ worldSimulation: { q3Guest: () => null, q3Source: () => null, q2ServerCvars: () => null, q1Source: () => null, quakecSource: () => null } }).debugGraphSettings()); }
// prepareSourceCommands, Q1 branch
{ const catalog = catalogOf(product("q1/id1", "q1", "classic", "id1"));
  const content = contentOf(catalog, "q1/id1");
  const cvars = new CvarRegistry({ dialect: "q1-netquake", context: { session: "oracle-session", origin: { kind: "server-console" } } } as any);
  const simulation = { options: { mode: "singleplayer", maxClients: 4 }, q2Native: () => null, q2Source: () => null, q1Source: () => ({ cvars }), quakecSource: () => null,
    q3Guest: () => null, q3Source: () => null, q2ServerCvars: () => null };
  const requests: any[] = [];
  const app = make({ imageSettings: null, sourceCommands: null, session: { session: "oracle-session" }, operatorState: null, launchOptions: { dedicated: false, network: { kind: "offline" } },
    host: { print: () => undefined }, ownership: { kind: "owned" }, localSeats: new Map() });
  const prepared = await app.prepareSourceCommands(simulation, content, false, (request: any) => { requests.push(request); return undefined; });
  out("prep.dialect", prepared.options.dialect);
  out("prep.names", [...prepared.commands.registeredNames()].sort());
  out("prep.cvars", ["cl_avidemo", "cl_forceavidemo", "debuggraph", "timegraph", "netgraph", "graphheight", "graphscale", "graphshift", "sv_autosave", "sv_autosave_interval"].map(name => [name, cvars.variableString(name), cvars.find(name)?.flags ?? null]));
  prepared.commands.executeNow("map e1m2 x");
  prepared.commands.executeNow("say hello");
  out("prep.requests", requests.map(r => [r.target, r.name, r.arguments_, r.seat, r.source?.origin.kind ?? null]));
  try { prepared.commands.executeNow("map e1m3", { session: "oracle-session", origin: { kind: "server-console" }, producer: { kind: "game-module", module: { id: "mod:x", artifactPath: "a", digest: "d", revision: "d" }, instance: Symbol("i") } }); }
  catch (error) { out("prep.component", (error as Error).message); }
  out("prep.component.requests", requests.length); }
// kickTargets errors, requestRestart error, appendQ1Commands
{ const sim = { q3Source: () => null, q3Guest: () => null, players: () => [], q2Source: () => null };
  const app = make({ worldSimulation: sim, localSeats: new Map(), bots: null });
  try { app.kickTargets([]); } catch (error) { out("kick.usage", (error as Error).message); }
  try { app.kickTargets(["Ranger"]); } catch (error) { out("kick.none", (error as Error).message); }
  try { await app.requestRestart([]); } catch (error) { out("restart.error", (error as Error).message); }
  const appended: string[] = [];
  make({ sourceCommands: { append: (text: string) => appended.push(text) } }).appendQ1Commands([
    { kind: "q1", event: { kind: "server-command", text: "changelevel e1m2\n" } }, { kind: "q1", event: { kind: "sound" } }, { kind: "q2", event: { kind: "server-command", text: "x" } }]);
  out("append", appended);
  try { make({ sourceCommands: null }).appendQ1Commands([{ kind: "q1", event: { kind: "server-command", text: "x" } }]); } catch (error) { out("append.error", (error as Error).message); } }
// guestOutput, operatorHost
{ try { make({ network: null, launchOptions: { network: { kind: "q3-server" } }, localGuest: null }).guestOutput(); } catch (error) { out("guest.error", (error as Error).message); }
  const printed: string[] = []; const box: { write: ((text: string) => void) | null } = { write: null };
  const host = AppStatic.operatorHost({ print: (text: string) => { printed.push(`host:${text}`); } }, box);
  host.print("a"); box.write = text => { printed.push(`op:${text}`); }; host.print("b"); box.write = null; host.print("c");
  out("operator", printed); }
// openDedicatedConsole
{ const ring = new EventRing<any>(16384); const cursor = ring.openCursor();
  const app = make({ sourceCommands: null, imageSettings: null, session: { session: "oracle-session" }, host: { print: () => undefined }, loadedContent: contentOf(catalogOf(product("q1/id1", "q1", "classic", "id1")), "q1/id1"),
    requestedCommands: ring, stopping: false });
  app.openDedicatedConsole();
  app.dedicatedCommands.executeNow("map e1m4"); app.dedicatedCommands.executeNow("addbot 3");
  out("dedicated.names", [...app.dedicatedCommands.registeredNames()].filter((name: string) => ["quit", "save", "load", "map", "say", "addbot", "removebot", "botlist"].includes(name)).sort());
  out("dedicated.queued", ring.take(cursor).map((r: any) => [r.name, r.arguments_, r.seat]));
  app.dedicatedCommands.executeNow("quit"); out("dedicated.quit", app.stopping); }
// ---- P3 members ----
// isTeamArenaSkirmish(simulation = this.simulation)
{ const q3 = (product: string, nextmap: string) => { const cvars = new CvarRegistry({ dialect: "q3" }); cvars.register("nextmap", nextmap, 0);
    return { q3Source: () => ({ options: { product }, host: { cvars } }) }; };
  out("skirmish", [make({ worldSimulation: { q3Source: () => null } }).isTeamArenaSkirmish(),
    make({}).isTeamArenaSkirmish(q3("missionpack", "teamarena-results")), make({}).isTeamArenaSkirmish(q3("baseq3", "teamarena-results")),
    make({}).isTeamArenaSkirmish(q3("missionpack", "q3dm1"))]); }
// queueLocalGuestCommand: the published guest uses requestedCommands, a candidate its own queue
{ const ring = new EventRing<any>(16384), cursor = ring.openCursor(), pending = new EventRing<any>(16384), pendingCursor = pending.openCursor();
  const current = { pendingCommands: new EventRing<any>(16384) }, candidate = { pendingCommands: pending };
  const app = make({ localGuest: current, requestedCommands: ring });
  out("guest.queue.current", app.queueLocalGuestCommand(current, { target: "client", name: "score", arguments_: [], seat: seat0 }) ?? "undefined");
  app.queueLocalGuestCommand(candidate, { target: "client", name: "team", arguments_: ["red"], seat: seat1 });
  out("guest.queue", [ring.take(cursor).map((r: any) => [r.name, r.arguments_]), pending.take(pendingCursor).map((r: any) => [r.name, r.arguments_])]); }
// static startupScriptReader(content, options, scripts): game loose roots first, then the base product's
{ const { mkdirSync, writeFileSync, rmSync } = await import("node:fs");
  const root = "/tmp/qa-application-core-p3-scripts";
  rmSync(root, { recursive: true, force: true });
  for (const [dir, name, text] of [["base", "default.cfg", "base default"], ["base", "shared.cfg", "base shared"], ["game", "shared.cfg", "game shared"], ["user", "user.cfg", "user text"]]) {
    mkdirSync(`${root}/${dir}`, { recursive: true }); writeFileSync(`${root}/${dir}/${name}`, text); }
  const base = { id: "q1/id1", expectation: { id: "q1/id1", baseProduct: null }, userContent: null, looseRoot: `${root}/base` };
  const mod = { id: "q1/mod", expectation: { id: "q1/mod", baseProduct: "q1/id1" }, userContent: { root: `${root}/user` }, looseRoot: `${root}/game` };
  const content = { catalog: { product: (id: string) => (id === "q1/id1" ? base : mod) }, recipe: { engineBehavior: { content: "q1/mod" } } };
  const scripts = { readMountedScript: async () => undefined, read: async (name: string) => `user:${name}` };
  const read = AppStatic.startupScriptReader(content, {}, scripts);
  const source = { session: "s", origin: { kind: "server-console" } };
  out("reader", [await read("shared.cfg", source, "loose"), await read("default.cfg", source, "loose"), await read("shared.cfg", source, "base-loose"),
    await read("default.cfg", source, "game-loose"), await read("user.cfg", source, "game-loose"), await read("x.cfg", source, "user"), await read("x.cfg", source, "mounted")]); }
// ---- P4/P5 members ----
// guards of saveGame / loadGame / restoreSavedGame / replaceWorld / changeLevel
{ const msg = async (run: () => Promise<unknown>) => { try { await run(); return "ok"; } catch (error) { return (error as Error).message; } };
  const idle = { closed: false, stepping: false, network: null, worldOperation: "idle", movies: new Map(), worldSimulation: {} };
  out("save.closed", await msg(() => make({ ...idle, closed: true }).saveGame("slot1.sav")));
  out("load.network", await msg(() => make({ ...idle, network: {} }).loadGame("slot1.sav")));
  out("load.stepping", await msg(() => make({ ...idle, stepping: true }).loadGame("slot1.sav")));
  out("restore.busy", await msg(() => make({ ...idle, worldOperation: "saving" }).restoreSavedGame("slot1.sav")));
  out("replace.busy", await msg(() => make({ ...idle, worldOperation: "saving" }).replaceWorld("e1m2", null)));
  const sim = {}, movie = { playback: {}, request: {}, current: () => true, complete: async () => {} };
  out("restore.movie", await msg(() => make({ ...idle, worldSimulation: sim, movies: new Map([[sim, movie]]) }).restoreSavedGame("slot1.sav")));
  out("replace.movie", await msg(() => make({ ...idle, worldSimulation: sim, movies: new Map([[sim, movie]]) }).replaceWorld("e1m2", null)));
  out("change.stepping", await msg(() => make({ ...idle, stepping: true }).changeLevel("e1m2"))); }
// reportCampaignTravelError and applyTransition's terminal branches
{ const printed: string[] = [];
  const base = { host: { print: (text: string) => printed.push(text) }, graphical: null, fatalWorldFailure: false, capture: null, ownedVideoRestart: null,
    ownership: { kind: "owned" }, pendingSave: null, pendingTeamArena: null, pendingMap: null, pendingNativeTravel: null, pendingRestart: null,
    worldSimulation: { pendingMatchMap: () => null, q3Source: () => null, q3Guest: () => null } };
  make(base).reportCampaignTravelError(new Error("Map e9m9 is not installed"));
  out("report", printed.splice(0));
  try { make({ ...base, fatalWorldFailure: true }).reportCampaignTravelError(new Error("Map e9m9 is not installed")); } catch (error) { out("report.fatal", (error as Error).message); }
  await make({ ...base, pendingTransition: null }).applyTransition(); out("apply.none", printed.splice(0));
  const complete = make({ ...base, pendingTransition: { kind: "campaign-complete" } }); await complete.applyTransition();
  out("apply.complete", [printed.splice(0), complete.pendingTransition]);
  const round = make({ ...base, pendingTransition: { kind: "round-restart" } });
  try { await round.applyTransition(); } catch (error) { out("apply.round", [(error as Error).message, round.pendingTransition]); } }
// localViewport(seat, width, height) and reserveMovie(simulation)
{ const c0 = owner.client(0, 0), c1 = owner.client(1, 0);
  const app = make({ localSeats: new Map([[c1, { id: seat1 }], [c0, { id: seat0 }]]), movieRequests: new Map(), movies: new Map(), presentationFrames: 0 });
  out("viewport", [app.localViewport(seat0, 640, 480), app.localViewport(seat1, 640, 480)]);
  const sim = {}, a = app.reserveMovie(sim), b = app.reserveMovie(sim);
  out("movies", [a !== b, app.movieRequests.size, app.movieRequests.get(sim) === b]); }
// ---- P6 members ----
// commands(): graphics-free dispatch through applicationCommand / guestCommand / sourceCommand; errors print with "\n"
{ const ring = new EventRing<any>(16384); const cursor = ring.openCursor(); const printed: string[] = [];
  const sim = { options: { mode: "singleplayer", modCommands: undefined, maxClients: 1 }, q3Guest: () => null, q3Source: () => null, q2Source: () => null,
    q2Native: () => null, q1Source: () => null, quakecSource: () => null, recipe: { equipment: { grapple: { kind: "disabled" }, handGrenades: { kind: "disabled" } } } };
  const app = make({ requestedCommands: ring, requestedCursor: cursor, graphical: null, worldSimulation: sim, stopping: false, closed: false, stepping: false,
    capture: null, pendingSave: null, pendingTeamArena: null, pendingMap: null, pendingNativeTravel: null, pendingRestart: null, pendingTransition: null,
    ownership: { kind: "owned" }, operatorOutput: { write: null }, operatorState: null, ownedDemos: null, movies: new Map(), movieRequests: new WeakMap(),
    sourcePublished: false, host: { print: (text: string) => { printed.push(text); } }, session: { session: owner.session } });
  for (const name of ["cinematicpause", "foo", "+grapple", "local_join", "say", "quit"]) app.queueCommand(name, [], null);
  await app.commands();
  out("p6.commands.printed", printed);
  out("p6.commands.stopping", app.stopping);
  out("p6.commands.pending", ring.pending(cursor));
  try { app.commandActor(null); } catch (error) { out("p6.commandActor", (error as Error).message); }
  out("p6.pendingShell", app.pendingShellPublication); }
