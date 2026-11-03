// Semantic TS drift since a baseline revision: transpiles the old and new versions of
// each TS file to minified JS with Bun's transpiler (types, whitespace, comments and
// quote style removed) and reports files whose code differs. Formatting-only commits
// (for example a repo-wide reformat) drop out. Type-only changes (interfaces, type
// aliases) also drop out; they are listed separately by comparing raw type text.
// Usage: bun tools/ts_semantic_drift.ts <ts-repo> <baseline-rev> > docs/ts_semantic_drift.tsv
import { $ } from "bun";
const [repo, base] = [process.argv[2] ?? "../quake-anthology-ts", process.argv[3]];
if (!base) throw new Error("usage: bun tools/ts_semantic_drift.ts <ts-repo> <baseline-rev>");
const transpiler = new Bun.Transpiler({ loader: "ts", minifyWhitespace: true, trimUnusedImports: false });
const norm = (text: string) => { try { return transpiler.transformSync(text); } catch (e) { return "PARSE-ERROR " + String(e); } };
const typeText = (text: string) => (text.match(/(?:export\s+)?(?:interface|type)\s+\w+[\s\S]*?(?:\n\}|;\n)/g) ?? []).join("\n").replace(/\s+/g, " ");
const files = (await $`git -C ${repo} ls-files src tests`.text()).split("\n").filter(f => f.endsWith(".ts") && !f.endsWith(".d.ts"));
const old = new Set((await $`git -C ${repo} ls-tree -r --name-only ${base} -- src tests`.text()).split("\n"));
console.log("status\tfile");
let counts: Record<string, number> = {};
for (const file of files) {
  const now = await Bun.file(`${repo}/${file}`).text();
  let status: string;
  if (!old.has(file)) status = "added";
  else {
    const before = await $`git -C ${repo} show ${base}:${file}`.text();
    if (before === now) status = "same";
    else if (norm(before) === norm(now)) status = typeText(before) === typeText(now) ? "format-only" : "types-only";
    else status = "code";
  }
  counts[status] = (counts[status] ?? 0) + 1;
  if (status !== "same") console.log(`${status}\t${file}`);
}
for (const file of old) if (file.endsWith(".ts") && file && !files.includes(file)) { console.log(`removed\t${file}`); counts.removed = (counts.removed ?? 0) + 1; }
console.error(JSON.stringify(counts));
