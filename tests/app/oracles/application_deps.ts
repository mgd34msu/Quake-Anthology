// Oracle for tests/app/test_application_deps.c (ApplicationImageSettings). Run from ../quake-anthology-ts:
//   bun ../quake-anthology-c/tests/app/oracles/application_deps.ts
import { mkdtemp, mkdir, rm, writeFile, readFile } from "node:fs/promises";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { ApplicationImageSettings } from "../../../../quake-anthology-ts/src/app/bootstrap/image-settings.ts";
import { ApplicationViewSettings } from "../../../../quake-anthology-ts/src/app/bootstrap/view-settings.ts";
import { CommandBuffer } from "../../../../quake-anthology-ts/src/core/commands/index.ts";
import { createIdentityOwner } from "../../../../quake-anthology-ts/src/contracts/identity.ts";
const context = { session: createIdentityOwner("deps").session, origin: { kind: "local-console" } } as const;
const show = (label: string, value: unknown) => console.log(`${label}=${typeof value === "string" ? value : JSON.stringify(value)}`);
for (const dialect of ["q1-netquake", "q3"] as const) {
  const root = await mkdtemp(join(tmpdir(), "deps-"));
  const prints: string[] = [];
  const s = await ApplicationImageSettings.open({ context, dialect, userContentRoot: root, print: t => prints.push(t), deferPersistence: true });
  for (const name of ["volume", "bgmvolume", "gamma", "vid_gamma", "r_gamma", "fov", "s_geometryAcoustics", "viewsize", "cl_sbar", "s_outputRate", "music_shuffle"])
    show(`${dialect}:${name}`, s.cvars.find(name) === undefined ? "-" : `${s.cvars.variableString(name)}/${s.cvars.find(s.cvars.canonicalName(name))?.flags}`);
  show(`${dialect}:policy`, s.policy);
  show(`${dialect}:model`, s.modelPolicy);
  await rm(root, { recursive: true, force: true });
}
{
  const root = await mkdtemp(join(tmpdir(), "deps-"));
  await mkdir(join(root, "settings"));
  await writeFile(join(root, "settings/images.cfg"),
    'seta r_gamma "2"\nseta r_customwidth "900"\nset volume "0.3"\nr_enhancedmodels 0\nseta gl_debug_linewidth "5"\nunknowncmd 1\nseta r_texture_formats "png tga"\nseta r_gamma "1.5"\n');
  const prints: string[] = [];
  const s = await ApplicationImageSettings.open({ context, dialect: "q2-classic", userContentRoot: root, print: t => prints.push(t),
    displayOverrides: { height: 700 }, renderWorker: true, gamma: 1.25 });
  show("loaded:persisted", s.persistedEntries);
  show("loaded:prints", prints);
  for (const name of ["r_gamma", "r_customwidth", "r_customheight", "r_smp", "volume", "r_enhancedmodels", "gl_debug_linewidth"]) show(`loaded:${name}`, s.cvars.variableString(name));
  show("loaded:gamma", s.gamma);
  show("loaded:policy", s.policy);
  s.cvars.set("gl_debug_linewidth", "-1");
  show("loaded:line", s.debugLineWidth);
  show("loaded:line-value", s.cvars.variableString("gl_debug_linewidth"));
  s.cvars.set("r_model_distance", "  4096 ");
  show("loaded:model", s.modelPolicy);
  s.cvars.set("r_model_distance", "far");
  try { s.modelPolicy; } catch (e) { show("loaded:model-error", (e as Error).message); }
  s.cvars.set("r_model_distance", "SOURCE");
  show("loaded:prints2", prints);
  await s.close();
  show("loaded:file-first", await readFile(join(root, "settings/images.cfg"), "utf8"));
  s.enablePersistence();
  await s.close();
  show("loaded:file-second", await readFile(join(root, "settings/images.cfg"), "utf8"));
  await rm(root, { recursive: true, force: true });
}
{
  const root = await mkdtemp(join(tmpdir(), "deps-"));
  const images = await ApplicationImageSettings.open({ context, dialect: "q3", userContentRoot: root, print: () => {}, deferPersistence: true });
  const seen: number[] = [], view = new ApplicationViewSettings(value => { seen.push(value); });
  let release = view.bindCvars(images.cvars);
  const candidate = images.prepareClientSettings();
  const candidateSeen: number[] = [], candidateView = new ApplicationViewSettings(value => { candidateSeen.push(value); });
  const releaseCandidateView = candidate.settings.bindViewSettings(candidateView);
  const commands = new CommandBuffer({ dialect: "q3", context, cvars: candidate.settings.cvars });
  commands.executeNow("gamma 0.5"); commands.executeNow("s_volume 0.2"); commands.executeNow("fov 120");
  show("cand:gamma", candidate.settings.gamma);
  show("cand:volume", candidate.settings.cvars.variableString("volume"));
  show("cand:owner-volume", images.cvars.variableString("volume"));
  show("cand:view", [view.fieldOfView, seen, candidateView.fieldOfView, candidateSeen]);
  commands.executeNow("fov 15");
  show("cand:fov15", [candidateView.fieldOfView, candidate.settings.cvars.variableString("fov")]);
  releaseCandidateView();
  commands.executeNow("fov 15");
  show("cand:fov15-released", candidate.settings.cvars.variableString("fov"));
  const replacement = new ApplicationViewSettings(() => {});
  const releaseReplacement = candidate.settings.bindViewSettings(replacement);
  releaseCandidateView();
  commands.executeNow("fov 125");
  show("cand:replacement", replacement.fieldOfView);
  commands.executeNow("fov 120");
  releaseReplacement();
  candidate.validatePublication();
  release();
  candidate.publish();
  show("cand:published", [images.gamma, images.cvars.variableString("s_volume"), images.cvars.variableString("fov")]);
  try { candidate.publish(); } catch (e) { show("cand:republish", (e as Error).message); }
  const rejected = images.prepareClientSettings();
  rejected.settings.cvars.set("fov", "15");
  show("cand:rejected-fov", rejected.settings.cvars.variableString("fov"));
  images.cvars.set("volume", "0.4");
  try { rejected.publish(); } catch (e) { show("cand:rejected", (e as Error).message); }
  show("cand:volume-after", images.cvars.variableString("volume"));
  release = view.bindCvars(images.cvars);
  release();
  await images.close();
  await rm(root, { recursive: true, force: true });
}
// Content rows (QTS_TEST_CORPUS): resolveApplicationMovement, authoredCampaignStart, the QVM grapple and weapon behavior preparation.
{
  const corpus = process.env.QTS_TEST_CORPUS;
  if (corpus !== undefined && corpus !== "") {
    const { parseApplicationCommand } = await import("../../../../quake-anthology-ts/src/app/bootstrap/options.ts");
    const { resolveApplicationMovement } = await import("../../../../quake-anthology-ts/src/app/bootstrap/content.ts");
    const { discoverInstalledContent } = await import("../../../../quake-anthology-ts/src/content/catalog/index.ts");
    const { authoredCampaignStart } = await import("../../../../quake-anthology-ts/src/app/bootstrap/authored-start.ts");
    const { applicationQvmGrappleSelection, prepareApplicationQvmGrapple } = await import("../../../../quake-anthology-ts/src/app/bootstrap/qvm-grapple-selection.ts");
    const { prepareApplicationWeaponBehavior } = await import("../../../../quake-anthology-ts/src/app/bootstrap/weapon-behavior-selection.ts");
    const { openMountPlan } = await import("../../../../quake-anthology-ts/src/content/mounts/index.ts");
    const { createMountPlanId } = await import("../../../../quake-anthology-ts/src/contracts/content.ts");
    const catalog = await discoverInstalledContent({ corpusRoot: corpus, discoverMods: true });
    for (const movement of ["qw", "q2-classic-baseq2", "q2", "q3-baseq3"]) {
      const command = parseApplicationCommand(["--game", "q1-classic-id1", "--movement", movement]);
      if (command.kind !== "run" && command.kind !== "menu") throw new Error(command.kind);
      const resolved = resolveApplicationMovement(catalog, command.options);
      show(`movement:${movement}`, [resolved.movement, resolved.movementProduct ?? null]);
    }
    const forContent = async (content: string) => {
      const mounts = await catalog.mountsFor(content);
      return openMountPlan({ id: createMountPlanId("test", Buffer.from(content).toString("hex")), mounts, defaultOrder: mounts.map(m => m.identity.id), prefixOrders: [] });
    };
    for (const [product, map] of [["q2-rerelease-baseq2", "base1"], ["q2-rerelease-baseq2", "maps/bunk1.bsp"], ["q2-rerelease-xatrix", "xcompnd1"], ["q2-rerelease-baseq2", "q2dm1"], ["q1-classic-id1", "start"]] as const) {
      const starts = await catalog.authoredStartsFor(product);
      const start = authoredCampaignStart(starts, map);
      show(`start:${product}:${map}`, start === null ? null : { kind: start.target.kind, name: start.target.name, spawn: start.target.spawnPoint, newUnit: start.target.newUnit, next: start.target.next?.name ?? null, items: start.startItems });
    }
    for (const product of catalog.products.filter(p => p.expectation.family === "q3" && p.availability.kind === "installed").map(p => p.id)) {
      const selection = await applicationQvmGrappleSelection(catalog, product);
      if (selection === null || selection.kind !== "enabled" || selection.mechanic !== "q3-qvm") { show(`grapple:${product}`, null); continue; }
      const prepared = await prepareApplicationQvmGrapple(selection, forContent);
      show(`grapple:${product}`, [selection.source.provider, selection.profile.id, prepared.artifact.module.artifactPath, prepared.artifact.module.digest]);
      try { await prepareApplicationQvmGrapple({ ...selection, source: { ...selection.source, provider: "q3:grapple/other" } }, forContent); }
      catch (e) { show(`grapple-mismatch:${product}`, (e as Error).message); }
    }
    try {
      await prepareApplicationWeaponBehavior(catalog, { source: { provider: "weapon-behavior:q1-classic-id1", content: catalog.product("q1-classic-id1").id },
        artifact: { requestedPath: "progs.dat", digest: "x" } as never, definition: { id: "id1:none" } as never }, forContent);
    } catch (e) { show("behavior:q1-classic-id1", (e as Error).message); }
  }
}
{
  const { isPrivateHost, isPrivateNetworkAddress } = await import("../../../../quake-anthology-ts/src/network/common/endpoint.ts");
  const { parseApplicationCommand } = await import("../../../../quake-anthology-ts/src/app/bootstrap/options.ts");
  const hosts = ["localhost", "a.localhost", "box.local", "svc.internal", "10.1.2.3", "127.0.0.1", "0.0.0.0", "169.254.1.1", "172.16.0.1", "172.32.0.1",
    "192.168.1.1", "100.64.0.1", "100.128.0.1", "8.8.8.8", "::", "::1", "fc00::1", "fd12::1", "fe80::1", "febf::1", "fec0::1", "::ffff:8.8.8.8",
    "2001:db8::1", "[::1]", "LOCALHOST", "Box.LOCAL", "example.com", "1.2.3"];
  show("private-hosts", hosts.map(host => isPrivateHost(host)));
  show("private-addresses", [
    isPrivateNetworkAddress({ kind: "loopback", host: "x", port: 1 } as never),
    isPrivateNetworkAddress({ kind: "ipx", network: 0, node: [0, 0, 0, 0, 0, 1], port: 1 } as never),
    isPrivateNetworkAddress({ kind: "ipv4", host: [192, 168, 0, 9], port: 1 } as never),
    isPrivateNetworkAddress({ kind: "ipv4", host: [203, 0, 113, 9], port: 1 } as never),
    isPrivateNetworkAddress({ kind: "ipv6", host: "fe80::2", port: 1 } as never),
    isPrivateNetworkAddress({ kind: "ipv6", host: "2001:db8::2", port: 1 } as never),
  ]);
  const lan = parseApplicationCommand(["--game", "q1-classic-id1", "--lan-only", "--listen", "26000"]);
  show("lan-only", lan.kind === "run" || lan.kind === "menu" ? [lan.kind, lan.options.trustedNetworksOnly ?? false] : lan.kind);
  const plain = parseApplicationCommand(["--game", "q1-classic-id1"]);
  show("lan-only-default", plain.kind === "run" || plain.kind === "menu" ? [plain.kind, plain.options.trustedNetworksOnly ?? false] : plain.kind);
}
