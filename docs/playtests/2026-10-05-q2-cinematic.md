# MIKE-15: Quake II cinematic brightness

No extra cinematic darkening reproduces in the tested `qa-c` builds. The actual classic Quake II `video/ntro.cin` frames match an independent decode of the retail file byte for byte. At neutral gamma, screenshots of the exact completed CPU and GL frames match bilinear sampling of those palette colors to within one 8-bit RGB step across the whole image. The opening's dark palette is present in the retail movie itself.

The checks used the actual executable, a visible window on a private X display, genuine X11 console input, and read-only GDB observations. The public `r_gamma 1` and `cinematic video/ntro.cin` commands started playback. At each checkpoint the debugger observed the preceding completed scene image and white quad, then captured the mapped window while that frame was paused. No function was invoked and no game state was changed through GDB. Playback was skipped with a normal key press, the world continued rendering, and the application quit normally.

| Build | Renderer | Output | Independent retail frames matched | Largest whole-frame RGB error |
|---|---|---|---|---:|
| 59c89987 | CPU | 320 × 200 | 5, 10, 16 | 0.501 |
| 59c89987 | GL | 640 × 400 | 8, 14, 19 | 0.801 |
| f504 | GL | 640 × 400 | 31, 55, 81 | 0.950 |

Every channel was compared, including the admitted clamp-border samples. Each GL checkpoint checks 768,000 RGB channel values. Decoder comparisons used `ffmpeg` on the installed retail file, independently of the C engine decoder. The machine-readable file records each checkpoint and the exact maximum errors; the table rounds upward.

The behavior follows `quake-2/client/cl_cin.c:SCR_ReadNextFrame` and `SCR_DrawCinematic`, `ref_gl/gl_rmain.c:R_SetPalette`, and `ref_gl/gl_draw.c:Draw_StretchRaw`: use the movie's palette RGB without an extra brightness multiplier. The software original applies its selected `vid_gamma` palette normally. In this engine the corresponding reached paths are `src/media/cin.c:qa_cin_rgba`, `src/media/cinematic_scene.c:qa_cinematic_fullscreen`, and `src/app/frontend/campaign_cinematic.c`. The decoded image is published without scaling and the fullscreen picture uses opaque white vertex colors. This check qualifies `ntro.cin` at neutral gamma; it does not claim other movies, settings, or monitor calibration.

Evidence is in [2026-10-05-q2-cinematic.json](2026-10-05-q2-cinematic.json). Raw screenshots and decoder buffers remain in the private playtest archive. No movie bytes are included in these documents.
