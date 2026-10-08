# THE-843: shared cvar change notifications

A Q1 or QuakeWorld world suppressed the common cvar table's change notifications.
The public texture-mode command changed the canonical value while the renderer
continued using the old filter. This affected both renderers.

`write_commit` and the alias/derived-value metadata updates in
`src/console/cvars.c` now mark changes for every dialect. Receivers still clear
their own notifications, and an identical assignment does not increment the
revision. No polling, context wrapper, new failure return or content fingerprint
was added.

The original Q1 cvar structure has no modified/count contract to preserve
(`quake/WinQuake/cvar.h:56` and `quake/QW/client/cvar.h:56`). Its texture-mode
command immediately updates eligible textures (`quake/WinQuake/gl_draw.c:320`).
The unified renderer uses the common table's notifications to perform that update.

## Setter fixture and candidate qualification

The strict native build and existing core, archive, VFS, BSP, image and model
checks pass. The temporary public-setter fixture covers canonical and alias
changes, independent receiver acknowledgement, identical assignments and skyfog
revisions for Q1, QW, Q2, Q2 rerelease and Q3. With the previous `2d900924` cvar
object it fails at the first Q1 notification; the updated object passes all five
dialects using the same fixture and dependency libraries. This isolates the
notification change. It is not a live game or renderer matrix.

Before installation, the exact `fac87ea0` candidate passed private Q1 classic
`start` CPU and GPU0 GL gameplay, actual WORLD/SKIN policy changes, three PNGs per
renderer and public quit0. The installer used that copied-owner qualification.

Local evidence:

- `/tmp/qa-the843-cvars-tzwrdxew/before-after.json`
- `/tmp/qa-the843-q1-change-notification-20261007/runtime-summary.json`
- `/tmp/qa-private-av-sukdvsjc/user/evidence/` for candidate CPU
- `/tmp/qa-private-av-g_rxmepo/user/evidence/` for candidate GPU0 GL

## Fixed installed artifact

The current matrix below observes installed `qfiles/qa-c` from source
`fac87ea0428b67556ae8c846c5cc37232536ae6b`, built at
`2026-10-07T20:07:38.562341-05:00` and installed at
`2026-10-07T20:19:09.702318-05:00`. The installation receipt is
`installed-m0-shared-cvar-notification-20261007.json`; its retained direct-byte
copy is
`/tmp/qa-the843-fixed-fac87ea0-cpu-captures-20261007/root-receipt-before.json`.
Later source commits are not the artifact tested here.

Each route used a fresh copy of all 34 owner settings files, a private
authenticated display and an actual SDL stream to contained PulseAudio. The
public keyboard console changed the alias `gl_texturemode` from
`GL_LINEAR_MIPMAP_NEAREST` to `GL_NEAREST_MIPMAP_NEAREST`, then restored the owner
mode. Their numeric filter values are 3 → 2 → 3. The observer reads the actual
canonical `r_textureMode` row rather than an unmaterialized alias cache.

For native routes, WORLD means an actual perspective BSP draw. Its raw upload
usage remains DEFAULT in Q1 and WALL in Q2. SKIN requires an actual perspective
model draw. Loaded image metadata alone does not qualify either selection.

| Game and map | CPU on installed fac87 | GPU0 GL on installed fac87 |
| --- | --- | --- |
| Q1 classic, `start` | PASS: canonical controls, WORLD/SKIN resolver and prepared sampler 3 → 2 → 3; three completed-scene PNGs; quit0. Bundle `r42a8ohv`. | PASS: same drawn WORLD/SKIN resident names retain identity and change 3 → 2 → 3; actual min/mag calls; three PNGs; quit0. Bundle `lo9gy2s_`. |
| Q1 rerelease, `start` | UNQUALIFIED: baseline WORLD/SKIN samples and PNG; latest retry submits the command and observes controls2 plus a later WORLD return. Changed-phase SKIN/PICTURE samples, PNG, restoration and normal quit receipt are missing. Bundle `nlzkrei5`. | PASS: actual drawn WORLD/SKIN resident changes and min/mag calls 3 → 2 → 3; three PNGs; quit0. Bundle `7wh4g3k_`. |
| Q2 classic, `base1` | PASS: canonical controls, drawn WORLD/SKIN resolver and sampler 3 → 2 → 3; three PNGs; quit0. Bundle `w5w01ilx`. | PASS: actual drawn WORLD/SKIN resident changes and min/mag calls 3 → 2 → 3; three PNGs; quit0. Bundle `w4iizt6b`. |
| Q2 rerelease, `base1` | PASS: canonical controls, drawn WORLD/SKIN resolver and sampler 3 → 2 → 3; three PNGs; quit0. Bundle `69vppsj8`. | PASS: actual drawn WORLD/SKIN resident changes and min/mag calls 3 → 2 → 3; three PNGs; quit0. Bundle `k21qdzxm`. |
| Original Q3, `q3dm1` | PASS for actual frame-used Source mip/no-mip policy: matching Source reports, prepared samplers 3 → 2 → 3, three PNGs and quit0. Bundle `870mqk8u`. | No current fac87 policy run in this record. Historical installed982 evidence is separate below. |

### Selected native objects and exclusions

