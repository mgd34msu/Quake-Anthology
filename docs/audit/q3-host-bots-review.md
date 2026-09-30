# Q3 host bot integration review

Source-only review by `/root/native_q2`, 2026-09-27. The host packet was frozen by
its author, `/root/native_q3`. No compiler, build, test, project executable,
generator, or benchmark was run. Repository HEAD at the snapshot was
`437e22196025b59040d1fab05f304d9c00ce6ea6`; the reviewed host files were uncommitted.
This review does not establish B23 or B26 completion.

## Coverage

Every body in the following eight files was read. Donor comparisons included
`compat/qvm/{bot-library-syscalls,legacy-bot-syscalls,bot-navigation-syscalls,bot-navigation-records,memory}.ts`,
the reached library actions/goals/weights/weapons/BSP/character implementations,
`bots/behavior/q3/{library,navigation}.ts`, the shared navigation runtime and
estimate implementations, and the application's guest-bot and movement-prediction
adapters. Shared C declarations and reached owner implementations were inspected
for admission, callbacks, and lifetime.

| File below `src/compat/q3_host/` | SHA-256 before corrections |
| --- | --- |
| `bot_actions.c` | `097ac31b479928c04d7c33b304ad171265e8b34ae82972acbdcb85382f6c809e` |
| `bot_library.c` | `97e70500a52562f689cd98763e4ee1b63e21b1e6edce88a7d45c1e7f7b0bc4a4` |
| `bot_navigation.c` | `7594011f4f2491fe483cc559179900c4ed8f237c892cbc495bf4d994761f9553` |
| `bot_records.c` | `8c7b90e035a9f83501101ab4d0b265c97e80c3a3e6c28658b4f8445caf7939bc` |
| `bot_genetic.c` | `bc8802b2f35554f3bc6b2d956795e7bfb5ea7c4f9c86a0d2eed5079a844cc18a` |
| `bot_goals.c` | `b21a6d54e16ac8edcd93509c8dcf16afeb5af46f79cbc32cc2bdbc7ad078496b` |
| `bot_weapons.c` | `f17af2bab4fbb45bcc179c1196cc3f23d1ca8a62793f4421b2a43a59665ec385` |
| `bot_records.h` | `6113faeba14f60f5984d0c3327dc71e6996e66197a68d64e7dc8f69b4db39f0e` |

Chat adapters received a separate earlier bounded review. Movement imports
548–557/572/574 are not implemented in this packet and remain open. Other host
services are under the root's separate review. Adjacent script shutdown paths
were traced for the finding below, not reviewed as a complete script subsystem.

## Findings sent to the author

- **HB-01: modern EA output admission changes state on failure.**
  `bot_actions.c:64` calls `qa_bot_actions_input` before validating the output.
  Modern donor `bot-library-syscalls.ts:86` admits its 40-byte destination before
  `getInputBytes` writes `thinkTime`. Admit the destination first for the modern
  profile. The legacy source explicitly performs the think-time write first;
  retain that profile distinction.
- **HB-02: signed fuzzy inventory displacement is rejected.**
  `bot_records.c:q3_bot_inventory_read` rejects every negative index. Donor
  `bot-library-syscalls.ts:42` uses `guest.view(base,4,index*4)`, whose signed
  relative offset can remain inside the VM allocation. A switch index parsed as
  `0xffffffff` becomes -1 in the shared fuzzy parser. Preserve checked signed
  displacement, null-base rejection, and final allocation bounds.
- **HB-03: AAS trace crossing capacity loses supported output.**
  `bot_navigation.c:areas` limits crossings to `graph.node_count`. Donor
  `aas.ts:124` emits every visited non-solid leaf, including repeated references
  to an area or shared acyclic subtree. The validated native AAS query likewise
  retains those crossings. Preserve the requested maximum for AAS traces;
  unique-area bounds remain valid for bounding-box queries and foreign graphs.
- **HB-04: negative AAS trace maximum is silently accepted.**
  `areas` converts negative maximum to zero. Donor AAS and foreign trace queries
  reject it after trap308 clears the first output word. Retain that mutation and
  then reject the invalid maximum before reading unreached vectors.
- **HB-05: a short trace-point output is partially overwritten.**
  `areas` writes optional point components through `q3_write_vector` without
  first admitting all 12 bytes. Donor trap308 admits the complete point span
  before any component store. An output with only eight bytes remaining must
  fail without publishing x/y; the earlier area-number write remains committed.
