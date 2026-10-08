# Q3 event sounds, THE-159 / MIKE-06

Fixed installed `938ff929` has bounded pickup, shotgun fire, Sarge jump and
authored item-respawn evidence in native and Original Q3, on CPU and NVIDIA
GPU0 GL. This extends the earlier pickup-only proof. Pain, water and other
event families remain unqualified. No engine change was made for these runs.

The installation receipt is `installed-m0-local-lobby-20261007.json` in the
recovery cache. It records source
`938ff929b1d42c38bb1a00933627a2969a4aa107`, built 2026-10-07
20:37:33 CDT and installed 21:07:30 CDT. Each sequential route used the exact
installed executable and two native companions, a fresh copy of all 34 owner
settings files, an authenticated private display and a contained real Pulse
server with captured output. Original settings and installed files stayed
unchanged. These debugger observations provide no timing result.

## Four completed routes

All routes used retail `q3dm0` at 640×400. Real movement collected the shotgun,
keys 1 and 3 selected weapons, Mouse1 consumed one shell, Space produced an
airborne jump, and movement away allowed an authored item to respawn. No cue
was created by writing game state. The item-respawn sound resolved actual
source entity 135. Retained item rows show visible, hidden after pickup, then
visible again in every route. The sound is not inferred from an elapsed wait
alone.

| Route | Bundle | Four cues | Accepted cuts matched to monitor | Public quit | Owned process tokens absent |
| --- | --- | --- | ---: | ---: | ---: |
| Native CPU | `/tmp/qa-private-av-96t_bcgk` | Pass | 32/32 | 0 | 19/19 |
| Original CPU | `/tmp/qa-private-av-qvd1ehvw` | Pass | 32/32 | 0 | 19/19 |
| Native NVIDIA GPU0 GL | `/tmp/qa-private-av-yv4y0jd4` | Pass | 32/32 | 0 | 20/20 |
| Original NVIDIA GPU0 GL | `/tmp/qa-private-av-wtisvsz8` | Pass | 32/32 | 0 | 20/20 |

Every route has seven personally inspected whole-window PNGs covering
before/after pickup, both weapon selections, fire, jump and the final respawn
stage. Screenshots establish visible gameplay and HUD context. The audio
records establish the sound attribution. All 78 recorded PID/start tokens
were absent after cleanup. Both GL runs released their owned X77 socket and
lock before the next GPU user.

## Resolved assets and captured output

The shared `qa_q3_presentation_sound` observation resolves the real handle
through that presentation's asset bank. The selected mixer voice records its
actual asset pointer, voice ID, prepared sample and gain. The handles differ
between native and Original banks, while the resolved assets agree.

| Cue | Actual asset | Native handle | Original handle | Original source rule |
| --- | --- | ---: | ---: | --- |
| Shotgun pickup | `sound/misc/w_pkup.wav` | 74 | 78 | `game/bg_misc.c:210`, `cgame/cg_event.c:671–708` |
| Shotgun fire | `sound/weapons/shotgun/sshotf1b.wav` | 119 | 123 | `cgame/cg_weapons.c:738` |
| Sarge jump | `sound/player/sarge/jump1.wav` | 129 | 133 | `cgame/cg_event.c:620–622`, custom `*jump1.wav` |
| Item respawn | `sound/items/respawn1.wav` | 22 | 26 | `cgame/cg_main.c:646`, `cgame/cg_event.c:841–844` |

Original source references are within `qsrc/quake-iii-arena/code`. Each
selected source PCM equals the decoded retail `q3a/baseq3/pak0.pk3` WAV bytes.
All four assets are 22,050 Hz mono 16-bit PCM. Each prepared 48,000 Hz PCM
equals the original Q3 fixed-point nearest resampling calculation, including
its float-derived frame count and step. The reference is
`client/snd_mem.c:290–307`; the common implementation is
`src/audio/codecs.c:580–633`.

At the actual SDL queue entry, the observer reads the selected voice's current
slot, ID, asset, gain, start position and prepared samples. Equal-rate device
conversion state maps those samples to that buffer's source interval. The
queue return records acceptance. The complete accepted buffer is then found
byte-exact, with stereo alignment, in the private monitor recording. Each cue
has at least one nonzero matched cut of 4,096 bytes or more with a live,
positive-gain selected voice and nonzero prepared samples in that interval.
Tiny matches cannot qualify a cue.

The table counts substantial matched cuts with that live selected-voice
overlap. Each cue also retains all eight accepted buffer cuts, including
smaller buffers and cuts after its target voice retired.

| Route | Pickup | Fire | Jump | Item respawn | Item-respawn start gain, left/right |
| --- | ---: | ---: | ---: | ---: | --- |
| Native CPU | 7 | 7 | 5 | 2 | 59/64 |
| Original CPU | 7 | 5 | 6 | 2 | 59/64 |
| Native NVIDIA GL | 6 | 8 | 5 | 2 | 59/63 |
| Original NVIDIA GL | 8 | 7 | 6 | 3 | 60/65 |

Pickup, fire and jump start gains are 127/127 in all four routes. Mixer effects
gain is approximately 0.8. These live values support inferred contribution to
the captured mixed output. They do not isolate the cue's final mixed waveform
or prove perceptual fidelity. Common source points are
`src/presentation/q3/audio.c:67`, `src/audio/mixer.c:1666–1709` and
`src/audio/device.c:245–259`.

## Retained evidence and open bounds

The compact matrix is
`/tmp/qa-the159-fixed938-cue-captures-20261007/summary.json`. Each bundle has
`target-cue-output-summary.json`, `user/evidence/focused-events.jsonl`,
`user/evidence/samples.jsonl`, `user/evidence/result.json` and
`q3-monitor.raw`. Under `user/evidence`,
`actual-{pickup,fire,jump,item_respawn}-{source,prepared}.raw` contains the
selected PCM and `{cue}-queue-{1..8}.raw` contains the accepted output cuts.
The detailed summaries retain addresses, gain, sample windows and exact
monitor offsets without requiring sample arrays in this document.

The coordinator independently compared all 16 source/prepared PCM pairs
against retail WAV data and the original Q3 resampling arithmetic, then matched
all 128 accepted buffer cuts directly at their recorded aligned monitor
offsets. The result is `root-direct-byte-review.json` beside the compact matrix.

The frozen helper is `/tmp/qa-the159-current-cues-v2-20261007`. The first
attempt `/tmp/qa-private-av-rs92rvd6` stopped before game launch because the
optional static `paint_voice` symbol was absent. V2 removes only that optional
accumulator probe. The failed bundle and original helper remain preserved.

No pain or water cue resolved or started in these cuts. Observed health loss
was entirely -1 overhealth decay steps, approximately once per Source second.
Original GAME decrements over-max health in `game/g_active.c:457–460`.
`cgame/cg_playerstate.c:325–329` requires a drop greater than one for local
pain, so these screenshots do not establish bot damage. Splash footsteps do
not qualify water touch, leave, underwater or clear events. Actual damage and
authored water transit still need their own output cuts.

The Original CPU log separately preserves THE-585 at
`/tmp/qa-private-av-qvd1ehvw/user/evidence/logs/q3-functional-gdb.log:117–118`.
Original GL preserves the same warning at
`/tmp/qa-private-av-wtisvsz8/user/evidence/logs/q3-functional-gdb.log:124–125`:
`Original GAME console changes its actual engine ownership`. Saving and
recovery are outside this audio proof.

The remaining bounds include other event families, other weapons, combined
and remote sessions, isolated cue output, owner speaker audibility, latency
and performance. This four-cue matrix does not close the complete THE-159
sound-parity scope.