| Edition | Actual WORLD image and raw usage | Actual SKIN image | Literal exclusions observed |
| --- | --- | --- | --- |
| Q1 classic | `city4_6`, DEFAULT | `*model:5965:skin:0` | `frontend:source-conchars` remains nearest0; no retained SKY category qualified. |
| Q1 rerelease | `wizmet1_2`, DEFAULT | `*model:6614:skin:0` | `frontend:source-conchars` remains nearest0 in the baseline CPU cut and all GL cuts; no retained SKY category qualified. |
| Q2 classic | `textures/e1u1/metal5_1`, WALL | `models/deadbods/dude/dead1.pcx` | `pics/conchars.pcx` remains nearest0; `env/unit1_lf` remains its declared filter5. |
| Q2 rerelease | `textures/e1u1/ggrat5_2`, WALL | `models/deadbods/dude/md5/dead1.pcx` | Selected `pics/num_1.pcx` or `fonts/qconfont.png` remains linear1; `env/unit1_rt` remains linear1. |

CPU WORLD/SKIN prepared samplers report linear true → false → true, with no
blending between mip levels. Each return identifies the selected image address
and controls at the call. This proves sampler preparation for the drawn image;
it does not isolate individual covered pixels. GL observes stable WORLD/SKIN
names, resident filter changes and actual `TexParameteri` min/mag submissions:
9984/9728 for the changed mode, then 9985/9729 on restoration. These are API
submissions and renderer bookkeeping, not independent driver-state queries.

The whole PNGs establish the retained gameplay scene and visible HUD. Their
appearance alone does not prove texture filtering. The CPU capture worker viewed
all nine accepted PNGs from the three complete native CPU routes and the Q1
rerelease baseline cuts. The coordinator also inspected those nine native CPU
PNGs, the candidate, GL and Original Q3 captures. Exact policy claims come from
the matching records above.

`/tmp/qa-the843-q1-change-notification-20261007/installed-pixel-comparisons.json`
records direct before/change/restore pixel differences for eight installed fac87
cases. Its fixed left-world crop excludes notifications, the crosshair, view
weapon and status band. Q2 returns to identical baseline pixels in that crop;
Q1 has a few changed pixels after restoration. Scene time and animation are not
locked, so pixel differences alone do not isolate filtering or establish
equal-output equivalence before the engine fix. Filter attribution uses the
matched sampler/API records.

### Original Q3 Source policy

Installed fac87 CPU selects the actually frame-used Source image
`gfx/2d/numbers/zero_32b.tga`, with six mip levels. Its Source texture/filter and
prepared sampler change 3 → 2 → 3, with linear true → false → true. The actually
used no-mip `*white` image retains one level, filter1 and linear sampling. The
pre-clear Source reports for frames 6, 122 and 236 match completed frontend
frames 7, 123 and 237 with the same phase, renderer and actual Source world view.
The selected picture sampler is captured at baseline; its changed/restored
sampling calls are explicitly missing.

Historical Original Q3 GPU0 GL bundle `/tmp/qa-private-av-ls0nyznc` passed on
installed source `98225ced46089ae06a0c11cb02d7b07b052c823d`, not fac87. It observes
the same Source mip image with GL name53, retained filter 3 → 2 → 3 and actual
min/mag calls. No-mip `*white`, GL name3, remains filter1. Matching reports and
three PNGs accompany public quit0 and absence of all 16 recorded owned tokens.
Its receipt is `installed-m0-direct-fog-rays-20261007.json`.

Source selection reads the actual use bits at `qa_render_source_report` entry
before they clear. It proves use somewhere in the matching Source frame; it does
not attribute either selected image to an individual world or HUD draw. Native
WORLD/SKIN role evidence must not be substituted for that Source proof.

### Retained evidence and remaining bounds

The installed native CPU summary is
`/tmp/qa-the843-fixed-fac87ea0-cpu-captures-20261007/summary.json`.
Each bundle below contains `user/evidence/result.json`, policy records and PNGs.
Root GL/Source bundles also contain `qualification-result.json` and
`root-owned-cleanup.json`.

- Q1 classic CPU `/tmp/qa-private-av-r42a8ohv`; GL `/tmp/qa-private-av-lo9gy2s_`
- Q1 rerelease CPU latest `/tmp/qa-private-av-nlzkrei5`; GL `/tmp/qa-private-av-7wh4g3k_`
- Q2 classic CPU `/tmp/qa-private-av-w5w01ilx`; GL `/tmp/qa-private-av-w4iizt6b`
- Q2 rerelease CPU `/tmp/qa-private-av-69vppsj8`; GL `/tmp/qa-private-av-k21qdzxm`
- Original Q3 CPU `/tmp/qa-private-av-870mqk8u`; historical982 GL `/tmp/qa-private-av-ls0nyznc`

The two earlier fac87 Q1 rerelease CPU attempts remain unqualified and preserved:
`/tmp/qa-private-av-cgeh59ue` and `/tmp/qa-private-av-wfm_mx_h` failed at typed-text
observation. The latest observer arms CPU probes only for a pending capture with
the console closed. This lets the real input command complete, but request2 still
times out before the full sampler/PNG cut. The first retained changed-mode WORLD
return arrives 19.356858393 seconds after the request, after the client releases
at 12.006664333 seconds. These are debugger-affected observer file times, not
engine performance measurements. No engine filtering defect follows from this
missing proof. The complete retry record is
`/tmp/qa-the843-fixed-fac87ea0-q1rr-capture-only-20261007/summary.json`.

Original profiles and artifact pins remain unchanged in the recorded runs. The
three complete native CPU routes each leave none of 15 recorded owned tokens;
each installed native GL route leaves none of 16; Original Q3 CPU leaves none of
15. Each unqualified fac87 Q1 rerelease attempt leaves none of its 13 recorded
tokens, but none has a normal public quit receipt. Cleanup does not qualify it.

The live matrix covers the selected single-seat routes and these two texture
modes. QW has setter-fixture evidence only. Combined games, remote clients,
split-seat notification propagation, native Q3, other maps and the full quality
settings matrix remain unproved here. These read-only debugger functional runs
make no timing, latency or equal-output performance claim.
