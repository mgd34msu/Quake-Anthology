# Retail checks, 2026-10-06

The testing executable built from `c8d26292` includes the ordered CPU queue,
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
The muzzle origin remained distinct. Accepted nonzero SDL PCM was recorded,
but these two checks did not establish the weapon-loop sound identity.

## Open Original-provider checks

Original Q3 now passes the earlier empty-map-name failure and reaches Game and
BotLib initialization. It then fails with a QVM memory-range error. A read-only
capture identifies `CG_MEMORY_REMAINING`, import 58, incorrectly dispatched as
font registration, import 59. The memory-range guard correctly rejects the
resulting invalid record. A dispatch repair is still required.

Original rerelease Q1 now supplies typed client data, including actual health
and weapon model. Static-model submission still selects the native path from
catalog kind instead of the selected execution provider, reaching a classic
wire restriction. Additional local Rogue HUD and Unified readers need the same
typed Source access. Original Q1 rerelease readiness is not established.
