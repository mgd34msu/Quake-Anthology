# Movement, barrel combat and product playtests

MIKE-01, MIKE-03 and MIKE-08 pass the reported checks in the actual `qfiles/qa-c`. [Recorded observations](2026-10-05-movement-barrels-products.json) identify each tested build and edition. Input came through SDL; the debugger only observed state.

Q1 classic and rerelease `e1m1` both reached 200 units/second while grounded. Releasing forward cleared the command and stopped the player. Jumping produced upward velocity above 240 units/second, followed by a grounded landing and clean console quit. The canonical movement function had published a pointer to a stack-local result; phase callbacks read it after that result expired. Commit `9d8d8921` keeps the caller-owned output alive across all movement families. `d335e791` checks that lifetime and failure rollback in the existing gameplay target.

Normal walking, mouse aiming and blaster fire destroyed the selected authored barrel and caused a chain explosion in both Q2 editions. Its full actor identity disappeared from the registry, physical body and Source table. Classic CPU continued for 249 rendered frames and rerelease GL for 251, then each quit normally with exit zero. The earlier shutdown came from a queued looping sound whose new projectile had already been freed. Commit `59c89987` discards those expired loops across game families, following the originals' cleared looping-sound lifetime. Retained one-shot sounds still play.

Four actual product-menu visits offered one classic Quake option and no separate shareware option. Commit `88189e0c` keeps a single Quake product, accepts a valid pak0-only installation and obtains registration from the original `gfx/pop.lmp` content. These product-menu observations do not prove those then-failing custom launches; their remaining defects are tracked separately under MIKE-09/10.

The functional references are Quake `sv_phys.c`/`sv_user.c`, Quake II `g_misc.c` barrel damage and explosion lifetime, and Quake `common.c:COM_CheckRegistered`. These local X11 checks are not campaign, mod, network or performance qualification. No game assets are included in the evidence.
