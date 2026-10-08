# THE-899 / MIKE-38: compositor startup and bundled font

Installed source: `136d4fd408b46bcc89812e76e9a5c676dac413a3`.
Build time: October 8, 2026, 00:49:25 CDT. Installation: 00:58:40 CDT.

The owner's SDL2 library is sdl2-compat 2.32.72 over SDL3 3.4.16.
The SDL2 interface cannot synchronize SDL3's asynchronous window changes.
Immediate flag comparisons therefore rejected valid compositor-owned state.
The private headless sway baseline reproduced the original candidate-settings
error. A later bare launch also exposed an optional display-mode query treated
as a fatal error when the saved window size did not match the headless output.

`2d139cac` removes those immediate state comparisons and requests focus with
SDL_RaiseWindow. `136d4fd4` accepts a window without a matching display mode.
Actual SDL operation failures remain errors. `9d061bf6` ships DejaVu Sans 2.37
and its license in `engine-data/fonts`, mounted through the common VFS after
authored content. An explicit font-directory override remains available.
There is no default dependency on a system font directory.

The exact candidate passed these private sway launches, each with a fresh
copy of the owner's 34 settings files and a normal, frame-limited exit (code 0):

| Scope | CPU | GL |
| --- | --- | --- |
| Bare executable-relative launch, saved display defaults | Menu visible | Menu visible |
| Startup menu, controlled 640×400 | Menu visible | Menu visible |
| Q1 classic, start | World and player HUD visible | World and player HUD visible |
| Q1 rerelease, start | World and player HUD visible | World and player HUD visible |
| Q2 classic, base1 | World and player HUD visible | World and player HUD visible |
| Q2 rerelease, base1 | World and player HUD visible | World and player HUD visible |
| Q3, q3dm0 | World and player HUD visible | World and player HUD visible |
| No game content, bundled font | Menu glyphs visible | Menu glyphs visible |

Root inspected all 16 final compositor PNGs. Bare launches overlaid the five
candidate application files onto read-only game installation paths inside
the private mount namespace; they did not alter the installed executable.
Actual process executable and overlay file stat records match the candidate.
The source attestation compared 3,066 build inputs directly, without hashing.
All original settings and candidate files remained unchanged. All 992 recorded
Wayland process tokens were absent after cleanup.

Private X11 checks also reached gameplay on CPU and NVIDIA GPU 0
(RTX 5060 Ti), exercised two local-lobby Start/End cycles, and quit through
the public UI with code 0. The GL qualification helper initially rejected its
own different port; its hard-coded comparison was corrected using the recorded
argv and bound endpoints. The failed helper receipt remains in the evidence.

Installation used `tools/install_qualified_build.py`, with copied-profile X11
and Wayland receipts, then directly rechecked all five installed files.
`tools/qualify_private_wayland.py` preserves the private display/audio setup
and required bare-default, game and no-content font scopes for future builds.
The installer requires both bare-default cases as well as the game matrix.

Evidence is retained under the local bundle `THE899-wayland-20261008`, indexed
in the Linear issue, with final PNGs, console logs, containment records,
process cleanup records and qualification receipts. The original failure and
unsuccessful transient-keyboard attempts are retained there too. One early
unwrapped input-tool invocation was disclosed; it was removed, and the
reusable runner contains no input-tool calls.

Bounds: sway used Pixman, software GL and dummy audio; these checks establish
startup and visible gameplay, not Hyprland desktop behavior, physical audio,
renderer fidelity, keyboard behavior or performance. X11's functional observer
was attached, so its runs supply no timing claim. Strict builds and core
behavior tests passed. Direct SDL3 migration remains THE-900 / U21.
