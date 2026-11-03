// Oracle for tests/app/test_addon_library.c: AddonLibrary (src/app/bootstrap/addon-library.ts) over local fixtures only
// (no network): the tests/app/addon-library-purge.test.ts and tests/content/catalog/addons.test.ts scenarios, printing every
// status, entry list and the resulting file tree (path + sha256 of each file). Run from the TS checkout:
//   cd ../quake-anthology-ts && bun ../quake-anthology-c/tests/app/oracles/addon_library.ts > ../quake-anthology-c/tests/app/oracles/addon_library.txt
import { createHash } from "node:crypto";
import { mkdir, mkdtemp, readFile, readdir, rm, stat, writeFile } from "node:fs/promises";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { AddonLibrary } from "../../../../quake-anthology-ts/src/app/bootstrap/addon-library.ts";
import { managedAddonHidden } from "../../../../quake-anthology-ts/src/content/catalog/addons.ts";

const out = (line: string): void => {
  console.log(line);
};
// The zip() helper of tests/content/catalog/addons.test.ts (stored entries).
function zip(entries: readonly (readonly [string, string])[]): Uint8Array {
  const locals: Uint8Array[] = [],
    directory: Uint8Array[] = [];
  let offset = 0;
  for (const [path, text] of entries) {
    const name = new TextEncoder().encode(path),
      data = new TextEncoder().encode(text),
      crc = Bun.hash.crc32(data);
    const local = new Uint8Array(30 + name.length + data.length),
      a = new DataView(local.buffer);
    a.setUint32(0, 0x04034b50, true);
    a.setUint16(4, 20, true);
    a.setUint32(14, crc, true);
    a.setUint32(18, data.length, true);
    a.setUint32(22, data.length, true);
    a.setUint16(26, name.length, true);
    local.set(name, 30);
    local.set(data, 30 + name.length);
    locals.push(local);
    const central = new Uint8Array(46 + name.length),
      b = new DataView(central.buffer);
    b.setUint32(0, 0x02014b50, true);
    b.setUint16(4, 20, true);
    b.setUint16(6, 20, true);
    b.setUint32(16, crc, true);
    b.setUint32(20, data.length, true);
    b.setUint32(24, data.length, true);
    b.setUint16(28, name.length, true);
    b.setUint32(42, offset, true);
    central.set(name, 46);
    directory.push(central);
    offset += local.length;
  }
  const directoryLength = directory.reduce((sum, bytes) => sum + bytes.length, 0),
    result = new Uint8Array(offset + directoryLength + 22);
  let position = 0;
  for (const bytes of [...locals, ...directory]) {
    result.set(bytes, position);
    position += bytes.length;
  }
  const end = new DataView(result.buffer, position);
  end.setUint32(0, 0x06054b50, true);
  end.setUint16(8, entries.length, true);
  end.setUint16(10, entries.length, true);
  end.setUint32(12, directoryLength, true);
  end.setUint32(16, offset, true);
  return result;
}
const exists = async (path: string): Promise<boolean> =>
  stat(path).then(
    () => true,
    () => false,
  );
const tree = async (root: string, prefix = ""): Promise<string[]> => {
  const lines: string[] = [];
  const entries = (await readdir(join(root, prefix), { withFileTypes: true })).map(entry => entry.name).sort();
  for (const name of entries) {
    const relative = prefix === "" ? name : `${prefix}/${name}`;
    if (/^install-/.test(name) && prefix === ".addons") {
      lines.push(`${relative.replace(/install-.*/, "install-*")} dir`);
      continue;
    }
    const info = await stat(join(root, relative));
    if (info.isDirectory()) {
      lines.push(`${relative} dir`);
      lines.push(...(await tree(root, relative)));
    } else lines.push(`${relative} ${createHash("sha256").update(await readFile(join(root, relative))).digest("hex")}`);
  }
  return lines;
};
let currentRoot = "";
const show = (name: string, library: AddonLibrary): void => {
  out(`${name} status ${library.status().replaceAll(currentRoot, "<root>")} scope ${library.scope()}`);
  for (const entry of library.entries()) out(`${name} entry ${entry.id}|${entry.label}|${entry.detail ?? "-"}|${entry.unavailable ?? "-"}`);
};
const dump = async (name: string, root: string): Promise<void> => {
  for (const line of await tree(root)) out(`${name} tree ${line}`);
};

