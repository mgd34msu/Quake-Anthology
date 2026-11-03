/* Bun preload for tests/app/test_mod_declarations.c: wraps the three gameplay-mod
 * declaration readers (readQuakeCModDeclaration, readQvmModDeclaration,
 * readNativeModDeclaration) so every declaration a TS test reads is appended to
 * $QA_MOD_DECLARATION_CAPTURE as one line: runtime TAB JSON.stringify(input) TAB
 * JSON.stringify(output). Run from the TS checkout:
 *   cd ../quake-anthology-ts && QA_MOD_DECLARATION_CAPTURE=<file> \
 *     bun test --preload ../quake-anthology-c/tests/app/oracles/mod_declarations_capture.ts <tests...>
 * GPL-2.0-or-later. */
import { plugin } from "bun";
import { readFileSync } from "node:fs";

const targets: Record<string, string> = {
  "/src/content/mods/callbacks.ts": "readQuakeCModDeclaration",
  "/src/content/mods/qvm-callbacks.ts": "readQvmModDeclaration",
  "/src/content/mods/native-callbacks.ts": "readNativeModDeclaration",
};
plugin({
  name: "mod-declaration-capture",
  setup(build) {
    build.onLoad({ filter: /\/src\/content\/mods\/(callbacks|qvm-callbacks|native-callbacks)\.ts$/ }, args => {
      const key = Object.keys(targets).find(suffix => args.path.endsWith(suffix));
      let contents = readFileSync(args.path, "utf8");
      if (key !== undefined) {
        const name = targets[key]!;
        contents = contents.replace(`export function ${name}(`, `function __capture_${name}(`);
        contents += `
export function ${name}(reader: SaveReader) {
  const result = __capture_${name}(reader);
  const file = process.env.QA_MOD_DECLARATION_CAPTURE;
  if (file !== undefined) {
    const { appendFileSync } = require("node:fs");
    appendFileSync(file, \`\${result.runtime}\\t\${JSON.stringify(reader.value)}\\t\${JSON.stringify(result)}\\n\`);
  }
  return result;
}
`;
      }
      return { contents, loader: "ts" };
    });
  },
});
