/* Oracle for tests/app/test_mod_declarations.c: TS JSON.stringify of the three
 * gameplay-mod declaration types. Writes tests/app/oracles/mod_declarations.tsv,
 * one declaration per line: source TAB JSON input TAB JSON.stringify(read result).
 * Sources:
 *  - "fixture": every declaration the TS tests read, captured by the preload
 *    mod_declarations_capture.ts into <capture.tsv> (deduplicated);
 *  - "synthetic": the declarations below, written to reach every optional member,
 *    union branch and number spelling the readers have;
 *  - "reread": each result read again (the save/restore path reads the object
 *    a previous read produced).
 * Run from the TS checkout:
 *   cd ../quake-anthology-ts && bun ../quake-anthology-c/tests/app/oracles/mod_declarations.ts <capture.tsv> <out.tsv>
 * GPL-2.0-or-later. */
import { readFileSync, writeFileSync } from "node:fs";
import { parseGameplayModDeclaration } from "../../../../quake-anthology-ts/src/content/mods/declaration.ts";

const [capture, output] = process.argv.slice(2);
if (capture === undefined || output === undefined) throw new Error("usage: <capture.tsv> <out.tsv>");

const D = `sha256:${"ab".repeat(32)}`;
const D2 = `sha256:${"0f".repeat(32)}`;
const qcCall = (fn: string) => ({
  function: fn,
  arguments: [
    { kind: "input", name: "self" },
    { kind: "float", value: -0 },
    { kind: "float", value: 1e-7 },
    { kind: "vector", value: { x: 1.5, y: -2, z: 1e21 } },
    { kind: "string", value: "a\"b\\c\n\t\u0001\u001fé✓" },
  ],
  globals: [{ name: "g", value: { kind: "float", value: 0.1 } }],
});
const emptyQcCall = (fn: string) => ({ function: fn, arguments: [], globals: [] });
const statements = [
  { opcode: 1, a: -1, b: 2, c: 3 },
  { opcode: 0, a: 0, b: 0, c: 65535 },
];
const armorStage = (flags: "none" | "bits", scale: boolean) => ({
  function: "T_Damage",
  entry: 10,
  exit: 20,
  target: 1,
  damage: 2,
  saved: 3,
  ...(scale ? { regularScale: [{ caller: "T_Damage", statement: 12, scale: 0.6 }] } : {}),
  flags: flags === "none" ? { kind: "none" } : { kind: "bits", word: 4, noArmor: 1, noPowerArmor: 2, noRegularArmor: 4, energy: 8 },
  statements: [{ opcode: 1, a: 2, b: 3, c: 4 }],
});
const grip = {
  origin: { x: 1, y: 2.25, z: -3 },
  axis: [
    { x: 0, y: 1, z: 0 },
    { x: -1, y: 0, z: 0 },
    { x: 0, y: 0, z: 1 },
  ],
  scale: { x: 2, y: 2, z: 0.5 },
};
const quakec = {
  version: 1,
  runtime: "quakec",
  program: { path: "progs\\qwprogs.dat", digest: D },
  objectives: [
    {
      id: "x:flag",
      state: {
        storage: "flagstate",
        values: [
          { value: 0, stage: "home", complete: false },
          { value: 1.5, stage: "taken", complete: true },
        ],
      },
      carrier: { kind: "entity-field", global: "self", indirections: ["owner", "enemy"], field: "flag" },
      target: null,
      role: "owned",
      campaignGate: true,
      botGoal: false,
      change: qcCall("objective_changed"),
    },
    {
      id: "x:gate",
      state: { storage: { kind: "entity-field", global: "world", indirections: [], field: "gate" }, values: [{ value: 2, stage: "open", complete: false }] },
      carrier: null,
      target: "gate_target",
      role: "borrowed",
      writable: true,
    },
    {
      id: "x:owned-null",
      state: { storage: "s3", values: [{ value: 3, stage: "s", complete: false }] },
      carrier: null,
      target: null,
      role: "owned",
      campaignGate: false,
      botGoal: true,
      change: null,
    },
  ],
  actorFields: [
    { field: "health", binding: "health" },
    {
      field: "team",
      binding: "team",
      values: [
        { value: 1, team: "red" },
        { value: 2, team: null },
      ],
    },
    { field: "frags", binding: "score" },
    { field: "button0", binding: "client-input", input: "attack", update: "nonzero", scale: 0.5 },
    { field: "v_angle", binding: "client-input", input: "view-angles", update: "always" },
    { field: "flags", binding: "client-flags", grounded: true, privateMask: 4 },
    { field: "flags2", binding: "client-flags" },
    { field: "netname", binding: "userinfo", key: "name" },
    { field: "ammo_shells", binding: "inventory", item: "q1:ammo_shells" },
    { field: "k", binding: "constant", value: { kind: "vector", value: { x: 1, y: -0.5, z: 123456789012 } } },
    { field: "k2", binding: "constant", value: { kind: "string", value: "x" } },
    { field: "k3", binding: "constant", value: { kind: "float", value: 5e-324 } },
    { field: "origin", binding: "origin" },
  ],
  callbacks: [
    { id: "x:c1", ...qcCall("damage_fn"), operation: "damage", stage: "transform", result: "knockback" },
    { id: "x:c2", ...emptyQcCall("touch_fn"), operation: "actor.touch", stage: "replace", result: "boolean" },
    { id: "x:c3", ...emptyQcCall("give_fn"), operation: "inventory.give", stage: "observe" },
    { id: "x:c4", ...emptyQcCall("consume_fn"), operation: "inventory.consume", stage: "transform", result: "amount" },
  ],
  clientPresentation: { hud: "replace-vitals", view: "set-view" },
  clients: {
    maximum: 4,
    outputs: [
      { kind: "view-offset", field: "view_ofs" },
      { kind: "view-offset", height: "vh" },
      { kind: "movement-mode", field: "mt", mask: 3, values: [{ value: 1, mode: "noclip" }, { value: 2, mode: "freeze" }, { value: 0, mode: "normal" }] },
      { kind: "stance", field: "st", values: [{ value: 1, crouched: true }] },
      { kind: "body-shape", min: "mins", max: "maxs" },
    ],
    admit: [qcCall("admit")],
    userinfo: [],
    disconnect: [emptyQcCall("bye")],
    frame: [],
    input: [
      {
        scope: "client-command",
        phase: "before",
        calls: [emptyQcCall("pre")],
        outputs: [
          { kind: "field", field: "f" },
          { kind: "handler", function: "h", inputs: ["attack", "jump", "up-move"] },
        ],
      },
      { scope: "movement-slice", phase: "after", calls: [] },
      { scope: "movement-slice", phase: "before", calls: [], outputs: [] },
    ],
  },
  initialize: [],
  frame: emptyQcCall("StartFrame"),
  cvars: [],
  commands: [
    {
      name: "cmd",
      function: "Cmd_f",
      arguments: [
        { kind: "argument", index: 0, type: "string" },
        { kind: "argument", index: 1, type: "float" },
        { kind: "arguments-text" },
        { kind: "argument-count" },
        { kind: "vector", value: { x: 0, y: 0, z: 0 } },
        { kind: "string", value: "" },
        { kind: "float", value: 0.30000000000000004 },
      ],
      globals: [{ name: "g", value: { kind: "float", value: 3 } }],
    },
  ],
  protection: [
    {
      id: "x:armor",
      absorb: { kind: "function", call: qcCall("absorb") },
      admission: { kind: "replace-primary", owner: "q1:armor" },
      flags: { noArmor: 1, noPowerArmor: 2, noRegularArmor: 4, energy: 8, radius: 16 },
      channel: "regular",
      storage: {
        points: "armorvalue",
        item: null,
        selection: {
          field: "items",
          mask: 7,
          values: [
            { value: 1, item: "q1:item_armor1" },
            { value: 0, item: null },
          ],
        },
      },
    },
    {
      id: "x:armor2",
      absorb: { kind: "function", call: emptyQcCall("absorb2") },
      flags: { noArmor: 0, noPowerArmor: 0, noRegularArmor: 0, energy: 0, radius: 0 },
      channel: "regular",
      storage: { points: "armorvalue", item: "q1:item_armor2", selection: { field: "items", values: [] } },
    },
    {
      id: "x:power",
      absorb: { kind: "region", call: emptyQcCall("power"), stage: armorStage("bits", true) },
      admission: { kind: "claim" },
      flags: { noArmor: 1, noPowerArmor: 1, noRegularArmor: 1, energy: 1, radius: 1 },
      channel: "powered",
      storage: { cells: "ammo_cells", kind: "screen", selection: { field: "power", mask: 1, values: [{ value: 1, kind: "shield" }, { value: 0, kind: "none" }] } },
    },
    {
      id: "x:power2",
      absorb: { kind: "region", call: emptyQcCall("power2"), stage: armorStage("none", false) },
      admission: { kind: "replace-current-primary" },
      flags: { noArmor: 1, noPowerArmor: 1, noRegularArmor: 1, energy: 1, radius: 1 },
      channel: "powered",
      storage: { cells: "ammo_cells", kind: "shield" },
    },
  ],
  pickups: [
    { id: "p", offered: ["q1:a", "q1:b"], resource: { kind: "inventory", item: "q1:a" }, operation: { kind: "boolean-grant", grant: qcCall("grant") } },
    {
      id: "p2",
      offered: ["q1:c"],
      writes: [
        { kind: "protection", channel: "powered" },
        { kind: "inventory", item: "q1:d", fields: "capacity" },
        { kind: "protection", channel: "regular" },
      ],
      operation: { kind: "gate-then-grant", gate: emptyQcCall("gate"), grant: emptyQcCall("grant2"), grantAccepts: "always" },
    },
    { id: "p3", offered: ["q1:e"], resource: { kind: "protection", channel: "regular" }, operation: { kind: "gate-then-grant", gate: emptyQcCall("g"), grant: emptyQcCall("h"), grantAccepts: "nonzero" } },
  ],
  items: {
    definitions: [
      { item: "x:i", label: "I", icon: null, admission: "add", actions: { drop: emptyQcCall("drop") }, kind: "counter" },
      {
        item: "x:w",
        label: "W",
        icon: { kind: "wad-picture", path: "gfx.wad", lump: "INV_X" },
        admission: "replace-primary",
        actions: { use: emptyQcCall("use"), drop: emptyQcCall("drop") },
        kind: "weapon",
        held: { kind: "model", model: { path: "progs\\v.mdl", referenceFrame: 3, grip, digest: D, fallback: "progs/w.mdl", part: { digests: [D, D2], vertices: [0, 1, 7] } } },
        ammo: "q1:ammo",
      },
      { item: "x:w2", label: "W2", icon: { kind: "shader", name: "icons\\x" }, admission: "add", kind: "weapon", held: { kind: "none" }, ammo: null },
      { item: "x:w3", label: "W3", icon: { kind: "image", path: "gfx/x.lmp" }, admission: "add", kind: "weapon", held: { kind: "model", model: { path: "a.mdl", referenceFrame: 0, grip: { origin: grip.origin, axis: grip.axis } } }, ammo: null },
      { item: "x:c", label: "C", admission: "add", kind: "counter" },
    ],
    storage: [
      { kind: "bits", field: "items", privateMask: 0, items: [{ item: "x:w", mask: 1 }] },
      { kind: "counter", field: "ammo", item: "q1:ammo", capacity: { kind: "constant", value: 100 } },
      { kind: "counter", field: "a2", item: "q1:a2", capacity: { kind: "field", field: "max" } },
    ],
    weapons: {
      stage: {
        dispatcher: "W_Attack",
        continuations: ["a", "b"],
        repeats: [{ function: "r", entry: 1, exit: 2, result: { word: 28, value: 1 }, statements }],
      },
      selected: { field: "weapon", values: [{ value: 1, item: "x:w" }] },
      select: { field: "impulse", values: [], call: emptyQcCall("sel") },
      resume: [qcCall("resume")],
      model: { field: "weaponmodel", frame: "weaponframe" },
    },
  },
  combat: {
    damage: qcCall("T_Damage"),
    damageScale: { kind: "multiplier", function: "T_Damage", entry: 1, exit: 2, damage: 28, statements },
    armorStage: armorStage("bits", false),
    emptyArmor: { item: "q1:item_armor1", absorption: 0.3 },
  },
};
const quakecMinimal = {
  version: 1,
  runtime: "quakec",
  program: { path: "progs.dat", digest: D },
  actorFields: [],
  callbacks: [],
  clients: { maximum: 1, admit: [], userinfo: [], disconnect: [] },
  items: { definitions: [], storage: [] },
  combat: { damage: emptyQcCall("T_Damage"), damageScale: { function: "x", entry: 0, exit: 0, damage: 28, statements: [] } },
  objectives: [],
  protection: [],
  pickups: [],
};

