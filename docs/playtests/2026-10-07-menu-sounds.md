# THE-316 / MIKE-26: Q1 menu sound dispatch and output

Q1 classic and rerelease now preserve the menu entry cue when a real Return
action opens Options or Sound. Four captures on installed `eb32916f` prove
`menu2.wav` for those entries, `menu3.wav` for separate slider changes, and the
actual no-level fallback listener. They use CPU and private software GL,
fresh copies of the owner's 34 settings files, real keyboard input, and
captured private SDL/Pulse output.

## Before 938 and the shared fix

On installed `938ff929`, Return into Options and Sound first emitted
`QA_UI_OPEN`, resolving `misc/menu2.wav`. The successful control callback then
returned to `ui_action`, which unconditionally emitted `QA_UI_CHANGE` and
resolved `misc/menu3.wav`. Both local sounds use channel -1. The later menu3
voice replaced menu2 before menu2 contributed to painting. All four retained
before packets show this sequence, zero menu2 painter spans for those entries,
and actual menu3 output. Their separate slider output passes.

Commit `e9d3ec54` changes the one shared `src/ui/input.c:7` implementation.
Activation, submission and row activation select OPEN; list selection selects
MOVE; value edits retain CHANGE. If the action already changed the active
menu or stack depth, `qa_ui_open` or `qa_ui_close` supplies the cue and the
post-action notification is omitted. No game-specific dispatch was added.

The original Q1 contract distinguishes these actions:

- `qsrc/quake/WinQuake/menu.c:329-349`: Return selects a menu and sets
  `m_entersound`.
- `qsrc/quake/WinQuake/menu.c:3125-3129`: `m_entersound` plays `misc/menu2.wav`.
- `qsrc/quake/WinQuake/menu.c:1064-1066`: `M_AdjustSliders` plays
  `misc/menu3.wav`.
- `qsrc/quake/WinQuake/snd_dma.c:365-373`: channel -1 replaces the existing
  sound from the same entity.

The shared frontend maps MOVE to menu1, CHANGE to menu3, and entry/close to
menu2 in `src/app/frontend/ui_features.c:78-96`. Its local channel -1 request
uses the existing replacement rule at `src/audio/mixer.c:995`.

## Fixed installed artifact and exact input

The candidate from `eb32916f`, including `e9d3ec54`, built at
2026-10-07 21:29:45 CDT and installed at 21:34:20 CDT. The PASS installation
receipt is `installed-m0-menu-lobby-sound-20261007.json` in the recovery cache.
The coordinator qualified the exact candidate with copied-owner CPU and
NVIDIA GL gameplay before installation; the installed executable and both
companions directly equal those qualified files. The sound captures below
use that fixed installed artifact, independently of the candidate lobby runs.

Real End game returned to the no-level Home menu. Return then activated
Home menu 1 / control 3 **Options**, followed by Options menu 210 / control 2
**Sound**. These are `QA_UI_BUTTON` activations, not value changes. Each entry
now produces only `QA_UI_OPEN` and eight actual menu2 painter spans.

In Sound menu 213, control 1006 **Effects volume** received physical Left and
Right. Its actual action is `QA_UI_CHANGE_NUMBER`, with values
0.70 → 0.65 → 0.70. Each change separately resolves and paints menu3.

| Scope | After packet | Before 938 packet | Entry and slider output | No-level fallback observations |
| --- | --- | --- | --- | --- |
| Classic CPU | `qa-private-av-p7mvvtrg/user/q1` | `qa-private-av-7wivm4f0/user/q1` | Substantial attributed PCM; accepted SDL and private stereo monitor byte-exact | 34 |
| Classic software GL | `qa-private-av-4h5zrsrt/user/q1` | `qa-private-av-ht2j3nrd/user/q1` | Same separate menu2 and menu3 proof, byte-exact | 35 |
| Rerelease CPU | `qa-private-av-6qkppo2k/user/q1` | `qa-private-av-ednhz9ou/user/q1` | Exact device conversion; entry monitor correlation ≥0.9998906, normalized RMS error ≤1.4794% | 34 |
| Rerelease software GL | `qa-private-av-vcqz93fk/user/q1` | `qa-private-av-_zm56dks/user/q1` | Exact device conversion; entry monitor correlation ≥0.9998294, normalized RMS error ≤1.8469% | 34 |

