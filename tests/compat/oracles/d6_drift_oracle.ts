// Oracle for tests/guest/test_guest_drift.c and tests/compat/test_compat_drift.c (TS drift packet D6).
// Run: bun tests/compat/oracles/d6_drift_oracle.ts (expects ../quake-anthology-ts beside this repository)
import { createContentDigest } from "../../../../quake-anthology-ts/src/contracts/content.ts";
import type { ModuleIdentity } from "../../../../quake-anthology-ts/src/contracts/execution.ts";
import { SparseGuestMemory, GuestMemoryFault } from "../../../../quake-anthology-ts/src/guest/core/index.ts";
import { ucrtQuickSort } from "../../../../quake-anthology-ts/src/guest/runtime/windows/crt.ts";
import { virtualCpuid } from "../../../../quake-anthology-ts/src/guest/core/cpuid.ts";
import { filterGuestConsole } from "../../../../quake-anthology-ts/src/compat/q2/guest-console.ts";
import { fitRereleaseText } from "../../../../quake-anthology-ts/src/compat/q2/rerelease/module.ts";
import { fitClassicUserinfo } from "../../../../quake-anthology-ts/src/compat/q2/classic/records.ts";
import { classicPrintf, classicPrintfLayouts } from "../../../../quake-anthology-ts/src/compat/q2/classic/printf.ts";
import { legacyClientCommand } from "../../../../quake-anthology-ts/src/compat/qvm/legacy-client-abi.ts";
import { qvmEvent, qvmEntityType, qvmPowerupBits } from "../../../../quake-anthology-ts/src/compat/qvm/legacy-presentation.ts";

const out = (label: string, value: unknown) =>
  console.log(`${label} ${JSON.stringify(value, (_k, v) => (typeof v === "bigint" ? v.toString() : v))}`);

// ucrtQuickSort on fixed keys: final permutation and comparison count.
for (const keys of [[3, 1, 2], [5, 3, 9, 1, 1, 7, 2, 8, 6, 4, 0, 3], [2, 2, 2, 1, 1, 1, 0, 0, 0, 2, 1]]) {
  const identity = keys.map((_, index) => index);
  let comparisons = 0;
  ucrtQuickSort(
    keys.length,
    (l, r) => (comparisons++, (keys[identity[l] ?? 0] ?? 0) - (keys[identity[r] ?? 0] ?? 0)),
    (l, r) => {
      const held = identity[l] ?? 0;
      identity[l] = identity[r] ?? 0;
      identity[r] = held;
    },
  );
  out(`qsort ${keys.join(",")}`, { order: identity, comparisons });
}

for (const [leaf, cx8] of [[0n, false], [1n, true], [1n, false], [0x80000000n, false], [0x80000001n, false], [2n, true]] as const)
  out(`cpuid ${leaf} ${cx8}`, virtualCpuid(leaf, cx8));