const qvmCall = (entry: number) => ({
  entry,
  arguments: [
    { kind: "int32", value: { kind: "float", value: -1 } },
    { kind: "float32", value: { kind: "input", name: "amount" } },
    { kind: "vector", value: { kind: "vector", value: { x: 0.1, y: 0.2, z: 0.3 } } },
    { kind: "string", value: { kind: "string", value: "s" } },
    { kind: "actor", record: "gentity", input: "attacker" },
    { kind: "client", input: "self" },
    { kind: "time", input: "elapsed", units: "milliseconds", encoding: "int32" },
    { kind: "address", value: 4096 },
  ],
  globals: [{ address: 64, value: { kind: "int32", value: { kind: "float", value: 7 } } }],
  returns: "int32",
});
const emptyQvmCall = (entry: number, returns = "void") => ({ entry, arguments: [], globals: [], returns });
const qvmPointer = (kind: "argument" | "global") =>
  kind === "argument" ? { kind, index: 2, indirections: [0, 4], offset: 8 } : { kind, address: 1024, indirections: [], offset: 0 };
const qvmField = { record: "gclient", offset: 12 };
const qvmTest = (mask: number | null) => ({ field: qvmField, mask, comparison: "at-most", value: -3 });
const presentationCall = (entry: number, when: boolean) => ({
  entry,
  arguments: [
    { kind: "int32", value: -5 },
    { kind: "float32", value: 0.5 },
    { kind: "address", value: 12 },
    { kind: "source", value: "player-state" },
  ],
  ...(when ? { when: "weapon-presented" } : {}),
});
const program = (path: string) => ({ path, digest: D, abiProfile: "q3-modern" });
const presentationBase = {
  version: 1,
  gameplay: program("vm\\qagame.qvm"),
  cgame: program("vm/cgame.qvm"),
  initialize: [presentationCall(1, false)],
  refresh: [],
  frame: [presentationCall(2, true)],
};
const sceneStorage = {
  gameState: 100,
  time: [1, 2],
  frameTime: [3],
  viewOrigin: [4, 5, 6],
  viewAngles: [],
  serverCommandSequence: 7,
  centities: { address: 8, stride: 9, capacity: 10, state: 11, previousEvent: 12, snapshotTime: 13 },
};
const scene = {
  ...presentationBase,
  hud: { mode: "overlay", frame: [] },
  runtime: "qvm-scene",
  cvars: [{ name: "cg_x", value: "1" }],
  storage: sceneStorage,
  snapshots: [],
  eventEntityType: 13,
  eventCheck: { entry: 1, centityArgument: 0 },
  body: {
    player: { entry: 2, centityArgument: 1 },
    mesh: { entry: 3, entityArgument: 0, stateArgument: 1, shaderOffset: 64, parts: [{ call: 0, part: "lower" }, { call: 1, part: "head" }] },
  },
};
const playerEvents = {
  ...presentationBase,
  runtime: "qvm-player-events",
  storage: {
    gameState: 1,
    playerState: 2,
    snapshot: { kind: "synthetic-player-event", address: 3, pointers: [4, 5] },
    centities: { address: 6, stride: 7, capacity: 8, state: 9, origin: 10 },
    time: [11],
    frameTime: [12],
    viewOrigin: [13],
    viewAxis: [14, 15],
  },
  project: [presentationCall(3, false)],
  event: presentationCall(4, false),
};
const qvm = {
  version: 1,
  runtime: "qvm",
  program: { path: "vm/qagame.qvm", digest: D },
  abiProfile: "q3-1.16n-base",
  presentation: scene,
  spawnEntities: "G_SpawnEntitiesFromString",
  clients: {
    maximum: 8,
    outputs: [
      { kind: "view-offset", height: { record: "ps", offset: 4, encoding: "int32" } },
      { kind: "view-offset", field: { record: "ps", offset: 8 } },
      { kind: "stance", field: { record: "ps", offset: 12, encoding: "float32" }, mask: 1, values: [{ value: 1, crouched: false }] },
      { kind: "body-shape", min: { record: "ps", offset: 16 }, max: { record: "ps", offset: 28 } },
    ],
    records: ["gclient", "ps"],
    playerStateRecord: "ps",
    admit: [qvmCall(10)],
    userinfo: [emptyQvmCall(11)],
    disconnect: [],
    frame: [emptyQvmCall(12, "float32")],
    input: [
      {
        scope: "client-command",
        phase: "before",
        calls: [emptyQvmCall(13)],
        outputs: [
          { kind: "field", record: "ps", offset: 4, value: { input: "view-angles" } },
          { kind: "field", record: "ps", offset: 8, value: { input: "forward-move", encoding: "float32", scale: 0.25 } },
          { kind: "handler", entry: 14, actor: { record: "gentity", pointer: qvmPointer("argument") }, inputs: ["attack"], returns: { encoding: "int32", value: 1 } },
          { kind: "handler", entry: 15, actor: { record: "gentity", pointer: qvmPointer("global") }, inputs: [] },
          { kind: "command", entry: 16, actor: { record: "gentity", pointer: qvmPointer("global") }, command: qvmPointer("argument"), inputs: ["view-angles", "jump"] },
        ],
      },
    ],
  },
  entityRecord: "gentity",
  sourceActors: {
    allocate: 20,
    release: { entry: 21, argument: 0 },
    initialStores: [],
    inuse: 22,
    eventEntityType: 23,
    update: qvmCall(24),
    frame: {
      call: emptyQvmCall(25),
      clock: { address: 1, store: 2, argument: 3 },
      owned: [{ instruction: 4, localInstruction: 5 }],
      end: { instruction: 6, completedTaken: true },
    },
    callbacks: { touch: 1, use: null, pain: 3, die: null },
  },
  combat: {
    abi: "declared",
    entry: 30,
    health: 31,
    takedamage: 32,
    flags: 33,
    godmode: 34,
    noKnockback: 35,
    globals: [{ address: 36, value: { kind: "address", value: 0 } }],
    client: { pointer: 37, record: "gclient", health: 38, armor: 39, protection: 0.66, team: 40 },
    calls: {
      damage: {
        roles: { method: 7, flags: 6, amount: 5, point: 4, direction: 3, attacker: 2, inflictor: 1, target: 0 },
        extras: [{ index: 8, kind: "float32", value: 0.5 }],
      },
      touch: { roles: { trace: 2, other: 1, target: 0 }, extras: [] },
      use: { roles: { target: 0, other: 1, activator: 2 }, extras: [] },
      pain: { roles: { target: 0, attacker: 1, amount: 2 }, extras: [] },
      die: { roles: { target: 0, inflictor: 1, attacker: 2, amount: 3, method: 4 }, extras: [{ index: 5, kind: "address", value: 4 }] },
    },
    damageFlags: { radius: 1, noArmor: 2, noKnockback: 4, noProtection: 8, noTeamProtection: 16 },
    mass: { kind: "entity", offset: 41, storage: "float32" },
    teams: [{ value: -1, team: "q3:red" }],
  },
  protection: [
    {
      id: "x:armor",
      admission: { kind: "replace-primary", owner: "q3:armor" },
      absorb: emptyQvmCall(50, "int32"),
      flags: { noArmor: 1, noPowerArmor: 2, noRegularArmor: 4, energy: 8 },
      channel: "regular",
      storage: {
        points: { record: "ps", offset: 51, encoding: "int32" },
        item: null,
        selection: { field: { record: "ps", offset: 52, encoding: "int32" }, mask: null, values: [{ value: 1, selected: "q3:armor_combat" }, { value: 0, selected: null }] },
      },
    },
    {
      id: "x:armor2",
      admission: { kind: "claim" },
      absorb: emptyQvmCall(53, "int32"),
      flags: { noArmor: 1, noPowerArmor: 2, noRegularArmor: 4, energy: 8, radius: 32 },
      channel: "regular",
      storage: { points: { record: "ps", offset: 51, encoding: "float32" }, item: "q3:armor_body" },
    },
    {
      id: "x:power",
      admission: { kind: "replace-current-primary" },
      absorb: emptyQvmCall(54, "int32"),
      flags: { noArmor: 1, noPowerArmor: 2, noRegularArmor: 4, energy: 8, radius: 0 },
      channel: "powered",
      storage: { cells: { record: "ps", offset: 55, encoding: "int32" }, selection: { field: { record: "ps", offset: 56, encoding: "int32" }, mask: 3, values: [{ value: 2, selected: "shield" }] } },
    },
  ],
  items: {
    definitions: [
      { item: "x:i", label: "I", icon: { kind: "shader", name: "icons/iconr_red" }, admission: "add", actions: { use: emptyQvmCall(60) }, kind: "counter" },
      { item: "x:w", label: "W", admission: "replace-primary", kind: "weapon", ammo: "q3:ammo_x" },
    ],
    storage: [
      { kind: "counter", field: qvmField, item: "q3:ammo_x", capacity: { kind: "constant", value: 200 } },
      { kind: "counter", field: qvmField, item: "q3:ammo_y", capacity: { kind: "field", field: { record: "ps", offset: 1 } } },
      {
        kind: "counter",
        field: qvmField,
        item: "q3:ammo_z",
        capacity: { kind: "source", instruction: 61, overrides: [{ address: 62, comparison: "not-equals", value: -2, instruction: 63 }] },
      },
      { kind: "bits", field: qvmField, privateMask: 255, items: [{ item: "x:w", mask: 2 }] },
    ],
    weapons: {
      input: { entry: 70, clock: qvmField },
      stage: {
        dispatcher: { entry: 71, actor: { record: "gentity", pointer: qvmPointer("argument") } },
        predicates: [{ instruction: 72, unselected: true }],
        settled: [qvmTest(null), qvmTest(15)],
        selection: { field: qvmField, values: [{ value: 3, item: "x:w" }] },
        request: { entry: 73, argument: 1, accepted: [] },
        continuation: {
          entry: 74,
          actor: { record: "gentity", pointer: qvmPointer("global") },
          instruction: 75,
          originalTaken: false,
          when: [qvmTest(1)],
          predicates: [],
          projection: { movement: qvmPointer("global"), byteLength: 4, minimum: 0, maximum: 9, viewHeight: qvmField, ground: qvmField },
          calls: [{ instruction: 76, call: qvmCall(77) }],
        },
      },
    },
  },
  pickups: [
    {
      id: "p",
      offered: ["q3:item_health"],
      writes: [{ kind: "inventory", item: "q3:health", fields: "count-and-capacity" }],
      operation: { kind: "boolean-grant", grant: emptyQvmCall(80, "int32") },
      context: [{ record: "gentity", offset: 4, value: { kind: "time", input: "time", units: "seconds", encoding: "float32" } }],
    },
  ],
  objectives: [
    {
      id: "x:flag",
      state: { storage: { address: 90, encoding: "int32" }, values: [{ value: 0, stage: "home", complete: false }] },
      carrier: { kind: "global", address: 91, indirections: [4], offset: 8 },
      target: 92,
      role: "owned",
      campaignGate: false,
      botGoal: true,
      change: qvmCall(93),
    },
    {
      id: "x:flag2",
      state: { storage: { address: { kind: "global", address: 94, indirections: [], offset: 0 }, encoding: "float32" }, values: [{ value: 0, stage: "home", complete: false }] },
      carrier: null,
      target: null,
      role: "borrowed",
      writable: false,
    },
  ],
  actorRecords: [
    {
      id: "gentity",
      address: 4096,
      stride: 824,
      capacity: 1024,
      fields: [
        { offset: 0, binding: "health", access: "read-only", encoding: "int32" },
        { offset: 4, binding: "inventory", encoding: "int32", item: "q3:ammo_x" },
        { offset: 8, binding: "origin", access: "read-write" },
        { offset: 12, binding: "record", record: "gclient" },
        { offset: 16, binding: "constant", encoding: "float32", value: 2.5 },
        { offset: 20, binding: "constant-vector", value: { x: 1, y: 2, z: 3 } },
        { offset: 24, binding: "private", byteLength: 8 },
        { offset: 32, binding: "team", encoding: "int32", values: [{ value: 1, team: "q3:red" }] },
        { offset: 36, binding: "score", encoding: "int32" },
        { offset: 40, binding: "bounds-min" },
      ],
    },
  ],
  initialize: [qvmCall(100)],
  callbacks: [
    { id: "x:c1", operation: "damage", stage: "transform", result: "amount", ...qvmCall(101) },
    { id: "x:c2", operation: "actor.die", stage: "replace", result: "boolean", ...emptyQvmCall(102, "int32") },
    { id: "x:c3", operation: "actor.use", stage: "observe", ...emptyQvmCall(103) },
  ],
};
const qvmMinimal = {
  version: 1,
  runtime: "qvm",
  program: { path: "vm/qagame.qvm", digest: D },
  abiProfile: "q3-modern",
  presentation: playerEvents,
  entityRecord: null,
  combat: { abi: "q3-g-damage", entry: 1, health: 2, takedamage: 3, flags: 4, godmode: 5, noKnockback: 6, globals: [], client: null },
  sourceActors: { allocate: 1, release: { entry: 2, argument: 3 }, inuse: 4, eventEntityType: 5, update: null },
  clients: { maximum: 1, outputs: [], records: [], playerStateRecord: "ps", admit: [], userinfo: [], disconnect: [], input: [] },
  actorRecords: [],
  initialize: [],
  callbacks: [],
};

