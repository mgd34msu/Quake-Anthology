# Q3 high-rate input and frame caps

The installed `1c0ab5dc` build, installed at 05:44:02 CDT, was checked on a
separate NVIDIA Xorg display using the RTX 5060 Ti. The owner desktop and
audio devices were excluded. Each case copied the owner's 34 settings files,
used physical Space and C events with X autorepeat enabled at its unchanged
660 ms delay and 25 Hz rate, then quit through the public console. The startup
debugger detached before input. Completed SDL GL swaps used the real monotonic
clock, separately from configured caps and simulation time.

Each case held Space for two seconds, released and pressed it again for
1.5 seconds, then held C for 1.5 seconds. `r_maxfps` and `r_swapInterval` were
zero. Uncapped cases also set `com_maxfps` to zero.

| Mode and requested cap | Completed swaps/s during first Space, second Space, C holds | Space initial/repeat/up events | C initial/repeat/up events | Jump events / presses |
| --- | --- | --- | --- | --- |
| Native, uncapped | 446.79 / 482.05 / 486.42 | 2 / 55 / 2 | 1 / 21 / 1 | 2 / 2 |
| Original, uncapped, copied profile | 229.00 / 239.92 / 244.02 | 2 / 55 / 2 | 1 / 21 / 1 | 2 / 2 |
| Original, uncapped, 640×400 | 229.58 / 239.44 / 241.95 | 2 / 55 / 2 | 1 / 21 / 1 | 2 / 2 |
| Native, 85 | 84.994 / 85.002 / 85.000 | 2 / 55 / 2 | 1 / 21 / 1 | 2 / 2 |
| Native, 250 | 250.058 / 250.009 / 249.997 | 2 / 55 / 2 | 1 / 21 / 1 | 2 / 2 |
| Native, 333 | 332.561 / 332.987 / 332.467 | 2 / 55 / 2 | 1 / 21 / 1 | 2 / 2 |

Native uncapped satisfies the requested 250–500 Hz condition. Original did
not reach 250 Hz, including at the directly observed 640×400 drawable; its
held-input result does not establish that high-rate condition. The first two
cases did not capture actual drawable dimensions. The Native 85 case directly
observed a 1920×1080 window and drawable on the private virtual screen. The
250 and 333 cases used the same directly observed dimensions. These installed
measurements establish the configured cap's effect in Native Q3. The additional
Native game and alias checks below extend that result. Original cap behavior
was not measured on that artifact; the later installed-build checks below
qualify it separately.

Native uncapped retained 3,953 completed command sequence advances across
8.351 seconds. Typical interior 50 ms server-frame groups contained 20–25
observed command receipts. Sampled sequence gaps and partial boundary frames
remain in the receipt. This is not a count of every `ClientThink` invocation.
The held-jump flag persisted through 72 grounded samples; release cleared it,
and the next press generated one jump. Crouch retained view height 12 and
returned to 26, with the Native completed world camera agreeing. Original
uses direct GAME player state; no post-detach Original camera proof is claimed.

## Physical landing bounce remains distinct

Native uncapped also showed three downward-to-upward velocity reversals,
`-275→275`, `-273→273` and `-270→270`, with no new jump event. Original's
copied-profile run independently showed `-269→269` without a new jump event.
The Native 333 case captured three reversals, `-268→268`, `-268→268` and
`-269→269`, also without new jump events. Native 85, Native 250 and the
reduced-resolution Original cases captured no such reversal.
Passing button suppression therefore does not mean physical bouncing is fixed.

Both the C mover and original `bg_pmove.c` probe ground 0.25 units below the
player. The original `PM_WalkMove` clips the downward velocity with `OVERCLIP`,
normalizes it and restores its full speed before the stationary-horizontal
return. This can turn a small upward clipped vector into a full upward velocity.
The C implementation has the same order in `src/movement/q3/move.c:175–208`
and `src/movement/q3/slide.c:3–6`; the original is
`quake-iii-arena/code/game/bg_pmove.c:790–804`. The previously captured complete
retail Pmove and independent retail collision replay reproduced this behavior.
The exact latest collision normal was not captured, so attributing each new
Native reversal to that mechanism is an inference supported by that replay
and the direct Original GAME observation.

THE-420 remains open for the unresolved physical symptom and owner's retest.
The configured-cap implementation is committed as `e6fe850b` under THE-583.
This record makes no new movement-fix or performance-speedup claim.

## Installed cap and alias checks across worlds

Four more Native cases used the same installed artifact and private RTX 5060 Ti
display. Each directly observed a 640×400 window and drawable, warmed for two
seconds and sampled six seconds of completed swaps per setting. No debugger
was attached. `r_maxfps`, `r_swapInterval` and `timedemo` were read back as zero.
The alias case first set `com_maxfps` to zero and read both names as zero,
then set `cl_maxfps` to 85 and read both names as 85.

| Native session | `com_maxfps 85` swaps/s | `cl_maxfps 85` swaps/s | Uncapped swaps/s |
| --- | ---: | ---: | ---: |
| Q1 classic, start | 84.994 | 85.016 | 758.941 |
| Q2 classic, base1 | 85.010 | 84.997 | 919.530 |
| Q2 rerelease, base1 | 84.978 | 84.981 | 706.395 |
| Q1 start with Q3 movement/character/weapons and Q2 rerelease monsters | 84.997 | 84.998 | 679.145 |

Root inspected the captured gameplay screenshots for these cases. Each quit
normally with exit status zero. All 66 recorded process IDs, including two
excluded helper setup attempts, were absent after cleanup. The owner profile,
installed files and swap observer were unchanged. These measurements prove
the public cap and alias affect the installed Native frame loop. They do not
qualify Original modules, complete combined-mode behavior, or the 1080p frame
time target. The uncapped column describes this small-window workload; it is
not a measured speedup.

