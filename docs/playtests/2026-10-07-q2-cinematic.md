# Q2 classic cinematic palettes at the owner's settings

THE-201 / MIKE-15 exposed a palette ownership defect in the shared CIN player.
Commit `37828b66` keeps the palette with each decoded picture. The fixed
classic CPU and software GL captures match the retail movie at all eight
frames surrounding its four later palette changes.

These captures used the same installed build, compiled at 21:54:47 CDT and
installed at 21:58:48 CDT on 2026-10-07. Each run reached retail `base1`, played
`video/ntro.cin`, stopped the movie through the public console, returned to
gameplay, and quit normally. Public quit, GDB, and controller exits were all
zero in both fixed runs.

## Owner settings and completed captures

Each run copied the owner's 34 saved configuration and JSON files into a fresh
private profile. Saves were excluded, bindings were unchanged, and the original
profile remained unchanged. The route read `r_gamma` before and after the movie.
It never set gamma or brightness.

The owner's saved `r_gamma` was `1`, with `r_ignorehwgamma` set to `0`.
Both public gamma reads and every captured renderer gamma value were `1`.
The observed display gamma correction was inactive, and the fullscreen output
region did not request a source gamma transform. The expected brightness
transform for these actual copied settings was therefore identity.

Read-only GDB captured each frame after `frontend_frame_present` completed.
The observer retained the source frame index, decoded scene RGBA, displayed
index buffer, decoder palette, scene metadata, and actual window PNG. It made
no inferior calls or state writes. The worker inspected all 16 boundary PNGs
and both restored gameplay PNGs without changing exposure or contrast.

An independent FFmpeg decode read the installed retail movie from the start
and selected the same eight frames. A separate read-only CIN packet scan
retained the palette authored at each selected frame. Every new scene RGBA
buffer equals both the FFmpeg frame and its displayed indices mapped through
that frame's authored palette. Source frames are zero-indexed at 14 Hz.

| Retail frame | Source seconds | CPU RGBA vs retail | GL RGBA vs retail | CPU window max RGB byte error | GL window max RGB byte error |
| --- | ---: | --- | --- | ---: | ---: |
| 725 | 51.785714 | Exact | Exact | 0 | 0 |
| 726 | 51.857143 | Exact | Exact | 0 | 0.500000 |
| 1431 | 102.214286 | Exact | Exact | 0 | 0 |
| 1432 | 102.285714 | Exact | Exact | 0 | 0 |
| 1662 | 118.714286 | Exact | Exact | 0 | 0 |
| 1663 | 118.785714 | Exact | Exact | 1 | 0.500000 |
| 1872 | 133.714286 | Exact | Exact | 0 | 0 |
| 1873 | 133.785714 | Exact | Exact | 1 | 0.500000 |

The table rounds fractional window errors to six decimal places. Each window
comparison covers all 6,220,800 RGB channels of its 1920 by 1080 PNG.

## Current-frame palette ownership

Before the fix, frame 1662 used the next palette's color `(3, 2, 4)` for an
authored black frame. All 230,400 RGB bytes differed from the independent
retail frame. The displayed indices mapped through the authored palette
matched retail exactly. The same indices mapped through the decoder's
read-ahead palette matched the incorrect scene exactly.

In `src/media/cin_playback.c`, `cin_picture` previously retained pixels and an
index while `qa_cin_playback_frame` converted them with the decoder's current
palette. Reading the pending picture could replace that palette before the
current picture was presented. The shared fix copies all 768 palette bytes
alongside the indexed pixels in `read_picture` and converts with
`picture.palette`. Fixed frame 1662 is exactly black in the decoded scene and
both completed windows. All seven other selected frames also match retail.

Stock Q2 reads palette commands before frame decoding in
`quake-2/client/cl_cin.c`, `SCR_ReadNextFrame`, lines 427 and 449 through 478.
`SCR_RunCinematic`, lines 517 through 521, swaps pictures and then reads ahead.
`SCR_DrawCinematic`, lines 555 through 565, draws the current picture through
the mutable global cinematic palette. The stock comment at line 452 calls
that palette state dubious, so these checks do not assume flawless stock
double buffering. The authored CIN palettes and independent exact frames
provide the measured color reference.

