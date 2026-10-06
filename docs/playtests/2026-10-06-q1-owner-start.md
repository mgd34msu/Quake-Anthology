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
