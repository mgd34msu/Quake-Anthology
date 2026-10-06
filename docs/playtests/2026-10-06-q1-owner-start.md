# Q1 owner start-map checks, 2026-10-06

## MIKE-01: default Space

Installed build `87a4a97d` reproduced the owner's failure on retail classic
`start.bsp`: Space in the saved profile dispatched `+moveup` and did not jump.
Fresh settings on the same executable dispatched `+jump` and jumped. This was
a real keyboard check, without a debugger or a replacement jump binding.

Commits `d2f19905` and `ab43f9a1` keep one Jump action in selected defaults and
the controls menu. The existing command builders encode it for each mover.
Explicit Q1 `+moveup` retains its vertical movement behavior. Selected defaults
and explicit overrides survive settings reload; Reset reapplies managed defaults.
The existing version-1 settings reader also accepts older profiles, preserving
their explicit choices.

The three confirmed stock Jump entries in the owner's legacy profile received
a backed-up correction: Space, raw mouse button 3 and controller A. Other bytes
were preserved, including raw mouse button 2 bound to forward movement.

Installed `ab43f9a1`, CPU and GL at 640x400, passed real Space and raw mouse
button 3 press/release on classic `start.bsp` using a clone of the corrected
owner settings. World-camera photographs showed rise and landing. Raw mouse
button 2 still moved forward. No binding was changed during either live run.
These checks do not establish physical-controller acceptance.

## MIKE-21: slipgate timer release

Commit `4a917022` removes the classname comparison from grapple timer release.
Teleport fog has no classname. Grapple animation teardown now checks the
owner's retained animation reference, and players without grapple state skip
grapple bookkeeping. Component runs reproduced the original null-classname
crash and checked fog, hook, animation and stale-reference cleanup after the fix.

Both installed `ab43f9a1` runs walked through the Normal slipgate using the
default forward key and continued beyond fog expiry without crashing. Saved
player origin reached `(544, 2015.968750, 24.031250)`, downstream of the map's
teleport destination `(544, 1536, 16)`. Both public quits exited with status zero.

## MIKE-22: original writes and the next reached load defect

Commit `333b84cb` admits the actual local client index zero, distinct from
physical Q1 edict one. It also drains existing CLIENT delivery before reading
startup save metadata. Both installed runs wrote their level-entry autosave and
manual saves as original v5 text, without the shared-format fallback.

| Renderer | Level-entry autosave | Before-gate manual | After-gate manual |
| --- | ---: | ---: | ---: |
| GL | 32,495 bytes | 32,260 bytes | 33,040 bytes |
| CPU | 32,495 bytes | 32,286 bytes | 32,291 bytes |

Both attempts to load the after-gate file failed because several installed
products qualified for its map and Source definitions. A subsequent save kept
the current state; it is not evidence of a successful load. Product selection
and successful live load remain open at this build.

## Verification scope

Complete GCC and Clang builds with warnings treated as errors passed, followed
by all registered checks on each. The installed executable and sibling runtime
files were compared byte for byte with the build. Both live runs preserved the
owner settings and installed artifact and released their processes and private
display. No performance, full-campaign or multiplayer claim is made.

Private evidence receipts: `owner-default-space-baseline-87-proof.json`,
`owner-settings-clone-space-baseline-87-proof.json`,
`owner-jump-profile-repair-20261006.json`,
`owner-jump-slipgate-save-ab43-proof.json`, and `shipped-ab43f9a1.json`.

## Installed `b9140af8`: keyboard, slipgate and native load

Commit `e9d2e4eb` resolves the original save against the qualified current game.
Its four live checks restored position, but native admission rejected saved
globals and map actors, so load switched to QuakeC and changed the ammo HUD.
That result was incomplete. Commits `dff9f9c2` and `b9140af8` pair those globals,
map continuations and monster view offsets with their existing native state.
Mover vectors share one capture/admission/restore table; gameplay and saved
monster admission share the same view-height calculation.

Installed `b9140af8` passed all four requested classic `start.bsp` paths:

| Entry | Renderer | Position after moving away (Y) | Position restored by load (Y) |
| --- | --- | ---: | ---: |
| CLI | CPU | 1860.981567 | 2015.968750 |
| CLI | GL | 1853.435913 | 2015.968750 |
| Actual menu/preset | CPU | 1857.634155 | 2015.968750 |
| Actual menu/preset | GL | 1861.098877 | 2015.968750 |