Stock `ref_gl/gl_rmain.c`, `R_SetPalette`, lines 1497 through 1510, copies RGB
without attenuation and adds opaque alpha. `ref_gl/gl_draw.c`,
`Draw_StretchRaw`, lines 319 and 359, maps indices through `r_rawpalette`.
The native full-resolution source texture has its own explicitly measured
window scaling. Pixel identity with the stock 256 by 256 upload is not claimed.

## Actual presentation paths

Every captured cinematic scene used one fullscreen texture with LINEAR
filtering, CLAMP wrapping, a black RGB border, and opaque white vertices.
The observed lighting was `QA_LIGHT_VERTEX`, with zero lights and
`QA_FOG_NONE`. The movie did not use world lightmaps.

CPU rendered at 640 by 400, then used the actual `SDL_UpperBlitScaled` operation
to present at 1920 by 1080. The retained oracle models four-tap bilinear
sampling with the observed black border, native byte rounding, and that SDL
scale. Seven newly captured fixed window RGB buffers equal the previously
measured window buffers exactly, with identical presentation metadata. Their
one-byte bounds therefore transfer by full decoded-RGB equality. The new
frame 1662 source and window are both black and match the same scale exactly.
The new files remain separate from the older captures.

GL rendered directly at 1920 by 1080. The retained texel-center bilinear
CLAMP and black-border equation compared every completed window RGB channel
with the independent retail source. Its maximum error was
`0.5000000000012` byte, and no channel exceeded `0.500001` byte.
No fitted gain or extra brightness adjustment entered either comparison.
The dark scenes at frames 1663 and 1873 remain as dark as their retail frames
after the observed transform. These results do not establish a world-lighting
change.

The actual GL driver reported `Mesa`,
`llvmpipe (LLVM 22.1.8, 256 bits)`,
`4.6 (Compatibility Profile) Mesa 26.2.2-arch1.1`, and GLSL `4.60`.
These were software GL captures. NVIDIA was not exercised by this evidence lane.

## Private evidence and lifecycle limits

Game windows used authenticated private Xvfb displays. Actual output went to
private PipeWire and Pulse null sinks, with no hardware descriptors exposed.
Captured S16LE stereo audio at 48 kHz contained 30,625,920 bytes in the CPU run
and 31,812,480 bytes in the GL run. Both contained nonzero samples. This is
captured output evidence, with no sound-fidelity qualification.

| Capture | Private runtime bundle | Public, GDB, controller exit |
| --- | --- | --- |
| Fixed classic CPU | `qa-private-av-ke3nixck` | `0, 0, 0` |
| Fixed classic software GL | `qa-private-av-ekpn5gp6` | `0, 0, 0` |

The consolidated private record is
`qa-the201-palette-fixed-20261007/readout.json`, with `READOUT.md`,
`cpu-source-window-comparison.json`, `gl-source-window-comparison.json`,
the two lifecycle pin receipts, private output receipts, and the exact latest
supervisor comment. The independent decode, authored palettes, and before-fix
records remain in `qa-the201-owner-cinematic-20261007`.

The before-fix CPU bundle `qa-private-av-ksitr0e6` captured all eight frames and
logged an actual normal inferior exit after real public quit. Its temporary
observer omitted the exited-event JSON hook. The raw controller exit was `1`,
and `public_quit_exit` remained null because that receipt was missing.
That lifecycle gap remains recorded separately. Its measured completed images
are not relabeled as fixed-build captures. The restored existing exit hook
produced complete zero-exit receipts for both new fixed runs.

Before-fix GL bundle `qa-private-av-_vrv7vlj` was stopped at the coordinator's
hold and captured zero boundary frames. It supplies no before-fix GL pass.
The earlier incomplete CPU attempt `qa-private-av-i8bpxne1` also remains
excluded from the completed runs.

All three installed artifacts, the exact installation receipt, and retained
helper and reference files stayed unchanged across both fixed captures.
The receipts record 31 unchanged file pins. The original owner settings
remained unchanged, and all 16 recorded owned process tokens per run were
absent after cleanup. Assets, decoded frames, and screenshots remain private.

This evidence covers classic `ntro.cin` at the copied owner's settings around
all four later palette changes. Other movies, physical monitor calibration,
and the owner's visual retest are separate. Debugger captures provide no
performance result, and the private audio capture provides no audio-fidelity
claim.
