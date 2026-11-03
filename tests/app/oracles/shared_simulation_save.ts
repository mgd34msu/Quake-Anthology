// Oracle for tests/app/test_shared_simulation_save.c: encodes the save image
// fixture (tests/app/oracles/shared_simulation_save_image.json) with the TS
// save-image codec and prints length and FNV-1a 64 of each encoding. Run from
// the C repository root: bun tests/app/oracles/shared_simulation_save.ts
import { decodeSaveImage, encodeSaveImage, parseSaveImage } from "../../../../quake-anthology-ts/src/persistence/save-image.ts";
import { decodeCheckpointValue, encodeCheckpointValue } from "../../../../quake-anthology-ts/src/persistence/value.ts";

const fnv = (bytes: Uint8Array): string => {
  let hash = 0xcbf29ce484222325n;
  for (const byte of bytes) hash = ((hash ^ BigInt(byte)) * 0x100000001b3n) & 0xffffffffffffffffn;
  return hash.toString(16).padStart(16, "0");
};
const show = (name: string, bytes: Uint8Array): void => console.log(`${name} ${bytes.length} ${fnv(bytes)}`);
const text = await Bun.file("tests/app/oracles/shared_simulation_save_image.json").bytes();
const image = parseSaveImage(decodeCheckpointValue(text));
const parsed = encodeSaveImage(image);
show("parsed", parsed);
// checkpointFromSource builds the record in this key order.
const { recipe, frame, nextEventSequence, consumption, clocks, random, actors, bodies, combat, inventories, configurations, thinks, providers, guests } = image;
// captureSharedBodies writes { actor, body, linkCount, attachment, linked }.
const captured = bodies.map(body => ({ actor: body.actor, body: body.body, linkCount: body.linkCount, attachment: body.attachment, linked: body.linked }));
const checkpoint = { schemaVersion: 4 as const, recipe, frame, nextEventSequence, consumption, clocks, random, actors, bodies: captured, combat, inventories, configurations,
  thinks: thinks.map(think => ({ actor: think.actor, callback: think.callback, ...(think.executionProvider === undefined ? {} : { executionProvider: think.executionProvider }),
    due: think.due, boundary: think.boundary, provider: think.provider, sequence: think.sequence })), providers, guests };
show("checkpoint", encodeSaveImage(checkpoint));
show("redecoded", encodeSaveImage(decodeSaveImage(parsed)));
// A version 3 envelope (no header, schemaVersion 3, no consumption) migrates to an empty record.
const legacy = JSON.parse(new TextDecoder().decode(text)) as Record<string, unknown>;
legacy.schemaVersion = 3; delete legacy.consumption;
const body = encodeCheckpointValue(decodeCheckpointValue(new TextEncoder().encode(JSON.stringify(legacy))));
const v3 = new Uint8Array(8 + body.length); v3.set(new TextEncoder().encode("QTSAVE3\n")); v3.set(body, 8);
show("v3", encodeSaveImage(decodeSaveImage(v3)));
