# THE-865, THE-902, THE-903: common host frame

Installed source: `9836d8ed70993ca417e71dfaf1cc326ed5bc7585`.
Build time: October 8, 2026, 01:40:05 CDT. Installation: 02:02:26 CDT.

`a71c6289` puts live frames and recorded recovery frames through one host
frame: drain events and commands, advance server clocks, collect and drain
again, then run client input, presentation and audio. Constructor, startup,
restore and cinematic states skip applicable phases within that order.
After a command changes console/chat focus, the existing platform focus
function synchronizes native text input immediately. Recovery never activates
physical input. `fdb786ca` skips subsequent client phases after public quit.

`cef731f4` fixes Custom QASV restore: normal construction initialized the
transient local lobby service, but restore created its menu without that
owner and crashed. Both now use one initializer before output construction.
No asset bytes, presentation state or additional validation checks were added
to saves. `9836d8ed` preserves the recorded command-drain cadence of older
recovery journals and executes successful command records at their saved time.

## Immediate chat input

Real keyboard input opened chat, entered the first `q` immediately after its
first observed chat frame, entered the complete literal with a semicolon,
and submitted it. No additional readiness-frame wait was inserted. Root
reviewed every editable-chat screenshot.

| Scope | Renderer | First character | Public quit |
| --- | --- | --- | --- |
| Original Q3 | CPU | Retained | 0 |
| Native Q3 | CPU | Retained | 0 |
| Q1 classic | CPU | Retained | 0 |
| Q1 rerelease | CPU | Retained after private binding | 0 |
| Q2 classic | CPU | Retained | 0 |
| Q2 rerelease | Software GL | Retained | 0 |
| Custom Q1 world, Q3 movement, Q2 rerelease monsters | NVIDIA GL | Retained | 0 |

Original Q3's first chat frame was 208, with `q` present at frame 210; native
Q3 was 210 to 212. SDL_StartTextInput entry metadata and active-state
readback support the focus fix, but do not record the exact drain-return
ordering. Existing plain/slash console cvar checks, movement release and
mouse checks passed in the single-game scopes. Every scope captured nonzero
private audio output; that alone does not prove a particular sound cue.

Q1 rerelease's saved `t` binding was empty. Its private profile received
`bind t messagemode` through the console; the source profile was unchanged.
Custom typed `say` still reports unknown command and its delivered notify
line appears twice. Those shared chat gaps are recorded on THE-162; they
are not claimed fixed by THE-902.

## Save restore, startup and recovery

CPU and GL runs each loaded a stock Q1 v5 autosave and a genuine Custom QASV
autosave, walked and released movement, saved again, and quit publicly with
code 0. Root reviewed all eight gameplay/restored-gameplay screenshots.
The 39,865-byte stock save remained byte-identical after creating the separate
288,342-byte Custom autosave. Stock resaves remained v5, Custom resaves QASV.
The GL functional run used software GL. Functional observers were attached;
these runs supply no timing or audio claim.

All sixteen private sway startup scopes passed on the same candidate: bare
saved defaults, controlled menu, five game products and no-content bundled
font, each on CPU and GL. Root inspected all final compositor PNGs. They
prove visible startup and frame-limited normal shutdown, not physical
Hyprland behavior, audio, gameplay correctness or performance.

The existing real-content recovery check recorded an unclean Q1 exit and
restored the checkpoint in a fresh process. Health 73, god mode, position,
command sequence and original-progs global/edict text matched. Its
109,503-byte journal did not change over 512 ordinary frames. This checkpoint
test contains no frame tail; it does not qualify older recorded-frame replay.
THE-865 remains In Progress pending that replay proof and its remaining
architecture acceptance. No per-frame recovery writer was reintroduced.

The public Load Game → Recover control recognized an actual October 5 Q2
rerelease journal with 290 frame/input/advance records and one successful
`hand 0` command. Its checkpoint decoder rejected a source count before
restoring a world or reaching the tail. The count field, value and maximum
were not captured. This exposes a retained-checkpoint compatibility gap;
it proves neither success nor failure of the changed frame replay. The
application returned to the menu and quit publicly with code 0. Root viewed
all three screenshots. The original journal, profile and installed files
were unchanged; all 15 owned process tokens were absent. Evidence packet:
`qa-the865-public-tail-9836-20261008-0nu4fspd/failure-packet.json`.

## Pinned CPU comparison

Q2 rerelease `base1`, copied owner profile, stationary view, swap interval and
FPS caps zero. Runs were sequential and pinned to `0-7,12-19`, without a
debugger or perf sampling. Each sample used 600 consecutive presentation
intervals after more than 2,270 actual warm-up intervals. The private X
presentation observer's overhead is included. These are CPU renderer cases.

| Drawable | Before `136d4fd4` median / p99 | After `9836d8ed` median / p99 |
| --- | --- | --- |
| 640×400 | 5.442413 / 8.248091 ms | 5.337002 / 7.603320 ms |
| 320×200 | 3.723302 / 5.249199 ms | 3.736714 / 5.290231 ms |

This is a small difference, not a material speedup. The CPU targets of
4 ms at 640×400 and 2 ms at 320×200 remain open. Every measurement exited
through public quit with code 0 and preserved the candidate and owner profile.

## Evidence and installation

The source attestation directly compared 3,064 committed build inputs.
Strict builds and existing core behavior checks passed. Installation used
`tools/install_qualified_build.py` with copied-owner-profile and Wayland
receipts; all five installed application files directly byte-match the
qualified candidate. No hashes were computed.

Linear comments index the local evidence packets: immediate-chat index
`THE902-six-scope-custom-index.json`; CPU/GL save packets `asodpgpx` and
`jfv_c2m5`; recovery packet `xm47vxib`; timing pairs `12hzjv1i`/`jaa_mz7o`
and `l5le2q4u`/`savpcnr1`; install receipt
`installed-u2-chat-lobby-20261008.json`. Failed baseline and temporary-observer
attempts remain available. All 34 original settings files stayed unchanged.
The chat sweep's 303 owned process tokens and Wayland sweep's 990 tokens
were absent after cleanup. All windows and audio stayed private.