The aggregate receipt is `qa-the583-gpu0-matrix-20261007/native-cap-result.json`.
Runtime evidence identifiers are `qa-private-av-mneiws4e`,
`qa-private-av-jdepf6_0`, `qa-private-av-n5iu9_c2` and
`qa-private-av-6r438m24`. Their `user/evidence` folders contain gameplay
screenshots for all three settings.

Evidence identifiers: `qa-private-av-ms3l1grc`, `qa-private-av-iw9ijnvg`,
`qa-private-av-4lfutnd7`, `qa-private-av-6y5xnevi`, `qa-private-av-uwcbcr0e`
and `qa-private-av-b__ov1c3`; each contains `result.json`
and cleanup records. The first two also have exact `physical-reversal-cuts.json`.
The prior independent replay is
`the420-detached-input-20261006/offline/physical-report.json`.

## Original client with fixed 2 ms and 4 ms frames

The supervisor-authorized fallback used the same installed `1c0ab5dc` on the
private GPU display. A private preload supplied fixed elapsed time only at the
four inspected `qa_frontend_run` performance-counter call sites. It activated
after gameplay admission and debugger detach. SDL frequency, other counter
callers, real monotonic time, OS autorepeat and delivered key events were
unchanged. No button, usercmd, movement rule or production clock was patched.

Actual returned counter deltas were exactly 2 ms or 4 ms. The normal simulation
debt consumed that elapsed time with its unchanged 50 ms Q3 Source recipe.
This tests the requested command cadence in game time; it does not increase
actual GPU throughput. Both caps and swap interval were zero.

| Original client frame time | Achieved client frames/s in game time | Completed command advances / commandTime interval | Actual completed swaps/s during first Space, second Space, C | Space initial/repeat/up | C initial/repeat/up | Jump events / presses |
| --- | ---: | --- | --- | --- | --- | --- |
| 2 ms | 500 | 2,171 / 4,342 ms, 500/s | 222.51 / 232.46 / 243.54 | 2 / 55 / 2 | 1 / 22 / 1 | 2 / 2 |
| 4 ms | 250 | 1,949 / 7,796 ms, 250/s | 223.43 / 237.16 / 239.42 | 2 / 55 / 2 | 1 / 21 / 1 | 2 / 2 |

Each accepted sequence advance had the corresponding commandTime progression;
no stale or unexpected advance was captured. Interior 50 ms Source groups
typically held 25 observed command receipts at 2 ms, and 12 or 13 at 4 ms.
One sampled sequence gap in each case and partial boundary groups remain
explicit. These are committed-command observations, not exact `ClientThink`
invocation totals. The synthetic frame clock means Source game time can advance
at a different rate from real time; the 50 ms recipe does not establish 20
server frames per real second in this diagnostic.

The held-jump and duck flags persisted through their real key holds; release
cleared them and repress generated one jump. The 2 ms case captured four
physical downward-to-upward reversals without new jump events, including after
release. The 4 ms case captured none. This still does not close THE-420's
physical symptom. Neither case entered the inspected frontend pacing-wait or
`SDL_Delay` call sites. The measured 4.1–4.5 ms real frame work was not broken
down by CPU/VM/GPU cost; pacing elsewhere and the Original throughput ceiling
were not diagnosed here.

Both cases quit normally. All 24 recorded owned process IDs were absent and
the private display sockets removed; installed files and original owner
settings were unchanged. Evidence identifiers are `qa-private-av-ridxp5uh`
and `qa-private-av-2ikd5c9c`, each with `frame-clock-proof.json` and exact
physical-state cuts. Original detached world-camera fidelity remains outside
this observation scope. The subsequent `3765bb6b` installation changes only
the CPU fog shader; this record identifies the precise earlier test artifact.
The compact aggregate is
`qa-the420-frame-clock-20261007/1c0ab5dc-original-fixed-frame-summary.json`.

## Current installed Q3 client and render caps

Two additional cases used installed `3765bb6b`, a fresh copy of the same owner
settings, affinity `0-7,12-19` and the private RTX 5060 Ti display. Actual
window and drawable dimensions remained 640×400. Each setting warmed for
two seconds, then sampled six seconds of completed GL swaps. No debugger
was attached; swap interval and timedemo read back as zero.

| Session and public setting | Completed swaps/s | Completed swaps |
| --- | ---: | ---: |
| Native Q3, `com_maxfps 0`, `r_maxfps 85` | 84.995 | 510 |
| Native Q3, both caps zero | 496.180 | 2,978 |
| Original Q3, `com_maxfps 85`, `r_maxfps 0` | 85.006 | 510 |
| Original Q3, canonical zero then `cl_maxfps 85` | 84.982 | 510 |
| Original Q3, both caps zero | 246.161 | 1,477 |

Public canonical and alias readbacks agreed before sampling. Root inspected
the captured Native and Original q3dm1 gameplay images. Both runs quit through
the public console with status zero, without cleanup signals. All 22 recorded
owned process IDs were absent afterward; owner settings, installed artifacts
and the swap observer remained unchanged.

These checks qualify the separate render-cap consumer and Original Q3's
canonical/alias client cap. They make no renderer speedup, 1080p target,
Original Q1/Q2 cap or complete rendering-fidelity claim. The Original log also
reported an autosave/recovery ownership failure; cap qualification does not
qualify saving or recovery. THE-420's separate physical bounce remains open.

Evidence: `qa-the583-q3-cap-20261007/q3-cap-result.json`,
`qa-private-av-r9iba9pu/result.json` and `qa-private-av-cnp7flmp/result.json`.
