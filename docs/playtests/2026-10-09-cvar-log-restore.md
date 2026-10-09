# THE-3166: gameplay cvars and host log restoration

The common cvar reader now accepts a saved gameplay dialect different from the active dialect. The saved dialect still drives name/value conversion; header, extent, name and payload checks remain. The actual old Q2 rerelease save contains a 16-byte QACV record with dialect Q3 and zero rows. It restores through the same table in all five dialects, preserving the current gravity. Additional legacy records restore `sv_gravity=543.25` to `g_gravity` and `skill=3` to `g_spSkill=4` in every dialect. Current capture/edit/restore matches; malformed records do not publish.

Bot log restoration creates its output directory and opens the host log for append at its current EOF. The old save's log position 5553 no longer requires an old host file to exist. Missing files and shorter existing logs work; ordinary non-resume opens still truncate. The saved gameplay and bot/navigation data are unchanged.

Both complete production and ASan/UBSan builds pass, as do the seven platform/core/archive/VFS/BSP/image/model checks in each build. Actual-library component fixtures pass strict GCC/Clang and bounded sanitizer checks. Evidence is retained in `/tmp/qa-the3166-cvar-log-build-20261009`, `/tmp/qa-the3166-cvars-20261009` and `/tmp/qa-the3166-botlog-20261009`.

Full private copied-owner-profile runs now print `Loaded saves/the2873-previous.sav.` for both old Q2 rerelease and Q3 saves. They subsequently exit with `Loopback endpoint is closed`, so complete restore and normal quit are still unproved. The receipt `/tmp/qa-the3166-cvar-log-restores-20261009.json` records that failure and owned-process cleanup. THE-3166 remains In Progress. This slice was not installed; urgent THE-3167 takes priority next.
