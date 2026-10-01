# Frontend baseline handoff — 2026-09-30

Current role: `/root/frontend_baseline`, GPT-6.1 Sol/high. Root requested an immediate implementation freeze for a new session. No further production edits are authorized in this session. The global execution gate is CLOSED: no compilation, configuration, builds, tests, game runs, scripts, parsers, generators or syntax checks. Only source reads, hashes and whitespace checks were used. No descendants were spawned.

## Current cvar seam: incomplete and not independently accepted

Exclusive scope granted by root: `src/console/cvars.c`, `src/console/cvars_private.h`, and new `include/qa/console_cvar_observer.h`. Generic `include/qa/console.h` was not edited. `q3_settings_owner` owns the actual frame-time mirror implementation; it has not implemented the mirror legs yet. No cvars-save source was edited.

Exact current production hashes:

| Path | SHA256 |
| --- | --- |
| src/console/cvars.c | e1433294e0fd12246c259d468f0a8bffbcb6ff112ea1feffd5f168fdd6d135b8 |
| src/console/cvars_private.h | 99e3b4389e0a3c5db9b503f41f02fec9f7217286c4c94a4d793843a263400a66 |
| include/qa/console_cvar_observer.h (NEW) | 91571fad914832f92b9d652eac6e9ba66734a38e7084e47691e9f9330cd894f1 |

On disk now:

- Typed numeric `qa_cvar_observer_token` and bool callback `(void *, qa_cvars *, const char *, qa_error *)`; observe, unobserve, admission-time suppress and observer-idle APIs.
- Private registration-ordered observer list with owned canonical name, owner/token/user/callback, active/suppressed flags and pending reference counts. Private FIFO events retain an exact observer fanout snapshot, never cvar views.
- Helpers reserve fanout before actual value publication, enqueue and release it, then drain all admitted callbacks while retaining the first error. Inactive/removed observers are skipped; suppression is tested when fanout is admitted.
- Helpers `mutation_begin`/`mutation_end` exist but **NO production public mutation entrypoint calls them yet**. They are presently unused. Consequently **the FIFO does not drain**, so this partial implementation must not be treated as usable or complete. An admitted observer publication currently leaves pending events, and the new destroy idle guard refuses that registry. Finish the wrappers before any consumer can be admitted.
- Actual `binding.changed` publication sites now prepare post fanout and preserve original changed callback behavior. These include accepted equal writes and Q2 console/equal forced latch clearing. Latch-only staging and registration/metadata do not emit value events.
- Ordinary callback fences added around print/effect/validate/changed/cheats-policy/command-exists callbacks; `qac_cvars_touch` rejects mutation while notifying. Registry destruction refuses active mutation/notification/drain/pending events. Source owner removal detaches matching observer owners before variable deletion.
- Source whitespace check passed at this boundary. No independent whole-owner review, compiler or execution qualification occurred. Do not infer that the public header contract is implemented simply because it is declared.

## Agreed source contract and donor evidence

Read complete current C cvar owner, private struct, callback API and relevant scalar persistence producer. Donor read: `/home/buzzkill/Projects/quake-typescript/src/core/cvars/mirror.ts`, `core/cvars/index.ts` publication sites (especially 448–518), and `app/bootstrap/frame-time.ts`. TypeScript skill was applied while reading the donor.

Actual SharedCvarMirror has one coalesced owner/name subscription, registration-ordered client fanout, and one mirror binding per admitted name. Any owner value publication refreshes ALL admitted names for each client. Mirror writes force-set the real owner unless that client is refreshing. Close removes clients and tears down an empty subscription. Registries must be distinct but share session and dialect. Bind-slot collision semantics remain genuine: the mirror writer will still reserve actual `qa_cvars_bind` slots with a validation callback and NULL changed callback; the new observers provide the safe propagation leg.

`q3_settings_owner` explicitly confirmed:

- Per-name events correspond exactly to actual existing binding.changed points, including accepted equal values.
- Do not skip equal forced writes: Q2 force/console equal writes clear genuine pending latch storage; Q3 equal accepted values still notify.
- Notify only after the whole outer mutation publishes flags, effects and pending-storage cleanup. Ordinary callback notification remains read-only.
- Post callbacks may mutate either registry safely. Nested publications append FIFO and the originating outer bool setter drains synchronously before returning. Registry destruction stays fenced throughout.
- Suppression must exclude a token at EVENT ADMISSION during the entire mirror refresh. Merely checking refreshing at later callback dispatch causes delayed equal writes to feed back after refreshing becomes false.
- Callback receives a borrowed canonical name and actual registry; resolve the current view afresh. Never keep a borrowed cvar/string pointer across mutation.
- Removed tokens cancel future pending invocations; retained event refs keep token/name storage alive until drain release. Observer registration order is stable and tokens are never reused within their borrowed registry.
- Release mirror observers before physical source lease/registry retirement. Parent source.c still needs a genuine physical-current lease predicate independent of EXIT-clock state; a client effect accessor alone does not qualify GAME-frame registry updates.

## Next concrete production work

1. Complete mutation entrypoint wrappers in cvars.c. Every relevant public bool/void mutation must call begin/end around the actual implementation, including nested register/set/full_set/set_flags/apply_latched/restart/cheat/reset calls. Nested internal public calls must defer drain until the outer operation returns. Preserve current return/error/partial-publication semantics and ordinary numeric/latch behavior. Do not introduce a frame-delayed queue or equality shortcut.
2. Read the complete changed owner again for callback lifetime, admission allocation failures, removal/re-registration, fanout cancellation, partial failure and FIFO ordering. The observed subscription must be retired coherently when its actual source owner disappears. Require an independent full source review after a verifiable frozen packet.
3. Coordinate actual `qa_cvars_observer_idle` with the real cvar capture/restore owner and app teardown. Current cvars_save.c preserves installed bindings while exchanging private scalar records and never runs changed callbacks during decode; it currently does not use the new idle seam. Root must authorize any changes there. No silent observer replay or reset of saved scalars during candidate import.
4. Mirror owner implements real coalesced owner/name and per-role mirror tokens, whole-name admission suppression, physical-current qualification, close/partial-constructor cleanup and candidate service identity. New header has been sent, but mirror owner has NOT independently read/accepted the partial owner implementation.

## Accepted read-only seat review

`/tmp/frontend-seat-components-20260930.sha256` final five-file packet was independently SOURCE ACCEPTED after two concrete defects were repaired by frontend_resume. All five final hashes were checked twice and scoped whitespace/new-file trailing whitespace checks passed. No production edits were made by this reviewer to that packet.

| Path | SHA256 |
| --- | --- |
| src/app/frontend/seat_inventory.c | d786ddf81002d45c846dc7eda14f2b112aafee0685de58cdc2e19b8ea05271ea |
| src/app/frontend/seat_inventory.h | 02a823653570cc7dbf123407f6f375c59bb27bd81a0dc07410d4ebad0dbfec5b |
| src/app/frontend/seats.c | 714021c4874c376e8b63342dbc8eb309644d4491ec1ad3d2c970c8ed54badff3 |
| src/app/frontend/menu_save.c | 938c9e8466d6c5a0f34296cc6d99b2a61854aa7352a8ce6c9a8ab8b29e68283d |
| src/app/frontend/menu_save.h | 5da7ada78062f386b3190f71e85db9ca0f978f7c1b29913d926e8c71186f745b |

Repaired findings:

- Normal frontend constructs Assistance before lazy Mods, while the original candidate constructor reversed these. Actual UI codec validates physical menu registration order. Restored constructor now registers Assistance before optional Mods.
- Reservation wire framing originally used native pointer-containing struct sizes, making typed records ABI-dependent. It now stores one schema-defined zero cell per reserved slot; native sizeof is only an allocation overflow check. Real label storage uses its actual byte extent.

Bounded review traced full represented helper/menu owners, input-before-UI token dependency, all actual installed service/font/catalog/LLM references, builder/actor/sequence/control producers, scratch refills, retained HUD/icon ownership, wheel selector provenance, isolated nested import and partial teardown. No remaining confirmed defect in those represented sections. Actual retained Q2 player projections (`player_events.c` view/timer/help/scores/inventory) are a separately assigned required owner. Full frontend outer save/load caller, candidate and no-fail publication are still unfinished and were NOT accepted by this review. Baseline remains open.