These packet directories are under `/tmp`. Each contains
`menu-output-qualification.json`, `menu-route-proof.json`, actual input and
observer records, controller results, and raw `audio/` evidence. The
rerelease packets also contain `rerelease-output-review.json` with the
individual entry and slider comparisons.

## Authored cue contribution and delivered output

The full actual source PCM matches the resolved retail PAK/WAV: classic
11025 Hz mono and rerelease 44100 Hz mono. Actual prepared PCM is 48000 Hz.
Read-only observations record named voice identity, resolved resource,
actual source/prepared PCM, gains, and painter accumulator bytes before and
after. The measured contribution equals prepared samples multiplied by the
actual gains with the painter's integer rounding. Quiet no-level captures
match this substantial named-cue contribution through accepted SDL output
and the recorded private null-sink monitor. Mixed nonzero output alone does
not establish any cue pass.

Classic entry and slider checks retain qualifying painter spans of at least
512 frames and complete accepted stereo buffers found byte-for-byte in the
aligned private monitor. Each rerelease check first matches 768 device
frames exactly to the existing 48000 → 22050 nearest conversion, then
compares 1024 captured monitor frames after 22050 → 48000 conversion.
`src/audio/streams.c:248-282` supplies the existing device stepping; no engine
conversion change was made for this proof.

Rerelease monitor comparison trims 64 conversion-edge frames and permits a
bounded half-frame alignment for the capture resampler's fractional phase.
The bounds remain correlation ≥0.995, normalized RMS error ≤2%, and gain
0.98–1.02. Both monitor channels are identical over each compared span.
Slider comparisons pass independently of the menu2 entries.

## Profile, listener and cleanup

The owner's classic audio file saves 48000 Hz / 16-bit stereo. The owner34
source has no separate rerelease audio file; the rerelease's actual selected
and accepted output is 22050 Hz / 16-bit stereo. Both selections remain
unchanged throughout these routes. The private monitor records at 48000 Hz;
the different rerelease device rate is accounted for in evidence analysis,
not changed to obtain raw byte identity.

Public console queries record initial music volume 1 and effects volume 0.7.
Only the private copied profile is changed to music 0 and positive effects
0.7 during attribution. Real console input restores both original values,
and subsequent public queries confirm them before public quit0. The owner's
34 original files remain byte-identical through each run.

The observer independently records zero live map providers with one actual
listener whose actor is `QA_AUDIO_NO_ACTOR`. This directly exercises the
existing fallback at `src/app/frontend/presentation.c:521-523`, rather than
inferring a listener from audible output. All four captures have zero
observer errors and normal public quit exit 0. Installed artifact stat pins
remain equal, and every recorded owned PID/start token is absent after
cleanup. No owner desktop window or audio stream is used.

The final helper and summary are retained in
`/tmp/qa-the316-menu-938-lnkvl407`: `frozen-ready`, `after-fix-summary.json`,
`after-capture-status.json`, and `THE316-READOUT.md`. Runtime helpers remain
byte-identical to the before-fix freeze. Only offline rate-conversion
evidence changed; all before raw packets are preserved.

## Earlier evidence and limits

Earlier move/open/close evidence remains separately stamped to installed
`418` (built 2026-10-07 07:59:29 CDT, installed 08:12:00 CDT), under the
retained sweep's `THE-316` and `_q1-common` packets. Verification comment
`5a31337f-ebf4-4faf-aea2-4bb8c01bac26` accepted real Escape open/close menu2
and Down/Up menu1 event/voice/private-output evidence in all four cases,
while leaving select, slider and independent no-listener observation open.
Those earlier checks are not relabelled as eb32916f captures. The four new
packets above address the separately missing scopes and expose the 938
entry replacement defect before its fix.

GL in these sound packets is private llvmpipe software rendering. Initial
Escape from live gameplay has cue and painter observation but lacks an
isolated waveform comparison here. No complete renderer timing, NVIDIA
performance, subjective owner audibility or Mike retest claim follows from
these functional debugger captures. Supervisor verification and Mike's
retest remain separate.
