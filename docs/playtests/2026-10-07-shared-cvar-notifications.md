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

## Verification

The strict native build and the existing core, archive, VFS, BSP, image and model
checks pass. A temporary public-setter fixture checks canonical and alias changes,
independent receiver acknowledgement, identical assignments and skyfog revisions
for Q1, QW, Q2, Q2 rerelease and Q3. With the previous `2d900924` cvar object it
fails at the first Q1 notification; with this change all five dialects pass. The
same dependency libraries and fixture were used for both runs.

On the exact candidate, private Q1 classic `start.bsp` launches used fresh copies
of all 34 owner settings files, real keyboard console commands and a contained
PulseAudio output. The CPU and GPU-backed GL runs reached gameplay, changed
`gl_texturemode` from `GL_LINEAR_MIPMAP_NEAREST` to
`GL_NEAREST_MIPMAP_NEAREST`, restored it and quit through the public console with
exit code zero. Canonical values, render controls and the sampled/resident
filters followed 3 → 2 → 3 in both runs.

Each completed scene identifies an actual BSP world draw using `city4_6` and an
actual model skin draw using `*model:5965:skin:0`. The world image's declared usage
is DEFAULT; it is eligible because texture-mode handling is enabled. It was not
relabeled as WALL. CPU sampling changed between linear and nearest for both
objects. GL retained their texture names while issuing the corresponding min/mag
filter changes. The literal `frontend:source-conchars` picture filter remained
nearest. All six completed-frame PNGs were inspected and show the world, weapon
and original Q1 status bar.

The original settings and exact candidate file pins were unchanged. An independent
PID/start-token check found none of the 15 CPU or 16 GL owned processes remaining.
These are functional checks with a read-only debugger; they make no timing claim.

Local evidence:

- `/tmp/qa-the843-cvars-tzwrdxew/before-after.json`
- `/tmp/qa-the843-q1-change-notification-20261007/runtime-summary.json`
- `/tmp/qa-private-av-sukdvsjc/user/evidence/` — CPU PNGs, commands and sampling
- `/tmp/qa-private-av-g_rxmepo/user/evidence/` — GL PNGs, commands and API calls

This closes the reproduced Q1 notification defect. THE-843 remains In Progress
for its outstanding game/renderer evidence; this check does not claim the Q1
rerelease, Source Q3 or complete quality-setting matrix.
