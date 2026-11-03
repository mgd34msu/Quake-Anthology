// Oracle for tests/app/test_app_concrete_tools.c: ApplicationTools' runtime diagnostics over a loose MountedContent.
// Run: bun tests/app/oracles/app_concrete_tools.ts > tests/app/oracles/app_concrete_tools.txt
// The loose mount literal uses the catalog's key order (content/catalog/index.ts: kind, identity, rootPath).
import { mkdtemp, rm } from "node:fs/promises";
import { join } from "node:path";
import { tmpdir } from "node:os";
import { ApplicationTools } from "../../../../quake-anthology-ts/src/app/bootstrap/application-tools.ts";
import { CommandBuffer } from "../../../../quake-anthology-ts/src/core/commands/index.ts";
import { createIdentityOwner } from "../../../../quake-anthology-ts/src/contracts/identity.ts";
import { createMountIdentity } from "../../../../quake-anthology-ts/src/contracts/content.ts";
import { openMountPlan } from "../../../../quake-anthology-ts/src/content/mounts/index.ts";

const root = await mkdtemp(join(tmpdir(), "application-tools-"));
const owner = createIdentityOwner("application-tools");
const context = { session: owner.session, origin: { kind: "server-console" } } as const;
const authority = new CommandBuffer({ dialect: "q3", context });
const mount = createMountIdentity("mount:tools:fixture", "q3:classic:base:fixture", 0);
const mounts = await openMountPlan({ id: "mount-plan:tools:fixture", mounts: [{ kind: "loose", identity: mount, rootPath: root }], defaultOrder: [mount.id], prefixOrders: [] });
await Bun.write(join(root, "view.camera"), "cameraPathDef { time 1 camera_interpolated { startPos ( 0 0 0 ) endPos ( 10 0 0 ) } }");
let printed = "";
const tools = new ApplicationTools(authority, {
  mounts: () => mounts,
  outputRoot: () => join(root, "output"),
  milliseconds: () => 0,
  cvars: () => null,
  renderer: () => ({ backend: "cpu", width: 640, height: 480, driver: null, displayModes: [], images: [] }),
  shaders: () => [],
  resources: () => null,
  frame: () => ({ frame: 0, milliseconds: 0, renderer: "cpu", map: "fixture", clients: 1 }),
  print: text => { printed += text; },
});
tools.bind([authority]);
authority.append("touchFile view.camera\n");
await authority.executeAsync(() => tools.drain());
authority.append("path\nresourceinfo\ndir . camera\n");
await authority.executeAsync(() => tools.drain());
process.stdout.write(printed.split(root).join("<root>").split(encodeURIComponent(root)).join("<eroot>"));
await tools.close();
mounts.close();
await rm(root, { recursive: true, force: true });
