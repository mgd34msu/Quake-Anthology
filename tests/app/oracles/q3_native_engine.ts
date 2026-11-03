// Oracle for tests/app/test_q3_native_engine.c: Q3NativeGuestSource (simulation/q3/native-guest-source.ts) driving the
// authored Windows PE (with and without per-frame heap allocation) and the synthetic Linux ELF qagame of
// tests/support/q3-modules.ts through id's entry points, with every server trap answered 0 and logged. Each module is
// checkpointed (encodeCheckpointValue of the native-guest checkpoint: process regions, heap image with the allocator,
// heap blocks and Windows heap/TLS/FLS tables, registers, host record), restored into a fresh process and continued.
// Run: bun tests/app/oracles/q3_native_engine.ts > tests/app/oracles/q3_native_engine.txt
// It also writes the module images to tests/app/oracles/q3_native_engine/.
import { createHash } from "node:crypto";
import { mkdirSync, writeFileSync } from "node:fs";
import { join } from "node:path";
import { Q3NativeGuestSource, q3NativeProfile } from "../../../../quake-anthology-ts/src/app/bootstrap/simulation/q3/native-guest-source.ts";
import type { QvmHostCheckpoint } from "../../../../quake-anthology-ts/src/compat/qvm/module.ts";
import type { SharedHostCall } from "../../../../quake-anthology-ts/src/compat/qvm/syscalls.ts";
import { encodeCheckpointValue } from "../../../../quake-anthology-ts/src/core/save-value.ts";
import { authoredQ3Pe, syntheticQ3Elf } from "../../../../quake-anthology-ts/tests/support/q3-modules.ts";

const directory = join(import.meta.dir, "q3_native_engine");
mkdirSync(directory, { recursive: true });
const sha = (bytes: Uint8Array): string => createHash("sha256").update(bytes).digest("hex");
const cases: readonly (readonly [string, Uint8Array])[] = [
  ["pe", authoredQ3Pe()],
  ["pe-heap", authoredQ3Pe({ heap: true })],
  ["elf", syntheticQ3Elf()],
];
for (const [name, bytes] of cases) {
  writeFileSync(join(directory, `${name}.bin`), bytes);
  const profile = q3NativeProfile(bytes);
  console.log(`case ${name} ${profile} ${bytes.length} ${sha(bytes)}`);
  const module = { id: "q3:native-test", artifactPath: name.startsWith("pe") ? "qagamex86.dll" : "qagamei386.so", digest: sha(bytes), revision: "test:1" };
  const traps = (call: SharedHostCall): number => {
    const words = [1, 2, 3, 4].map(index => call.words.getInt32(index * 4, true));
    console.log(`trap ${call.kind} ${call.code} ${words.join(" ")}`);
    return 0;
  };
  const hostState = {
    checkpoint: (): QvmHostCheckpoint => ({ state: { module, format: "q3:qagame-host", bytes: Uint8Array.of(1, 2, 3) }, random: [], callbacks: [] }),
    restore: (state: QvmHostCheckpoint) => {
      console.log(`host-restore ${state.state.format} ${[...state.state.bytes].join(",")}`);
      return undefined;
    },
  };
  const engine = Q3NativeGuestSource.create({ prepared: { bytes, profile, module }, traps, hostState });
  engine.data.setClientCount(8);
  await engine.initializeAsync(0, 7, false);
  console.log(`located ${engine.data.numEntities} ${engine.data.numClients}`);
  console.log(`connect ${await engine.clientConnectAsync(0, true, false)}`);
  await engine.clientBeginAsync(0);
  await engine.runFrameAsync(100);
  await engine.clientThinkAsync(0);
  console.log(`console ${await engine.consoleCommandAsync(["synthetic"])}`);
  await engine.clientCommandAsync(0, ["say", "hi"]);
  await engine.runFrameAsync(200);
  const checkpoint = engine.checkpoint();
  const encoded = encodeCheckpointValue(checkpoint);
  console.log(
    `checkpoint ${checkpoint.regions.length} ${checkpoint.heap?.layout.length} ${checkpoint.heap?.blocks.length} ${checkpoint.heap?.windows === null ? "null" : `${checkpoint.heap?.windows.heaps.length}/${checkpoint.heap?.windows.tls.length}/${checkpoint.heap?.windows.fls.length}`} ${encoded.length} ${sha(encoded)}`,
  );
  const restored = Q3NativeGuestSource.create({ prepared: { bytes, profile, module }, traps, hostState });
  restored.data.setClientCount(8);
  restored.restore(checkpoint);
  const again = encodeCheckpointValue(restored.checkpoint());
  console.log(`restored ${again.length} ${sha(again)}`);
  await restored.runFrameAsync(300);
  await engine.runFrameAsync(300);
  const left = encodeCheckpointValue(engine.checkpoint()),
    right = encodeCheckpointValue(restored.checkpoint());
  console.log(`continued ${left.length} ${sha(left)} ${right.length} ${sha(right)}`);
  await engine.close();
  restored.retire();
  console.log(`closed ${name}`);
}
