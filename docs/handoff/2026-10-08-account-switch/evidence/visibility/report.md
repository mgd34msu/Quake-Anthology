# THE-2859 Q2 visibility handle tail

Frozen source: `src/app/application/providers.c` and `visuals.c`; two changed lines. Root supplied `internal.h:87` (`qa_cvar_handle sv_novis`). `source.patch` includes all three hunks. Both owned files remain byte-equal to `after/`.

The existing provider cold bind resolves `sv_novis` from the exact source registry returned by `application_guest_console_at`. Visibility reads that handle from the same registry, retains the floating `number != 0` test and OR with the caller override, and performs no warm name lookup. No additional cache, API, allocation, VM handle registration, validator or serializer was added.

## Source identity and lifetime

- `visuals.c:194` and `native_q2_presentation.c:221` both select `application_world_provider(app, QA_ROLE_ENTITIES, "")`.
- `guest_q3_exports.c:47` dispatches builtin Q2 to the real `native_q2_console.c:519` owner accessor; original Q2 dispatches directly to `engine->cvars` at `guest_q3_exports.c:88`.
- Builtin GAME/SERVER edition views are created at `native_q2_console.c:349`; original GAME/SERVER edition views at `guest_native_q2.c:310`. The binder runs after successful construction at `providers.c:1516`. Fresh replacement providers get that same cold bind.
- Registry pointers are assigned only at those constructors. Builtin restore at `native_q2_console.c:536` and common scalar restore at `save.c:1775/1805` use `qa_cvars_save_commit`; `cvars_save.c:268` publishes an edit into the existing registry. Handles survive scalar publication; a genuinely replaced source view requires a cold bind.
- `providers.c:1557` is the separate existing original Q3 restored-construction bind, also retained. No constructor or restore route was added.

## Executed component proof

Run `python run-checks.py` in this packet. GCC before/after, Clang after, and GCC ASan/UBSan after all compile and exit 0. The exact 5,204-byte visibility traces match. `checks.json`, `*-trace.txt` and `*-run.log` record the results. Full owned production translation units additionally pass GCC and Clang strict `-Werror` syntax checks (`syntax-checks.json`).

The component uses the exact extracted before/after binder, actual guest-console dispatcher and real builtin accessor/private console type, actual source registry views, actors/world links, visibility preparation/selection, PVS/area logic, and retail `base1.bsp` (external fixture path, never copied to the repo). DLL/game invocation and presentation selection are explicit fixture seams; unrelated module callbacks abort if reached. Retained static world/content/core/data/network libraries are linked; the source portions and cvar implementation used by the component are retained in this packet. This is not a live native-module launch or full save round trip.

For classic and rerelease, native and original source seams, 32 distributed linked actors exercise real culling: default accepts 5/31 other actors and rejects 26. Rerelease accepts all 31 with `0.5`, `-1`, prepared `1`, published `1`, or the caller override. Abort restores the original 5. Source replacement retains the published common value; setting the new source back to zero again yields 5. Existing classic behavior ignores this particular override in its Q2 visibility test and stays at 5; that behavior is preserved. `behavior-summary.json` records each route. VM declaration handle count is unchanged by the cold bind.

During 600 actual visibility preparations for each of four source/edition routes (2,400 total): before name reads 2,400; after name reads 0; before/after resolves 0 and heap calls 0. These are bounded workload counters, not frame-time speedup claims. Initial unlinked-actor fixture results were discarded; final traces use actual world links and visible/culled actors.

## Original behavior and limits

Classic PVS/area reference is `qsrc/quake-2/server/sv_ents.c:475` (fat PVS), `:522` (client frame), and `:591` (area filtering). `sv_novis` is not a classic Q2 qsrc cvar; the generated table records a Q2R source entry at `cvar_catalog_generated.c:18034`. This slice preserves existing engine behavior rather than claiming that cvar is stock classic functionality.

No usercmd/protocol widths, serializers, netchan, packet checksums or handshakes changed. No legacy connection, input/audio/gameplay qualification or timing claim is made. Root owns production builds, installation and the full THE-2859 cadence.

One other named `sv_novis` read exists at `network_q2_frame.c:664` (`owner->host.cvars`), outside the assigned two-file slice. It was reported to root; this packet does not claim all engine cvar reads are gone.
