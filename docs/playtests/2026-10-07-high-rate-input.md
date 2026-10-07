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
measurements establish the configured cap's effect in Native Q3; other games,
combined mode and Original cap behavior remain separate qualifications.

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

Evidence identifiers: `qa-private-av-ms3l1grc`, `qa-private-av-iw9ijnvg`,
`qa-private-av-4lfutnd7`, `qa-private-av-6y5xnevi`, `qa-private-av-uwcbcr0e`
and `qa-private-av-b__ov1c3`; each contains `result.json`
and cleanup records. The first two also have exact `physical-reversal-cuts.json`.
The prior independent replay is
`the420-detached-input-20261006/offline/physical-report.json`.
