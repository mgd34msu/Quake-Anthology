# THE-358 / MIKE-29: Native Q1 music output

**Native local only.** Six CPU-renderer captures prove the named level,
intermission, and episode-finale music in Q1 classic and rerelease.
QuakeC, remote NetQuake, remote QuakeWorld, and Mike's retest remain unproven.
These results do not close THE-358.

The fixed installed executable is from `37828b660751025678e3f118aae7e6fdf2cbcdf7`,
built 2026-10-07 21:54:47 CDT and installed 21:58:48 CDT. The recovery-cache
receipt is `installed-m0-cinematic-palette-20261007.json`. Every run records
`APPLICATION_PROVIDER_Q1` and uses a fresh copy of the owner's 34 settings
files, an authenticated private display, and a contained private audio server.
All six runs return public `quit` exit 0, leave the original settings and
installed executable unchanged, and retain no owned process after cleanup.

## Resolved installed files

These are the exact observed backing files relative to the installed content
root. The private packets retain their full resolved paths.

| Real game path | CD track | Classic backing file | Rerelease backing file |
| --- | ---: | --- | --- |
| `start` | 4 | `q1/id1/music/track04.ogg` | `q1/rerelease/id1/music/track04.ogg` |
| Public `map e1m1` | 6 | `q1/id1/music/track06.ogg` | `q1/rerelease/id1/music/track06.ogg` |
| Public `map e1m2` | 8 | `q1/id1/music/track08.ogg` | `q1/rerelease/id1/music/track08.ogg` |
| Authored `e1m7` exit, intermission | 3 | `q1/id1/music/track03.ogg` | `q1/rerelease/id1/music/track03.ogg` |
| Normal Space input, episode finale | 2 | `q1/id1/music/track02.ogg` | `q1/rerelease/id1/music/track02.ogg` |

The observer captures each named player's decoded PCM, actual music mix
contribution, accepted SDL buffers, and the private stereo monitor. Independent
FFmpeg decoding of all twelve selected source windows agrees with the captured
decoded PCM within one signed 16-bit sample unit. Each window contains 49,152
source frames at 44,100 Hz. This checks actual waveform identity in addition
to the recorded file selection.

## Captured output and EOF rollover

Classic retains the copied profile's 48,000 Hz device selection. Rerelease
selects 22,050 Hz. Both retain 16-bit stereo, music volume 1, effects volume
0.7, and a 48,000 Hz audio engine. The private monitor records at 48,000 Hz.
Rerelease comparisons account for device-rate conversion without changing
the game's audio settings.

| Scope | Decoded named PCM to monitor correlation | Accepted SDL to monitor correlation | Accepted comparison span |
| --- | ---: | ---: | ---: |
| Classic tracks 4, 6, and 8 | Minimum 0.999852541 | 1.0 for each | 1.851 to 2.021 seconds |
| Rerelease tracks 4, 6, and 8 | Minimum 0.999817219 | Minimum 0.999946357 | 1.729 to 1.793 seconds |
| Classic intermission 3, before and after EOF | 0.913017741 | 1.0 for each | 1.941 and 2.139 seconds |
| Rerelease intermission 3, before and after EOF | 0.913509181 | 0.999309540 and 0.999992461 | 1.771 and 2.003 seconds |
| Classic later finale 2 | 0.987123741 | 1.0 | 1.765 seconds |
| Rerelease later finale 2 | 0.987373588 | 0.999895032 | 1.772 seconds |

Decoded comparisons cover about 1.115 seconds each. Ambient game audio stays
in the monitor, so these are mixed-output comparisons. The intermission
opening repeats after EOF; the full-recording search selects its stronger
post-rollover match. The initial accepted-output comparison has its own
alignment. Classic also has sustained byte-exact monitor matches in blocks
of 4,096 stereo frames, or 16,384 bytes. The earlier four-byte comparisons
remain insufficient evidence.

Both full intermission runs reach track03's final decoded chunk at source
frame 6,438,912. That chunk contains 276 frames and ends at 6,439,188.
The real decoder then seeks the same owned track03 stream to frame zero.
The packets capture resumed chunks at source frames 0, 16,384, and 32,768,
then accepted post-loop output in the private monitor. The source length is
146.013333 seconds. No synthetic seek, accelerated playback, or `cd play`
or `cd loop` command substitutes for this EOF.

## Real transitions and rejected early finale attribution

Public `god`, `notarget`, `noclip`, and `bind c +movedown` enable a recorded
transit past the boss. Movement descends away from the retail teleporter,
approaches the exit corridor, restores collision with public `noclip`, then
walks into the authored `trigger_changelevel`. Both BSPs identify this trigger
as model `*14`, destination `start`. Native source state reaches intermission
stage 1, and the shared music policy receives track 3. Normal Space input
advances source state to stage 2 and produces the real track 2 event.
This route does not prove a normal campaign clear.

The first short finale captures fail named-music attribution. Their quiet
track02 opening correlates only 0.081781563 in classic and 0.088686862 in
rerelease against the mixed monitor. Those failures remain in the full EOF
packets. Separate real finale captures retain later decoded windows beginning
at source frame 196,608 and the qualifying output in the table above.
Filename selection or mixed nonzero output alone does not replace that proof.

The inspected rerelease finale screenshot still shows the world, shotgun,
and HUD without episode text. Source stage 2 and music track 2 are proven;
finale presentation parity remains an observed gap.

## Retained packets and remaining scope

The compact index is
`/tmp/qa-the358-native-music-20261007/native-proof-summary.json`.
The following directories are under `/tmp`.

| Scope | Packet directory |
| --- | --- |
| Classic level tracks 4, 6, and 8 | `qa-private-av-e289n2hl/user/proof` |
| Rerelease level tracks 4, 6, and 8 | `qa-private-av-1gmdxt8z/user/proof` |
| Classic intermission and genuine EOF | `qa-private-av-fv7whqbt/user/proof` |
| Rerelease intermission and genuine EOF | `qa-private-av-w91pdtp_/user/proof` |
| Classic later finale audio | `qa-private-av-y5vhzd1t/user/proof` |
| Rerelease later finale audio | `qa-private-av-0a7s5k71/user/proof` |

Each packet contains `events.jsonl`, `client-result.json`, `run-result.json`,
`audio-proof.json`, decoded and accepted PCM cuts, the continuous private
monitor, and screenshots. Each observer retains one startup frame-read error
before the game state is ready. Later music records contain no observer error.

Native local captures exercise the shared EFFECT consumer and native
intermission track3 producer. The QC CDTRACK projector and NQ/QW signon
changes are present in source but have no live proof here. Full music
fidelity, other scripted cues, owner speaker audibility, renderer parity,
latency, performance, and Mike's retest remain outside these captures.