// SparseGuestMemory: quota, scalar access, writeDeliveries, allocator image.
const module: ModuleIdentity = { id: "test:d6", artifactPath: "d6", revision: "1", digest: createContentDigest("aa".repeat(32)) };
{
  const memory = new SparseGuestMemory({ module, pointerBytes: 4, maximumMappedBytes: 0x3000 });
  memory.map({ base: 0x10000n, byteLength: 0x2000, permissions: "read-write", label: "a" });
  try {
    memory.map({ base: 0x20000n, byteLength: 0x2000, permissions: "read-write", label: "b" });
  } catch (error) {
    out("quota", error instanceof GuestMemoryFault ? { reason: error.reason, message: error.message } : String(error));
  }
  memory.unmap(memory.pointer(0x10000n)!, 0x1000);
  memory.map({ base: 0x20000n, byteLength: 0x2000, permissions: "read-write", label: "b" });
  out("quota after unmap", memory.mappings().map(m => [Number(m.base), m.byteLength]));
}
{
  const memory = new SparseGuestMemory({ module, pointerBytes: 4 });
  const a = memory.map({ base: 0x10000n, byteLength: 0x1000, permissions: "read-write", label: "a" });
  out("scalarRead cold", memory.scalarRead(0x10004, 4));
  memory.writeUint32(memory.offset(a, 4n), 0xdeadbeef);
  out("scalarRead warm", [memory.scalarRead(0x10004, 4), memory.scalarRead(0x10004, 2), memory.scalarRead(0x10007, 1), memory.scalarRead(0x10ffe, 4)]);
  out("scalarWrite", [memory.scalarWrite(0x10008, 2, 0x12345), memory.readUint32(memory.offset(a, 8n)), memory.scalarWrite(0x20000, 4, 1)]);
  const seen: unknown[] = [];
  const stop = memory.observeWrites(memory.offset(a, 0x100n), 8, ranges => seen.push(ranges));
  const before = memory.writeDeliveries;
  memory.writeUint32(memory.offset(a, 0x200n), 1);
  const unrelated = memory.writeDeliveries - before;
  memory.writeUint32(memory.offset(a, 0x104n), 1);
  memory.scalarWrite(0x10102, 4, 7);
  out("deliveries", { unrelated, after: memory.writeDeliveries - before, seen });
  stop();
}
{
  const memory = new SparseGuestMemory({ module, pointerBytes: 4 });
  const pages = [0, 1, 2].map(() => memory.allocate({ byteLength: 4096, alignment: 4096n, pooled: true, label: "p" }));
  memory.allocate({ byteLength: 100, label: "plain" });
  memory.unmap(pages[1]!, 4096);
  const image = memory.allocatorImage();
  out("allocator image", { hints: image.hints, slabs: image.slabs.map(s => ({ base: s.base, used: [...s.used.slice(0, 4)], next: s.next })), freePages: image.freePages, pages: pages.map(p => p.byteOffset) });
  const again = memory.allocate({ byteLength: 4096, alignment: 4096n, pooled: true, label: "p" });
  out("pooled reuse", again.byteOffset);
  memory.restoreAllocator(image);
  out("restored reuse", memory.allocate({ byteLength: 4096, alignment: 4096n, pooled: true, label: "p" }).byteOffset);
  try {
    memory.restoreAllocator({ ...image, freePages: [999] });
  } catch (error) {
    out("restore invalid", String(error));
  }
}

for (const text of ['gamemap "base1"\n', "kick 3; quit", 'say "a;quit"\nset rcon_password x', "MAP q2dm1", "  ;;\n", 'echo hi;wait;BIND x "y"'])
  out(`console ${JSON.stringify(text)}`, filterGuestConsole(text));
for (const [text, capacity] of [["abcdef", 4], ["abécd", 4], ["a\u{1f600}b", 4], ["a\u{1f600}b", 5], ["a\u{1f600}b", 6], ["short", 256]] as const)
  out(`fitRerelease ${JSON.stringify(text)} ${capacity}`, [...new TextEncoder().encode(fitRereleaseText(text, capacity))]);
out("fitClassic", fitClassicUserinfo("x".repeat(600)).length);

for (const format of ["%hd|%hhu|%lld|%I64x|%llu", "%5%z|%q|100%", "%I32d %Id %Ld %jd %zd %td %wd"])
  out(`printf layouts ${format}`, classicPrintfLayouts(format).map(l => (l.kind === "scalar" ? l.storage : "aggregate")));
const memory = new SparseGuestMemory({ module, pointerBytes: 4 });
out(
  "printf",
  classicPrintf(memory, "%hd|%hhu|%lld|%I64x|%llu|%hx", [
    { kind: "int32", value: 70000 },
    { kind: "int32", value: -1 },
    { kind: "int64", value: -5n },
    { kind: "uint64", value: 0xfedcba9876543210n },
    { kind: "int64", value: -1n },
    { kind: "uint32", value: 0x12345678 },
  ]),
);
out("printf stray", classicPrintf(memory, "%5%z|%q|100%", [{ kind: "int32", value: 1 }]));

out("legacyClientCommand", [legacyClientCommand(["cs", "x"], "q3-1.16n-base"), legacyClientCommand(["cs", "13", "a"], "q3-1.16n-base"), legacyClientCommand(["cs", "21", "a"], "q3-1.16n-base")]);
out("lenient", [qvmEvent(200, "q3-1.16n-base", true, true), qvmEntityType(12, "q3-1.16n-base", true, true), qvmEntityType(13 + 200, "q3-1.16n-base", true, true), qvmPowerupBits(0x3ff, "q3-1.16n-base", true)]);
