# Presentation and application review, 2026-09-29

This contributor reviewed existing code and fixed the confirmed defects below.
The work preserves the existing uncommitted application, material-order and Q3
presentation implementation. It does not claim completion of AUDIT, BASELINE,
application integration, or runtime qualification.

Model: GPT-6.1 Sol, high effort. No descendants were spawned.
The runtime did not expose a trusted external session UUID to this contributor,
so no Jev session identity or claim was invented. The coordinator tracks this
contribution under AUDIT work `w_357ddd74810a4aa8a3bb84fdd7095fb8`.

## Confirmed defects and repairs

| ID | Severity | Current location | Failure trace | Repair |
|---|---|---|---|---|
| PA01 | High | `src/app/application/lifetime.c:155`, `owner.c:381` | A safe faulted session frees itself and returns false from `qa_session_destroy`. The application formerly retained that freed pointer after the error, so retrying application destruction accessed the freed session. | Use the session owner's new `qa_session_destroy_ready` predicate before cleanup and again immediately before destruction. Once ready destruction is called, clear the consumed session and world pointers before handling the error. A later application-destroy retry finishes remaining resources with a NULL session. Admission or active-call rejection leaves the session live. |
| PA02 | High | `src/app/application/control.c:1120`, `services.c:852` | The first cutscene update saved the original movement mode. The second update sent RESET through the generic control discontinuity callback, which cleared `saved_mode_valid`; the update froze movement again, so cutscene end could not restore the original mode. | Split motion-record publication from ordinary control-reset synchronization. Cutscene publication uses the shared record helper and performs its own existing pose update. Ordinary builtin discontinuities still call both publication and control synchronization. |
| PA03 | Medium | `src/app/application/control.c:1175`, `1205` | Ending a cutscene restored `view_offset` but left `view_height` at the cinematic height. The control camera view exposed both fields, so its height remained inconsistent after release/reset. | Restore `view_height` from the saved view offset alongside restoring that offset. |
| PA04 | Medium | `src/app/application/control.c:821` | Frozen commands bypassed dialect and increasing-sequence checks. A stale command could lower `command_sequence` during a cutscene, allowing a previously processed sequence after release. | Apply the existing dialect and sequence admission before the frozen-command branch. |
| PA05 | High | `src/app/application/control.c:864` | A direct movement call outside session advancement left the application IDLE while retaining mutable control, provider and result pointers. Synchronous movement callbacks could enter application lifecycle operations while that outer call was live. | Hold APPLICATION_ADVANCING over the movement mutation interval, reject calls during other lifecycle operations, and restore the exact prior IDLE/ADVANCING operation on the single exit after acquisition. |
| PA06 | Medium | `src/presentation/q3/audio.c:135` | Listener contribution discarded the supplied entity and always used QA_AUDIO_NO_ACTOR. The shared Q3 mixer therefore missed its personal-sound full-volume branch and listener-specific voice limit for sounds emitted by that player. | Resolve the listener through the same canonical `audio_actor` callback as positional and looping sounds before contributing the listener. |
| PA07 | Medium | `src/app/application/match.c:83` | The mode player-view hook returned shared body angles. Q2/Q3 movement commits body pitch as zero while retaining actual view pitch in the canonical control record. LMCTF flag/rune drops therefore used a flat forward vector when the player looked up/down. | Prefer the generation-matched control view angles and keep body angles as the fallback for actors without admitted controls. |
| PA08 | High | `owner.c:386`, `lifetime.c:117`, `composition.c:11`, `publication.c:374` | A direct world body-binding callback can run while the session itself is safe. Application teardown/configuration could otherwise retire the session/actor owners while the enclosing world callback still retained them. | Use the world owner's new `qa_world_idle` callback/visit predicate in application destruction, both finalizer prechecks, configuration safety and publication preparation. This preserves the existing application serialized-operation contract. |
| PA09 | High | `src/media/cinematic.c:436`, `467`, `531` | Pause, time query and checkpoint capture called the caller-owned clock while `movie->busy` was false. A `clock.sample` callback could destroy that same movie, then each resumed method read or wrote freed movie state. | Hold the existing movie busy guard through each operation and every clock callback. A private capture body lets all allocation/decoder failures return through the guarded public wrapper. Tick/end already used this guard. |

PA01 and PA08 use narrow world/session predicates implemented by the independent
world/network reviewer. That reviewer confirmed the application lifetime,
cutscene, move-lease and audio repairs by source inspection. Their inspection of
control.c was limited to the changed paths. Their later review also accepted
the generation-matched mode-player-view fallback. This contributor independently read
both world/session predicate diffs and their destruction bodies. The predicates
match the pre-existing destruction rejection conditions; faulted idle sessions
remain eligible, and NULL handling is explicit in each API.

The foundations reviewer independently read the full cinematic owner and its
clock/public/fullscreen contracts and accepted PA09 at SHA-256
`def3c1f6e574fb3f2a611997aa6536eabf6d2ac9d230d759eccd8825b5a5e1fc`.
The three methods acquire busy before the external clock and release it on
success and failure. Existing callback-free metadata getters remain available.
The helper retains capture's previous allocation cleanup and output publication.

Donor references read for the sound and mode defects:

- `../quake-typescript/src/compat/qvm/client-audio-syscalls.ts`, the complete
  CG_S_RESPATIALIZE dispatch that passes entity identity to `setListener`.
- `../quake-typescript/src/content/q3/presentation/client.ts`, listener contract
  and forwarding calls.
- `../quake-typescript/src/content/q2/multiplayer/lmctf/flags.ts:105` and
  `runes.ts:108`, the drop direction selecting retained player view angles before
  body angles.

The C consumers were traced at `src/audio/mixer.c:332`, `:739` and `:757`,
`src/gameplay/modes/objects.c:644`, and the body-angle commitment in
`src/app/application/control.c`. CIN's initially absent pending image was also
compared with `../quake-typescript/src/media/cin-playback.ts:101-140`; that
behavior matches the donor and was not changed.

## Coverage and verification limits

The companion `review-20260929-presentation-app-coverage.tsv` records every file
in this contributor's assigned directories and selected public headers, with
its inspection extent and SHA-256 at review time. It records 36 complete source
reads, seven bounded source reads, and 77 explicitly uncovered files. Complete
source read means the file was read; it does not assert every behavior was
qualified. In particular, much of the committed audio, media codecs, input and
renderer remains uncovered by this contributor pass. App map/services reads
were bounded, while the other application C files were read completely.

Shared material-order changes were read together with registration destruction,
failure rollback, generated replacement, frame sort preparation and library
lifetime paths. No confirmed additional defect was found in those paths. The
pixel-space picture helper now supplies +Z normals and its legacy image wrapper
restores zero normals before projection. GPU resource inspection was limited to
texture and mesh admission, caching, references and pruning in resources.c.

Only source reads, repository metadata, coverage metadata generation, SHA-256
and `git diff --check` were used. The whitespace check passed. No engine build,
configure step, compiler, parser, test, executable, gameplay, sanitizer or
benchmark was run. No original source file was copied, no sibling repository
was edited, and no per-file license or copyright notice was added.

Known missing production definitions of Q3 presentation render/picture/remap
and external application map/movement adapters are unfinished baseline scope.
They were recorded as coverage limits and were not filled in during this bug
review.
