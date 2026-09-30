# Q1 addon brush and visual peer review, 2026-09-29

This review inspects source only. No compiler, configuration, parser execution, test, executable, gameplay or benchmark was run. It does not accept complete Q1 behavior or the baseline.

## Inspected scope

Read complete `maps/addon_brushes.c` and `maps/addon_visuals.c` against complete donor `src/content/q1/addons/brushes.ts`, `lights.ts` and `rope.ts`. Inspected affected typed class/field/spawn/use/touch/blocked/reaction/think dispatch, action-kind predicates, pusher scheduling, retained frame roster reset/release, brush/visual map checkpoint words, frame roster codec validation, actor frame/skin/alpha and shared physics serialization. Inspected native frame entry, public brush callback entries, operation/destroy lifetime, helper allocation, actor snapshots and addon mover classification. The new unfrozen `special.c`, `monsters.c`, `triggers.c` packet is excluded; their unrelated implementation is not accepted here. Full checkpoints, all physics, navigation activation, cloning, all native gameplay and renderer light evaluation are outside this review.

Matched frozen module identities:

| Path | SHA256 |
| --- | --- |
| `maps/addon_brushes.c` | `649c8aa6e54ed07f013d8cb707fb44c98d4837b2da09e0624656b99be5004eb7` |
| `maps/addon_visuals.c` | `28cdfc2fb62376561bd131e2eb7c3c49265df4e9799b53774f62dd120e5731c1` |
| `maps/navigation.c` | `c4602fe048de475055e2aafd4d4d8a82ba2fb6c678cfa85ad0c61ec5810a8811` |
| `maps/runtime.c` | `1488eca9229d4b7f005108a8528a741373202e63553841310f9573aaf9ae784f` |
| `checkpoint/map.c` | `2aa673d24f555fc99d938ad5f7bc1c0a2ab3e6aeb97ab2c8ee74e5a1243aaf3f` |
| `checkpoint/codec.c` | `0d8086fe9a18b4abec9c0a32aaf718312e6bfe9a2f3157b08a4d49dbe40cb681` |
| `maps/internal.h` | `b0973c9c987120abf4ec69aa99a9c1ea9fee3b9b9a0948b6705d37598bd4d814` |
| `include/qa/game_q1_maps.h` | `ca049de044330972545a0c66f9a1a8f8c1d9487d1f0522e685cb223b90b5259a` |

Paths above are relative to `src/gameplay/q1`, except the public header.

## Confirmed finding

High, CONFIRMED by source trace. Native callback entry does not retain its owner through addon callbacks. `runtime.c:270` calls `q1_map_addon_frame` directly from the exported component `begin_frame`. A standalone registered native component has no required permanent application owner retention. While ramp frame work holds its native state and actor snapshot, `maps/addon_visuals.c:59` invokes the host lightstyle callback. That callback can call public `qa_q1_game_destroy`; with both operation and retention counts zero, destruction frees the game, map storage and snapshot. The next `visual(g,id)` at line61 and frame-loop snapshot release use freed memory. Snapshot borrowing alone does not guard native destruction.

The same missing admission appears through direct public brush use: `qa_q1_game_use_from` enters active bob work without an operation lease; `maps/addon_brushes.c:34` reads the canonical body through its external binding. A binding callback that destroys the native game returns to `brush(g,id)` at line36 with freed ownership. Public brush spawn, touch, blocked, reaction and pusher think expose related callback chains and must share an actual retained boundary. Application permanent retention masks this failure but does not repair the standalone public API. Sent the exact traces to the Q1 owner and root. Superseding repair is awaiting independent reread; this finding blocks acceptance of these callback lifetime paths.

## Superseding owner repair

Independently reread native `runtime.c` SHA256 `cb93de449141add4503d812f4336e254e1b78322c7bedbb99416f4a4d7138b4e`. Exported component frame/actor callbacks, scheduled and pusher think, and public spawn/template/touch/use/blocked/reaction now hold the existing typed operation lease over their actual callback chains. The shared finish preserves an actual callback failure, converts successful pending teardown to failure, and ends the lease without later accessing the reclaimed owner. The initial frame depth guard still rejects pool recycling during borrowed work. Scheduled temporary clock restoration occurs before lease end. Spawn rejects stale or retiring canonical admissions before allocating native state, and changed laser motion reacquires its native actor after a body callback. The two confirmed traces above are closed by these owner boundaries. No further confirmed defect was found in the bounded repair. Brush and visual module hashes remain unchanged. Their inspected source policy, dispatch and continuation paths are accepted within the explicit limits of this report; this is not all-entrypoint or whole-family acceptance.

The subsequent coordinated authored skin handoff adds checked signed `skin` decoding beside `frame` in application `map.c` SHA256 `f9193e5ae7d44e1ce6536e8f19dedfc261d679eb0a9c1965af12559dbb3d66cf`, consuming the actual landed `qa_q1_map_fields.skin`. The Q1 owner independently reread and accepted that exact consumer, actor skin publication and existing checkpoint serialization. The new corpse and ambient class packet remains excluded.

## Other disposition

No additional confirmed policy defect was found in the inspected brush/visual paths. Traced bob phase/solid toggles and blocked cooldown, toss/debris local pusher deadlines and later ordinary scheduling, shatter launch construction, explode damage/effect/target retirement checks, hurt actor filters, wall frame flipping, model once/loop polarity and immediate first loop tick, rotation acceleration and source negative-z behavior. Traced ramp initialization/toggle/clamped source patterns and rope original anchor, capped helper chain, source ordering and final bounds. Helpers are removed on the inspected allocation/model/body/link failures, and parent retirement is checked after callbacks.

The native frame roster preserves source order, removes full released identities and validates unique saved rows and allowed continuation kinds. Brush phase/origin/directions and visual chain/origin/phase use explicit native checkpoint fields; canonical body and actor presentation remain their existing owners. Authored skin admission was subsequently wired through the coordinated decoder/header handoff described above. Renderer dynamic light evaluation and runtime parity remain unverified.

Source whitespace checks on the two new modules produced no matches. Checks do not establish compiled or runtime correctness.
