# THE-2859 QW policy handles

QuakeWorld spectator camera, skin selection and sound PHS policy read bound
handles in the common cvar table. Receiver construction and successful CLIENT
rebind resolve `cl_hightrack`, `cl_chasecam`, `noskins` and `baseskin`; provider
clock binding resolves `sv_phs` after source registration. Resets retain the
binding and live edits read the same table. There is no copied scalar, new
registry, per-frame resolution or context wrapper.

Actual-source packets:

- `/tmp/qa-qw-camera-cvar-handles-20261009`: 5,191 camera command comparisons
  and four lifecycle cases, GCC/Clang optimized and ASan/UBSan/LSan.
- `/tmp/qa-qw-fixed-policies-20261009`: 736 skin sequences, 3,272 exact QW28
  SOUND comparisons and seven lifecycle cases in the same four configurations.

The components use the actual common cvar implementation and camera/skin/emitter
source. Geometry, resource decode, transport and source adapters are controlled.
Changed values, prepared edits/abort, resets, rebind and owner retirement are
included. All changed C units pass strict GCC/Clang syntax checks. These checks
prove bounded behavior and SOUND bytes, rather than a retail remote server or
captured audio. No timing claim is made.

Production, ASan/UBSan and allocation-gate builds, with seven core checks each,
passed in `/tmp/qa-common-selectors-handles-build-20261009`. Frozen tree
`e186a88a0643665e38da5e6a2a89e89136a11060` contains this seven-path slice plus
the eleven-path body-selector slice. No installation was made. QC world
deathmatch, native Q1 limit consumers and native ABI shadow refresh still need
their remaining handle adoption.