- **HB-06: foreign navigation flag8 is mistaken for disabled routing.**
  Trap300's read-only query infers enabled state from `area.flags & 8`. Donor
  `runtime.enableArea` applies the default flag8 disable only to AAS-origin nodes;
  foreign NAV flags have separate meanings. Use canonical
  `qa_navigation_enabled`, which also owns explicit overrides. The setting path
  already uses that shared policy.
- **HB-07: eager AAS vector admission consumes fields the source skips.**
  Trap316 reads origin before checking zero or unknown start/goal areas, while
  donor `selectedEstimate` and `NavigationEstimates.estimate` return unreachable
  before origin reads. Trap308 with maximum0 on an AAS graph does not consume
  either vector because its BSP loop never runs. Broader checked-reader support
  remains needed where source route outputs retain a live origin reference;
  detached values cannot preserve overlapping guest reads/writes. The shared
  navigation/movement owner will provide those readers, without another host
  routing algorithm.
- **HB-08: parser lifetime is closed too early during bot shutdown.**
  Trap201 first closes the runtime, then reports and closes individual parser
  handles. Donor `library.ts:153` reports all currently open handles before
  disposing the source table; source operations remain live during those
  diagnostics. The host's adjacent `scripts.c` also gates independent
  `PC_ADD_GLOBAL_DEFINE` behind the closed runtime, though donor trap204 directly
  uses its separate global-definition owner. Preserve the source lifetime and
  recheck handles after callbacks. Removing the gate alone would expose stale
  local script pointers; the current gate prevents that specific use-after-free.

The author corrected HB-01 through HB-06 and the concrete admission cases in
HB-07. Focused rereview accepted those corrections: modern-only destination
admission, checked signed displacement, full AAS crossing capacity, negative
limit rejection after the initial clear, whole point-span admission, canonical
enabled lookup, and skipped origin reads on invalid route areas/AAS maximum0.
HB-08 was corrected with a transient source-reporting phase: global defines stay
independent, parser operations remain available during diagnostics, and a fresh
table walk disposes handles only after all reports. The reviewer reread those
changes and found no additional demonstrated defect in these branches.

The broader live-field work in HB-07 remains open. A retained shared crossing
collector is also required to replace the host's allocation proportional to an
untrusted maximum with storage proportional to actual trace output; removing the
incorrect node-count cap established correctness, not the final memory policy.

## Movement adapter follow-up

The subsequently authored `src/compat/q3_host/bot_movement.c` was read in full,
with its `host.c` dispatch and `internal.h` declaration, against donor navigation
traps548–557/572/574, movement-state handle admission, and the lazy record
references. The 68-byte initialization and 52-byte movement-result offsets,
six initial clears, untouched result tail, null-goal checks, source handle
diagnostics, per-word bounds, and typed lazy read/write adapters matched the
reviewed contracts. This remains source-only evidence.

Two shared query obligations remain open. Trap553 currently selects a client
navigation facade, whereas donor `reachabilityArea` uses the base runtime and
uses its client argument only to exclude that entity from the first trace. It
also eagerly snapshots origin across callbacks. Trap554's shared controller
currently selects the movement-state client's runtime, whereas donor
`movementViewTarget` does not enter `withClient`. These need explicit module
query behavior while native controller queries retain their selected actor.
Trap572's base-client -1 selection is correct. The broader route576 live-origin
result overlap remains part of HB-07.

## Refuted candidates and limits

- Goal location/item/camp traps eagerly read their complete prior 56-byte goal
  records in the donor dispatch. The native eager reads are appropriate there;
  movement goals have a different, lazy contract.
- Trap575's goal-position vector is unused by the donor's actual alternative
  route algorithm. Its omission does not remove a consumed field.
- The donor carries `visualize` through movement prediction but the reached
  application/projector never consumes it. No demonstrated visualization loss
  was established from the missing native query member.
- Legacy EA flag mapping, 112-byte entity update, 140-byte entity info,
  56-byte goals and partial goal writes, 84-byte movement prediction,
  36-byte route prediction with untouched padding, 24-byte alternative goal
  stride, and typed genetic staged outputs were compared with the donor.
- The canonical projectile model offset80 in the native 552-byte weapon codec
  intentionally corrects the donor parser's erroneous offset88 model write.
  It is a native functionality correction, not an exact donor-byte claim.
- Resource handles and runtime shutdown semantics are still active B26 work.
  This packet does not certify native bot decisions, rerelease behavior,
  checkpoints, application composition, or arbitrary post-shutdown bot calls.
