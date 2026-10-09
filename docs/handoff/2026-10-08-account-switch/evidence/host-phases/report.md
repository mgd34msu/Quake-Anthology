# THE-865 bounded host-phase slice

Source lane is frozen for coordinator integration. Only `src/app/frontend/frame.c` was edited; no SDK, builds of the engine, Git changes, installations, external posts, or game launches were performed by this worker.

## Result

The existing host frame now has one common second physical collection/drain phase across normal play, cinematic playback, and owner-wait modes. When a constructor, CLIENT preparation, restart, or returned-resource owner is unavailable, physical input/socket intake still happens, while dispatch and command execution stay queued until that owner returns. A quit observed during either physical poll is latched independently of that dispatch readiness.

CLIENT configuration completion alone does not establish returned ownership. `continue_mode` checks the existing `qa_application_client_prepare_active` authority after advancing configuration, then falls through only after its real preparation token is cleared. There is no added context or validation wrapper.

Normal play keeps the existing order: first dispatch/commands, native server tick/publication, second physical collection/dispatch/commands, CLIENT snapshot readiness, current usercmd construction, scene/render/audio, completion. Existing source-duration math and application tick scheduling were moved as a block. The selected-input `controls` function is byte-for-byte unchanged from this lane's starting file, and the application advance has one call site before and after the change.

Replay still enters that same host function. It does no physical polling or current-input sampling, and preserves the historical single command-buffer drain per recorded frame. No save format, recovery writer, checkpoint reader, or persistent field was changed.

## Source evidence

| Responsibility | Current source |
| --- | --- |
| Physical collection plus quit latch shared by both polls | `src/app/frontend/frame.c:451` |
| CLIENT preparation waits for actual token release | `src/app/frontend/frame.c:718` and `:727` |
| Actual token cleanup authority | `src/app/application/client_prepare.c:132` and `:146` |
| Shared host entry and first collect | `src/app/frontend/frame.c:753` and `:771` |
| First physical dispatch/command drain | `src/app/frontend/frame.c:818` |
| Existing server advance and publication | `src/app/frontend/frame.c:870` |
| Shared second physical collect/dispatch/commands | `src/app/frontend/frame.c:890` |
| Cinematic client phase after the second drain | `src/app/frontend/frame.c:897` |
| Snapshot readiness before current command construction | `src/app/frontend/frame.c:917` and `:924` |
| Common completion/source events | `src/app/frontend/frame.c:975` |
| Replay wrapper reaches the same host | `src/app/frontend/frame.c:1023` |
| Recovery consumer invokes that wrapper | `src/app/frontend/save_commands.c:528` |

The reference order is `quake-iii-arena/code/qcommon/common.c`: first `Com_EventLoop`/`Cbuf_Execute` at 2692/2698, `SV_Frame` at 2713, second `Com_EventLoop`/`Cbuf_Execute` at 2743/2744, then `CL_Frame` at 2754. The C port's CLIENT snapshot readiness remains before command sampling because its current command construction consumes the published CLIENT state; this change does not swap it ahead of that state.

## Executed proof

`python /tmp/qa-the865-host-phases-20261009/run_checks.py` passed. `checks.json` records exact compiler commands and exit codes; each compiler's stdout/stderr is retained. `gcc-run.stdout`, `clang-run.stdout`, and `sanitizer-run.stdout` include per-case phase traces and aggregate PASS.

The fixture includes the exact current `frontend_step`, replay wrapper, `continue_mode`, physical drain/collector, command-drain helpers, and selected-input `controls` functions. It links current `src/platform/events.c`, `src/input/commands.c`, `src/input/mouse.c`, `src/core/common.c`, and `src/core/number.c`. Platform collection, settings/view state, console service, native world advancement, and network boundaries are typed fixture dependencies. There is no replacement host loop, FIFO, or command builder.

Twelve cases ran under strict GCC and Clang and under Clang ASan/UBSan:

1. Five selected-builder profiles (NetQuake, QuakeWorld, Q2 classic, Q2 rerelease, Q3): a key, packet, and console record injected during the second physical poll are processed before sampling; server advancement precedes that poll; CLIENT snapshots precede usercmd construction. The actual common builder preserves sequence and native profile timing fields (Q3 server time, zero where absent).
2. Constructor wait collects twice without dispatch or gameplay; the next ready frame processes the retained input and packet.
3. A second-poll quit during constructor wait stops without entering unavailable dispatch, commands, simulation, or controls.
4. CLIENT configuration reports complete while its token remains active: intake continues and dispatch waits. Clearing the actual token resumes the same frame normally.
5. Cinematic playback collects and dispatches twice and executes the second commands before its media phase, without a gameplay tick or current input sampling.
6. Recovery replay uses the shared host, advances/completes once, retains recorded wall/source/frame values, polls zero times and drains commands once.
7. A recorded historical console command passes through the actual FIFO and replay drain before that same replay host frame.
8. A quit processed during the second command drain skips controls and presentation.

Scope checks: `controls` unchanged; one `qa_application_advance` call site; literal `return false` sites 87 to 86; `frontend_fail` call sites 26 to 26. These counts describe this source diff, not an efficiency or coverage claim.

## Bounds and remaining integration

This proves bounded component phase behavior, not retail play, audio, frame timing, legacy network interoperability, or recovery checkpoint-tail loading. Native NQ 15/Fitz 666/RMQ 999, QW 28, Q2 34/rerelease, and Q3 68/TA wire adapters and widths were not edited. The five builder cases are common selected-input profile cases; they do not exercise every native transport receiver.

Coordinator owns the full source/SDK build and qualification after the current event-pressure stall is fixed. The old retained checkpoint rejection before journal tail remains a separate persistence item; this fixture neither claims it fixed nor changes its cadence.

`owned-source.patch` is the diff against the exact starting file, with the original retained as `frame.c.before`. No other source or protected persistence file was touched by this slice.