// 1. a removed add-on's directory, cached package and tombstone are deleted at the next start
{
  const root = await mkdtemp(join(tmpdir(), "quake-addon-purge-"));
  currentRoot = root;
  const hidden = `qd_${"a".repeat(20)}_${"b".repeat(20)}`,
    kept = `qd_${"c".repeat(20)}_${"d".repeat(20)}`,
    sha256 = "e".repeat(64),
    keptSha = "f".repeat(64);
  for (const [name, digest] of [
    [hidden, sha256],
    [kept, keptSha],
  ] as const) {
    await mkdir(join(root, "q1", name), { recursive: true });
    await writeFile(join(root, "q1", name, ".quaddicted.json"), JSON.stringify({ sha256: digest, title: name, group: name, start: null }));
    await mkdir(join(root, ".addons"), { recursive: true });
    await writeFile(join(root, ".addons", `${digest}.zip`), "package");
  }
  await mkdir(join(root, ".addons", "removed", "q1"), { recursive: true });
  await writeFile(join(root, ".addons", "removed", "q1", hidden), "");
  const library = new AddonLibrary({ root, changed: async () => undefined, launch: () => undefined });
  await library["readInstalled"]();
  show("purge", library);
  await dump("purge", root);
  await library.close();
  await rm(root, { recursive: true, force: true });
}
// 2. local install/update/remove preserve immutable prior loose bytes
{
  const root = await mkdtemp(join(tmpdir(), "addon-library-"));
  currentRoot = root;
  let changes = 0;
  const library = new AddonLibrary({
    root,
    changed: async () => {
      changes++;
    },
    launch: () => {},
  });
  const path = join(root, "map.zip");
  await writeFile(path, zip([["maps/example.bsp", "first version"]]));
  library.create.submit(path);
  await library.settle();
  show("local-first", library);
  await writeFile(path, zip([["maps/example.bsp", "second version"]]));
  library.create.submit(path);
  await library.settle();
  out(`local changes ${changes}`);
  show("local-second", library);
  const first = (await readdir(join(root, "q1"))).sort()[0] ?? "";
  out(`local first-hidden ${await managedAddonHidden(root, "q1/" + first)}`);
  const local = library.entries().find(entry => entry.id.startsWith("local:"));
  if (local !== undefined) {
    library.activate(local.id);
    show("local-selected", library);
    library.activate("local-remove");
    await library.settle();
  }
  out(`local changes ${changes}`);
  show("local-removed", library);
  await dump("local", root);
  await library.close();
  await rm(root, { recursive: true, force: true });
}
// 3. failed catalog publication restores the previous version
{
  const root = await mkdtemp(join(tmpdir(), "addon-cancel-"));
  currentRoot = root;
  let fail = false;
  const library = new AddonLibrary({
    root,
    changed: async () => {
      if (fail) throw new Error("catalog failed");
    },
    launch: () => {},
  });
  const path = join(root, "map.zip");
  await writeFile(path, zip([["maps/example.bsp", "first version"]]));
  library.create.submit(path);
  await library.settle();
  const original = library.entries().find(entry => entry.id.startsWith("local:"));
  fail = true;
  await writeFile(path, zip([["maps/example.bsp", "second version"]]));
  library.create.submit(path);
  await library.settle();
  show("restore", library);
  out(`restore original-hidden ${await managedAddonHidden(root, "q1/" + (original?.id.slice(6) ?? ""))}`);
  await dump("restore", root);
  await library.close();
  await rm(root, { recursive: true, force: true });
}
// 4. classic and rerelease package visibility is independent; a game-directory ZIP and a non-ZIP
{
  const root = await mkdtemp(join(tmpdir(), "addon-editions-"));
  currentRoot = root;
  const launches: string[] = [];
  const classic = new AddonLibrary({ root, edition: "classic", changed: async () => {}, launch: (product, map) => launches.push(`${product} ${map}`) });
  const rerelease = new AddonLibrary({ root, edition: "rerelease", changed: async () => {}, launch: () => {} });
  const path = join(root, "Map.zip");
  await writeFile(path, zip([["maps/example.bsp", "geometry"]]));
  classic.create.submit(path);
  await classic.settle();
  rerelease.create.submit(path);
  await rerelease.settle();
  show("classic", classic);
  show("rerelease", rerelease);
  const item = classic.entries()[0];
  if (item !== undefined) {
    classic.activate(item.id);
    classic.activate("local-play");
    await classic.settle();
    classic.activate("local-remove");
    await classic.settle();
  }
  out(`editions launches ${launches.join(",")}`);
  show("classic-removed", classic);
  const game = join(root, "Game Pack.ZIP");
  await writeFile(game, zip([["mygame/maps/start.bsp", "start"], ["mygame/progs.dat", "progs"]]));
  classic.create.submit(game);
  await classic.settle();
  show("classic-game", classic);
  const text = join(root, "notes.txt");
  await writeFile(text, "not a zip");
  classic.create.submit(text);
  await classic.settle();
  show("classic-text", classic);
  classic.create.submit(join(root, "missing.zip"));
  await classic.settle();
  out(`classic-missing status ${classic.status().replaceAll(root, "<root>")}`);
  await dump("editions", root);
  await classic.close();
  await rerelease.close();
  await rm(root, { recursive: true, force: true });
}