Every case used the corrected owner settings clone and real default Space,
raw mouse button 3 and forward input. Photographs show jump rise and landing.
Normal-gate travel survived fog expiry. Original v5 saves restored the exact
saved player position after distinct movement, and the stock ammo icon and
count remained intact. Each public quit exited zero. Owner settings and the
installed artifact remained unchanged; all processes and the private display
were removed. No rebinding, god mode, noclip or debugger was used.

Both complete compiler builds and their registered checks passed. Separate
GCC and Clang application component runs retained built-in Q1 through two
load/save cycles, each reproducing the entire 32,661-byte owner save exactly.
The live receipt is `owner-jump-slipgate-save-b914-proof.json`; installation
receipts are `shipped-b9140af8.json` and `shipped-runtime-b9140af8.json`.

Rerelease default Space also jumped and landed in installed `e9d2e4eb`, CPU and
GL, with its separate fresh input profile. This is recorded in
`owner-rerelease-default-space-e9d2-proof.json`; it does not qualify rerelease
slipgate travel or loading.

The classic owner start-map defects MIKE-01, MIKE-21 and MIKE-22 pass these
live acceptance checks. The nonfatal demo-recording startup warning remains
open. Native admission of placed items and additional mover types remains
incomplete on other maps; these checks do not establish all-save or campaign
completion.

## Installed `235e359a`: owner path after inventory and brush changes

The paired inventory and brush-save changes in `f80b9454` and `235e359a` passed
complete GCC and Clang builds and every registered check. The executable and
sibling runtime installed in `qfiles/qa-c` match the GCC build byte for byte.

Three further checks on that installed copy passed real default Space and raw
mouse button 3 press/release, rise and landing, and preserved raw mouse button 2
forward movement. Each walked through the Normal slipgate beyond fog expiry,
wrote original v5 saves, moved away with the default S key, loaded, and resaved.

| Entry | Renderer | Y after moving away | Y restored by load |
| --- | --- | ---: | ---: |
| CLI | CPU | 1858.122192 | 2015.968750 |
| CLI | GL | 1861.507080 | 2015.968750 |
| Actual menu/preset | GL | 1859.665161 | 2015.968750 |

Position, view angles, health, weapon, ammunition and item flags matched the
pre-load player record exactly after resaving. Photographs show the retained
stock ammo icon and 25 shells. These live checks observe the default native
launch and stock HUD; they do not instrument the private provider pointer.
Manual and level-entry saves were original text, between 32,208 and 32,975 bytes.

Each public quit exited zero. All game processes and the private display were
removed, and owner settings and the installed artifact remained unchanged.
Receipt: `owner-jump-slipgate-save-235e359a-proof.json`. The nonfatal demo warning
still appears after load. No performance, audio or full-campaign claim is made.

## Installed `41c128d0`: current owner acceptance

The original-writer selection, scoped clock, host-owned client colors, hidden
map metadata, empty-target callbacks and pickup-state changes passed complete
GCC and Clang builds and their registered checks. The installed executable and
12 sibling runtime files match the GCC build byte for byte.

Three current shipped checks again passed default Space and raw mouse button 3
jump/release/landing, raw mouse button 2 forward movement, Normal slipgate
travel beyond fog expiry, and original v5 save/load/resave after real S-key
movement. No binding changed during the checks.

| Entry | Renderer | Y after moving away | Y restored by load |
| --- | --- | ---: | ---: |
| CLI | CPU | 1857.529419 | 2015.968750 |
| CLI | GL | 1859.631226 | 2015.968750 |
| Actual menu/preset | GL | 1857.738403 | 2015.968750 |

The saved and resaved origin, angles, health, weapon, ammunition and item flags
match exactly. Inspected photographs retain the stock ammo icon and 25 shells.
All manual and level-entry files are original text, 32,208–32,601 bytes. All
three public quits exited zero; game PIDs and the private display were removed.
Owner settings and the installed artifact remained unchanged.

Receipt: `owner-jump-slipgate-save-41c128d0-proof.json`. This is current live
acceptance of MIKE-01, MIKE-21 and the owner start-map MIKE-22 path, without a
private provider-pointer instrument, audio or performance qualification.
The nonfatal demo warning after load remains. Actual original-progs import on
other maps is a separate compatibility check and is not established here.

## Installed `acf06fa7`: repeat after span and fog changes

The same three owner paths passed on the newly installed executable: CPU CLI,
GL CLI and actual GL menu. Real default Space and raw mouse button 3 jumped
and landed; raw mouse button 2 retained forward movement. Each Normal slipgate
run continued beyond teleport-fog release, with no crash.

