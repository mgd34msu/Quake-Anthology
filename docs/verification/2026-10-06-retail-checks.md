# Retail checks, 2026-10-06

The initial testing executable built from `c8d26292` includes the ordered CPU queue,
fogged brush spans, Q1 typed presentation reads, Q3 GAME information publication
and shared Q2 effect capture. Complete GCC and Clang builds passed with warnings
treated as errors, followed by all six registered CTest checks on each build.
The installed `qa-c` was compared byte for byte with the GCC executable.
These checks do not establish full campaign or multiplayer acceptance.

## MIKE-03: Rogue platform startup

Retail classic Rogue `rmine1`, GL, completed 180 frontend frames and exited
normally. Its actual Q2 Source clock reached 2.0 seconds, beyond the previous
500 ms failure. No gameplay input was supplied. A short read-only debugger
observed the shutdown boundary; this was not a performance test.

The previous failure involved `func_plat2` touching its own center trigger.
The trigger read combat health before checking whether its touching actor was
a living player or monster. Commit `b2b95faf` moves the original eligibility
checks before combat access. Missing combat bindings still fail for eligible
actors. This establishes the reported startup repair, not all Rogue combat.

## MIKE-14: queue and drawing paths

Actual CPU checks on retail Q1 `e1m1`, classic Q2 `base1`, rerelease Q2 `base1`
and Q3 `q3dm1` verified `timers` enable, disabled retention and reset. The
rerelease sample retained 553 queued brush draws per frame while dispatching
the workers three times per frame. Span and triangle pixel totals are recorded
in [the timing record](2026-10-06-render-timings.md).

Four Q1 and four classic Q2 screenshots matched the previous build's camera,
player, RNG, clocks and styles, with no RGB differences. Four native Q3
screenshots had identical RGBA but differing frontend clocks, so general
animation equivalence remains unproven. Rerelease screenshots matched gameplay
state but differed in frontend clocks and 229–941 pixels out of 307,200, with
maximum RGB differences of 1–11. Exact animated image equivalence is not claimed.

## MIKE-20: authored Plasma Beam

Genuine public weapon selection and held fire produced visible retail Plasma
Beam meshes on rerelease `base1` with GL and CPU. Both runs reached their actual
render backend and exited normally. The CPU observer captured six actual
Source audiences: Q2 ALL, shooter body origin and the actual local recipient.
The muzzle origin remained distinct. The first two checks recorded accepted
nonzero SDL PCM without establishing the weapon-loop sound identity. Follow-up
classic Rogue GL and rerelease CPU checks traced `sound/weapons/bfg__l1a.wav`
through the actual float mixer with positive gain and nonzero output, followed
by accepted dummy SDL PCM. Both exited normally. These checks establish that
weapon loop's identity and mixing, not physical speaker output or every effect.

## MIKE-01: Original Q1 ground publication

Actual `b95ea767` Original rerelease `e1m1` remained airborne after a successful
floor trace. Key release reached a zero command, but horizontal speed stayed at
the 30-unit air limit; jumping did not work. The shared mover's ground result
was overwritten by a QC callback reading old edict flags.

Commit `efe9e317` publishes the resolved ground state to the actual QC owner
before movement callbacks and retains the QC world edict reference. It uses
the same adapter for every selected mover and preserves legitimate flag changes
made by callbacks. It does not force a ground bit after callbacks.

The shipped `efe9e317` rerelease check used genuine mouse, forward press/release
and jump press/release on retail `e1m1`. Walking was grounded and reached 200
units/sec. Released input reached zero and stopped before jumping; the player
rose airborne and landed grounded. Gravity remained 800 and player identity was
preserved. The game exited normally. State was observed with a read-only
debugger, so this is behavior evidence and carries no timing claim. Original classic Q1 and both native classic/rerelease configurations also
passed the same public walk/release/jump/land actions and normal exit. Native
classic and rerelease stopped after 28.785 and 27.092 horizontal units on flat
ground without wall contact. Their observed grounded velocity transitions
matched stock friction 4, stop speed 100 and each actual tick duration within
0.00006 units/sec. QuakeWorld startup remains a separate open clock-boundary
check; the native Q2 callback adapter rejects its valid unadvanced exit time
even when no native Q2 recipient exists.

## Open Original-provider checks

Commit `c538807b` repairs Original Q3 memory/font import dispatch. The invalid
memory record came from dispatching argument-free `CG_MEMORY_REMAINING`, import
58, as font registration, import 59. The memory-range guard was correct.

Commit `06fb1996` then repairs a returned GAME fire-stamp read during independent
CGAME drawing. Actual `efe9e317` CPU reaches retail `q3dm1` and renders the stock
Q3 in-game menu, with screenshots inspected. Quit still fails at a scene-role
exchange guard. Normal cleanup, GL, restart, chat and the apparent duplicate HUD
remain open; rendering one map/menu is not full Original-provider acceptance.

Commit `3c63253c` routes local static models, Rogue HUD and Unified observation
through the selected Q1 execution provider. Original rerelease Q1 now consumes
its actual signon messages and typed health/weapon model. Retail `e1m1` has no
static models in that signon; a positive model count is not an acceptance
requirement. The movement proof above repairs the next reached defect. Full
campaign, guest-mod and multiplayer acceptance remains open.