const address = (rva: number, indirections: number[] = []) => ({ rva, indirections });
const nativeCall = (name: string) => ({
  entry: { kind: "export", name },
  arguments: [
    { kind: "int8", value: { kind: "float", value: -1 } },
    { kind: "uint64", value: { kind: "input", name: "item" } },
    { kind: "float64", value: { kind: "vector", value: { x: 1e-7, y: 0, z: -0 } } },
    { kind: "string", value: { kind: "string", value: "n" } },
    { kind: "actor", record: "edict", input: "inflictor" },
    { kind: "client", input: "other" },
    { kind: "userinfo", input: "activator" },
    { kind: "time", input: "time", units: "seconds", encoding: "float32" },
    { kind: "address", value: address(4096, [0, 8]) },
    { kind: "address", value: null },
    { kind: "user-command" },
  ],
  globals: [{ address: address(32), value: { kind: "int32", value: { kind: "float", value: 2 } } }],
  returns: "int32",
  skips: [],
});
const emptyNativeCall = (rva: number) => ({ entry: { kind: "rva", rva }, arguments: [], globals: [], returns: "void" });
const armorField = (offset: number) => ({ record: "gclient", offset, encoding: "int16" });
const nativeTest = (kind: "scalar" | "pointer") =>
  kind === "scalar"
    ? { kind, field: armorField(4), mask: null, comparison: "equals", value: 1.5 }
    : { kind, field: { record: "gclient", offset: 8 }, value: null };