| Entry | Renderer | Y after moving away | Y restored by load |
| --- | --- | ---: | ---: |
| CLI | CPU | 1854.344238 | 2015.968750 |
| CLI | GL | 1854.947632 | 2015.968750 |
| Actual menu/preset | GL | 1860.025757 | 2015.968750 |

All manual and level-entry files used original v5 text, between 32,208 and
32,972 bytes. Resaving after load retained the exact player origin, angles,
health, weapon, ammunition and item flags. The stock 25-shell HUD remained
visible. All three public quits exited zero; settings and the installed
artifact remained unchanged, and the private display was removed.

Two further bounded retail Q2 rerelease `base1` CPU captures at 640×400 and
320×200 retained coherent world geometry, lit textures, visible pickups and
the see-through machinery window. These spawn views do not establish
whole-map or pixel-perfect rendering parity. The existing nonfatal demo
warning after classic load remains open.

Receipt: `owner-jump-slipgate-save-brush-acf06fa7-proof.json`.

## Installed `694ccc53`: keyboard and rendering regression checks

CPU CLI, GL CLI and the actual GL menu passed default Space and raw mouse
button 3 jump/release/landing, retained raw mouse button 2 forward movement,
and survived Normal-slipgate travel beyond fog teardown. Original v5 saves
restored exact player origin, angles, health, weapon, ammunition and item flags
after real S-key movement. The stock 25-shell HUD remained visible.

| Entry | Renderer | Y after moving away | Y restored by load |
| --- | --- | ---: | ---: |
| CLI | CPU | 1858.866821 | 2015.968750 |
| CLI | GL | 1860.134888 | 2015.968750 |
| Actual menu/preset | GL | 1858.188721 | 2015.968750 |

All manual and level-entry saves were original text, 32,626–33,028 bytes.
A separate visibility check on classic `e1m1` showed no textured trigger
volumes in the corridor or slipgate view. That check used temporary noclip
navigation and private bindings; it is excluded from movement acceptance.
Its occluded side view adds no visibility evidence.

Two Q2 rerelease `base1` CPU spawn captures at 640×400 and 320×200 retained
lit world textures, pickup models and a see-through machinery window. These
are bounded image checks, not whole-map parity. All six public quits exited
zero, processes and the private display were removed, and owner settings and
the installed artifact remained unchanged. The nonfatal demo warning after
classic load remains open.

Receipt: `owner-jump-save-visibility-brush-694ccc53-proof.json`. Full GCC and
Clang builds and all registered checks passed before this installation.

## Installed `ff437555`: current owner acceptance

Complete GCC and Clang builds and their registered checks passed. The shipped
executable and 12 sibling runtime files match the GCC build byte for byte.
Three fresh owner paths passed on that installed copy: CPU CLI, GL CLI, and
actual GL menu/preset. Real default Space and raw mouse button 3 jumped,
released and landed; raw mouse button 2 retained forward movement. No bindings,
noclip, god mode or debugger were used in these movement checks.

| Entry | Renderer | Y after moving away | Y restored by load |
| --- | --- | ---: | ---: |
| CLI | CPU | 1861.689941 | 2015.968750 |
| CLI | GL | 1861.969727 | 2015.968750 |
| Actual menu/preset | GL | 1859.779419 | 2015.968750 |

All three Normal-slipgate runs survived teleport-fog expiry. Manual and
level-entry saves used original v5 text, 32,654–33,056 bytes. Loading after real
S-key movement restored the exact saved origin, angles, health, weapon,
ammunition and item flags. Inspected images retained the stock 25-shell HUD.
These checks do not qualify a physical controller or every original-progs save.
The nonfatal demo warning after classic load remains open.

A separate classic `e1m1` CPU visibility check showed no textured trigger
volumes in the corridor and slipgate view. It used temporary private navigation
bindings and noclip, so it is excluded from movement acceptance. Its
wall-occluded side view adds no visibility evidence. Two retail Q2 rerelease
`base1` CPU spawn views at 640×400 and 320×200 retained textured geometry,
pickup models and the see-through machinery window; these are bounded image
checks, not whole-map rendering parity.

All six public quits exited zero. Processes and the private display were
removed; owner settings and the installed artifact stayed unchanged. Evidence:
`owner-jump-save-visibility-brush-ff437555-proof.json`, `shipped-ff437555.json`
and `shipped-runtime-ff437555.json`.
