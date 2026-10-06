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