const native = {
  version: 1,
  runtime: "native",
  items: {
    definitions: [
      { item: "x:i", label: "I", icon: { kind: "image", path: "pics/i.pcx" }, admission: "add", actions: { use: nativeCall("Use") }, kind: "counter" },
      { item: "x:w", label: "W", icon: null, admission: "replace-primary", kind: "weapon", held: { kind: "model", model: { path: "models/v.md2", referenceFrame: 1, grip } }, ammo: "q2:shells" },
    ],
    storage: [
      { kind: "counter", field: armorField(0), item: "q2:shells", capacity: { kind: "constant", value: 100.5 } },
      { kind: "counter", field: armorField(2), item: "q2:cells", capacity: { kind: "field", field: armorField(4) } },
      { kind: "counter", field: armorField(6), item: "q2:slugs", capacity: { kind: "source", address: address(8, [1]), encoding: "uint8" } },
      { kind: "bits", field: armorField(10), privateMask: 3, items: [{ item: "x:w", mask: 4 }] },
    ],
    weapons: {
      dispatcher: { entry: { kind: "rva", rva: 1 }, record: "edict", argument: 0, arguments: 2 },
      decisions: [{ entry: 2, join: 3, fields: [{ field: armorField(12), clearMask: 1 }] }],
      committedInput: [],
      continuations: [[nativeTest("scalar"), nativeTest("pointer")], []],
      settled: [[{ kind: "scalar", field: armorField(14), mask: 7, comparison: "at-most", value: 2 }]],
      selection: {
        active: { record: "gclient", offset: 16 },
        pending: { record: "gclient", offset: 20 },
        values: [{ item: "x:w", address: address(24), request: emptyNativeCall(25) }],
      },
    },
  },
  program: { path: "gamex86_64.dll", digest: D },
  target: {
    api: { kind: "q2-rerelease-game", version: 2023 },
    abi: { kind: "windows-x86-64", image: "pe32+", pointerBytes: 8, call: "microsoft-x64" },
  },
  clientPresentation: { hud: "layout-overlay", view: "playerstate" },
  sourceActors: {
    allocate: { kind: "export", name: "G_Spawn" },
    release: { kind: "rva", rva: 4660 },
    update: { entry: { kind: "rva", rva: 1 }, returns: "void" },
    frameSeconds: 0.025,
    clock: [{ address: address(2), input: "frame", encoding: "int64", units: "milliseconds" }],
    callbacks: { abi: "q2-rerelease", touch: 1, pain: null, die: 3 },
    combat: {
      damage: { entry: { kind: "export", name: "T_Damage" }, abi: "q2-rerelease" },
      causes: { edition: "classic", game: "xatrix" },
      health: { offset: 1, encoding: "int32" },
      mass: { offset: 2, encoding: "int32" },
      takedamage: { offset: 3, encoding: "int32" },
      flags: { offset: 4, encoding: "uint32", invulnerable: 5, noKnockback: 6 },
      armor: {
        kind: "q2",
        regular: [{ item: "q2:armor_body", selection: { kind: "enum", field: armorField(7), value: 2, none: 0 }, points: armorField(8), normalProtection: 0.8, energyProtection: 0.6 }],
        power: [{ item: "q2:power_screen", kind: "screen", selection: { kind: "positive", field: armorField(9) }, cells: armorField(10), enabled: { field: armorField(11), mask: 8 } }],
      },
      deferred: { process: { kind: "rva", rva: 12 }, attacker: 13, inflictor: 14, blood: { offset: 15, encoding: "float32" }, knockback: { offset: 16, encoding: "int32" }, point: 17, mod: 18, receipt: 19 },
    },
    fields: { velocity: 20, ground: 21, use: null, think: 22, nextthink: { offset: 23, encoding: "float32", units: "seconds" } },
  },
  protection: [
    {
      id: "x:regular",
      admission: { kind: "replace-primary", owner: "q2:armor" },
      channel: "regular",
      storage: [{ item: null, selection: { kind: "positive", field: armorField(30) }, points: armorField(31) }],
      absorb: { abi: "q2-check-armor", entry: { kind: "export", name: "CheckArmor" }, flags: "q2-rerelease", globals: [], sparks: 2 },
    },
    {
      id: "x:power",
      channel: "powered",
      storage: [{ item: "q2:power_shield", kind: "shield", selection: { kind: "positive", field: armorField(32) }, cells: armorField(33), enabled: null }],
      absorb: { abi: "q2-check-power-armor", entry: { kind: "rva", rva: 34 }, flags: "q2-classic" },
    },
    {
      id: "x:call",
      admission: { kind: "claim" },
      channel: "powered",
      storage: [],
      absorb: { abi: "source-call", call: nativeCall("Absorb") },
    },
    {
      id: "x:region",
      admission: { kind: "replace-current-primary" },
      channel: "regular",
      storage: [],
      absorb: {
        abi: "source-region",
        call: emptyNativeCall(40),
        frame: { entry: 41, exit: 42, stackBytes: 43, argumentBytes: 44 },
        entry: 45,
        join: 46,
        result: { kind: "register", register: "rax", storage: "pointer" },
        inputs: [
          { target: { kind: "stack", offset: 8, storage: "int32" }, value: { kind: "client", input: "self" } },
          { target: { kind: "simd", index: 1, offset: 4, storage: "float32" }, value: { kind: "float32", value: { kind: "float", value: 1 } } },
          { target: { kind: "register", register: "r9", storage: "uint16" }, value: { kind: "user-command" } },
        ],
      },
    },
  ],
  pickups: [
    {
      id: "p",
      offered: ["q2:item_quad"],
      resource: { kind: "inventory", item: "q2:quad" },
      operation: { kind: "boolean-grant", grant: nativeCall("Pickup") },
      context: [
        { record: "edict", offset: 1, value: { kind: "float32", value: { kind: "float", value: 2 } } },
        { record: "edict", offset: 2, value: { kind: "address", value: address(3) } },
        { record: "edict", offset: 4, value: { kind: "time", input: "elapsed", units: "milliseconds", encoding: "int32" } },
      ],
    },
    { id: "p2", offered: ["q2:item_armor"], writes: [{ kind: "protection", channel: "regular" }], operation: { kind: "boolean-grant", grant: emptyNativeCall(5) } },
  ],
  clients: {
    maximum: 16,
    records: ["gclient"],
    admit: [{ ...nativeCall("ClientConnect"), accepts: "nonzero" }, { ...emptyNativeCall(6), accepts: "always" }],
    userinfo: [emptyNativeCall(7)],
    disconnect: [],
    command: [{ entry: { kind: "game-export", name: "ClientCommand" }, arguments: [], globals: [], returns: "void" }],
    input: [
      {
        scope: "movement-slice",
        phase: "before",
        calls: [emptyNativeCall(8)],
        outputs: [
          { kind: "field", record: "gclient", offset: 9 },
          { kind: "handler", entry: { kind: "rva", rva: 10 }, arguments: [{ kind: "user-command" }], inputs: ["side-move"] },
        ],
      },
    ],
    frame: [],
    endFrame: [emptyNativeCall(11)],
    outputs: [
      { kind: "movement-mode", field: armorField(12), values: [] },
      { kind: "view-offset", field: { record: "ps", offset: 13 } },
    ],
    pose: { viewHeight: armorField(14), crouched: { field: armorField(15), mask: 2 } },
    inputFields: [
      { record: "gclient", offset: 16, value: { kind: "vector", value: { kind: "vector", value: { x: 1, y: 2, z: 3 } } } },
      { record: "gclient", offset: 17, value: { kind: "time", input: "time", units: "milliseconds", encoding: "uint32" } },
    ],
  },
  cvars: [{ name: "deathmatch", value: "1" }],
  spawnEntities: null,
  entityRecord: "edict",
  objectives: [
    {
      id: "x:flag",
      state: { storage: { address: address(50, [2]), encoding: "int32" }, values: [{ value: 1, stage: "taken", complete: true }] },
      carrier: address(51),
      target: null,
      role: "owned",
      campaignGate: true,
      botGoal: true,
      change: emptyNativeCall(52),
    },
  ],
  actorRecords: [
    {
      id: "edict",
      base: { kind: "address", rva: 60, indirections: [0] },
      stride: 1024,
      firstSlot: 1,
      capacity: 1024,
      fields: [
        { offset: 0, binding: "address", value: address(61) },
        { offset: 4, binding: "address", value: null },
        { offset: 8, binding: "team", encoding: "int32", values: [{ value: 1, team: "q2:red" }, { value: 2, team: "q2:blue" }] },
        { offset: 12, binding: "score", encoding: "int16" },
        { offset: 16, binding: "health", encoding: "int32" },
        { offset: 20, binding: "inventory", encoding: "int32", item: "q2:shells" },
        { offset: 24, binding: "inventory-capacity", encoding: "int32", item: "q2:shells" },
        { offset: 28, binding: "record", record: "gclient" },
        { offset: 32, binding: "constant", encoding: "float64", value: -4.5 },
        { offset: 36, binding: "constant-vector", value: { x: 0, y: 0, z: 1 } },
        { offset: 40, binding: "private", byteLength: 4 },
        { offset: 44, binding: "angles" },
      ],
    },
    { id: "clients", base: { kind: "clients" }, stride: 512, firstSlot: 0, capacity: 16, fields: [] },
  ],
  initialize: [nativeCall("InitGame")],
  project: [],
  release: [emptyNativeCall(70)],
  callbacks: [
    { id: "x:c1", operation: "inventory.consume", stage: "transform", result: "amount", ...emptyNativeCall(71) },
    { id: "x:c2", operation: "actor.pain", stage: "observe", ...nativeCall("Pain") },
  ],
};
const nativeLegacy = {
  version: 1,
  runtime: "native",
  program: { path: "gamex86.dll", digest: D },
  target: { api: { kind: "q2-classic-game", version: 3 }, abi: { kind: "windows-i386", image: "pe32", pointerBytes: 4, call: "cdecl" } },
  sourceActors: {
    allocate: { kind: "rva", rva: 1 },
    release: { kind: "rva", rva: 2 },
    update: { entry: { kind: "rva", rva: 3 }, returns: "int32" },
    frameSeconds: 0.1,
    clock: [],
    combat: {
      damage: { entry: { kind: "rva", rva: 4 }, abi: "q2-classic" },
      causes: { edition: "rerelease" },
      health: { offset: 1, encoding: "int32" },
      mass: { offset: 2, encoding: "int32" },
      takedamage: { offset: 3, encoding: "int32" },
      flags: { offset: 4, encoding: "int32", invulnerable: 0, noKnockback: 0 },
      armor: { kind: "source", regular: [{ item: "q2:armor_jacket", selection: { kind: "positive", field: armorField(1) }, points: armorField(2) }], power: [] },
    },
    fields: { velocity: 1, ground: 2, use: 3, think: 4, nextthink: { offset: 5, encoding: "int32", units: "milliseconds" } },
  },
  poweredProtection: {
    id: "x:legacy",
    storage: [{ item: "q2:power_screen", kind: "screen", selection: { kind: "positive", field: armorField(3) }, cells: armorField(4), enabled: null }],
    absorb: { abi: "q2-check-power-armor", entry: { kind: "export", name: "CheckPowerArmor" }, flags: "q2-classic", globals: [{ address: address(5), value: { kind: "address", value: null } }] },
  },
  pickups: [],
  clients: { maximum: 1, records: [], admit: [], userinfo: [], disconnect: [], command: [], frame: [], outputs: [], inputFields: [] },
  cvars: [],
  spawnEntities: "SpawnEntities",
  entityRecord: null,
  objectives: [],
  actorRecords: [{ id: "edicts", base: { kind: "entities" }, stride: 4, firstSlot: 0, capacity: 1, fields: [] }],
  initialize: [],
  project: [],
  release: [],
  callbacks: [],
};
const nativeNone = {
  ...nativeLegacy,
  poweredProtection: undefined,
  sourceActors: {
    ...nativeLegacy.sourceActors,
    combat: { ...nativeLegacy.sourceActors.combat, armor: { kind: "none" } },
  },
};

const lines: string[] = [];
const seen = new Set<string>();
const emit = (source: string, input: unknown): void => {
  const text = JSON.stringify(input);
  if (seen.has(text)) return;
  seen.add(text);
  const result = parseGameplayModDeclaration(new TextEncoder().encode(text));
  const written = JSON.stringify(result);
  lines.push(`${source}\t${text}\t${written}`);
  if (!seen.has(written)) {
    seen.add(written);
    lines.push(`reread\t${written}\t${JSON.stringify(parseGameplayModDeclaration(new TextEncoder().encode(written)))}`);
  }
};
for (const line of readFileSync(capture, "utf8").split("\n")) {
  if (line.length === 0) continue;
  const [, input] = line.split("\t");
  emit("fixture", JSON.parse(input!));
}
const fixtures = lines.length;
for (const declaration of [quakec, quakecMinimal, qvm, qvmMinimal, native, nativeLegacy, nativeNone]) emit("synthetic", declaration);
writeFileSync(output, `${lines.join("\n")}\n`);
console.log(`${lines.length} declarations (${fixtures} from fixtures and their rereads)`);
