# Unified cvar table (Q1, QW, Q2, Q2 rerelease, Q3)

Date: 2026-10-06. Companion data: `unified-cvars.csv` (same directory).

Owner rule: normalize to the Quake 3 cvar names, and keep every cvar the other games have that Q3 lacks. Any cvar name from any of the games works in any game and does what a player of that game expects. Example: `fov` (Q1/Q2) and `cg_fov` (Q3) are one setting, and both names work in every game.

Generator: `scratchpad/uc/build.py` + `groups.py` + `patch1.py` (session scratchpad 3ad56775...). Input: the seven per-source extractions (q1, qw, q2, q2rr, q3, ts, ports). The script can be re-run if an extraction changes.

## 1. Counts

| Item | Count |
|---|---|
| CSV rows (one per canonical cvar) | 1260 |
| Rows that group several names (canonical + aliases) | 195 |
| Alias names | 237 |
| Rows with a single name (no alias) | 1065 |
| Grouped rows whose canonical is the Q3 name | 152 of 195 (the other 43 have no Q3 counterpart, so the canonical is the most widely used source name) |
| New canonical names invented here | 4: `g_dm_skin_teams`, `g_dm_model_teams`, `g_dm_fixed_fov`, `g_dm_no_footsteps` (dmflags bits with no named cvar in any source) |
| Effect cells (rows x 5 games) | 6300 |
| ... cells stating "Same function" in a game that lacks the name natively | 2403 |
| ... cells stating an explicit no-op with a reason | 1685 |

Source names covered (every name of every extraction is a canonical or an alias; uncovered = 0 for all sources):

| Source | Unique cvar names | Not cvars, kept out of the CSV |
|---|---|---|
| Q1 (WinQuake + progs106 + rerelease QC) | 218 | none |
| QW | 230 | 21 userinfo/serverinfo keys (`pmodel`, `emodel`, `*ver`, `*ip`, `*spectator`, `*gamedir`, `*progs`, `*cheats`, `*version`, `needpass` key, `map`, `rj`, `axe`, `dq`, `dr`, `w_switch`, `b_switch`, `clan1`, `clan2`, `rankmin`, `rankmax`); 2 dead names (`sys_linerefresh` never registered, `scr_screensize` appears only in a header comment) |
| Q2 classic | 279 | none |
| Q2 rerelease (rerelease game/cgame + q2repro engine + original 1998 game) | 646 (692 entries; same name registered by several owners is one name) | none |
| Q3 | 545 | 81 botlib LibVars (separate namespace inside botlib, not console cvars; the `bot_*` ones are fed from the engine cvars of the same name) |
| quake-typescript | 462 | 2 summary rows of the extraction |
| C + Rust ports | 661 (families expanded: `gun_x/y/z`, `sv_master2..5`, `g_spScores1..5`) | none |

Families kept as one row: `ui_seat<1-4>_*` (11 rows, 4 seats each). Q2 `adr0..adr15` are aliases of Q3 `server1..server16`.

Implementation status from the CSV status columns:

| Status | quake-typescript | C port | Rust port |
|---|---|---|---|
| Name not registered in that implementation | 849 | 726 (50 of them registered by Rust only) | 835 (159 registered by C only) |
| Registered under one name | 356 | 463 | 361 |
| Several names of one group registered as separate cvars (not unified) | 30 (+4 explicitly noted, e.g. fov/cg_fov) | 57 | 51 |
| Registered only under an alias name | 6 | 5 | 4 |
| Unified by alias, same direction as this table | 1 row (`r_gamma` <- `gamma`, `vid_gamma`) | same row | same row |
| Unified by alias, direction reversed | 5 rows (`s_volume`, `s_musicvolume`, `s_khz`, `ogg_shuffle`, `ogg_menu_track`) | same 5 | same 5 |
| Hard-coded / settings-layer / mirror mapping instead of aliases | 7 rows (`g_gametype`, `sv_maxclients`, `cg_autoswitch`, `g_friendlyFire`, `dmflags`, `sv_maplist`, `fixedtime`) | none | none |

The 8 existing aliases (`gamma`, `vid_gamma`, `s_volume`, `s_musicvolume`, `ogg_volume`, `s_khz`, `ogg_shuffle`, `ogg_menu_track`) are the only cross-game aliases in any implementation today. Everything else in the 195 grouped rows still needs work.

## 2. CSV columns

`canonical, aliases, type, range_units, default_q1, default_qw, default_q2, default_q2rr, default_q3, flags, owner, effect_q1, effect_qw, effect_q2, effect_q2rr, effect_q3, conversion, ts_status, c_port_status, rust_port_status, sources`

- `aliases`: `name(games that register it)`; `TS` and `ports` mean the name is registered by quake-typescript or by a port. `none` = single name.
- `default_<game>`: the native default. When the game registers an alias name it is shown as `alias=value`. When the game has no native name: `not native; unset = X` (X is the Q3 or home-game default), `unset=...` with a stated neutral value, `not native (stored only)` for no-op rows, or `Anthology default X` for names that only the ports/TS define.
- `effect_<game>`: native meaning from the extraction when the game has the name (`[as alias]` marks the alias used there; `Unified note:` adds unified-engine behaviour). For a game without the name: the mapped behaviour, "Same function: ..." (the engine subsystem is shared), or "Accepted and stored; no effect: <reason>".
- `flags`: per game, per name, as registered in the source (`||` separates games).
- `ts_status` / `c_port_status` / `rust_port_status`: a verdict followed by each registered member name. Port entries show the dialects (`C=Q1,QW`, `R=*`). `ALIAS->x` marks an alias registration.
- `sources`: game and file:line of every member's registration.

## 3. The alias rule

1. There is ONE cvar table. Each canonical entry has: the canonical name, type, stored value (or "unset"), per-dialect default, per-dialect flags, and a list of aliases.
2. Name lookup is case-insensitive in every dialect (the Q3 rule). No two names in the sources differ only by case with different meanings: `g_redTeam`/`g_redteam` and `g_blueTeam`/`g_blueteam` are already one Q3 variable.
3. An alias resolves to its canonical entry on read, write, `set`/`seta`, `toggle`, `reset`, command-line `+set`, config exec, menu binding, guest registration, and save/load. Reads and writes through an alias apply that alias's conversion (section 5).
4. Archive: only the canonical name is written to config files. Aliases are never archived. Legacy configs that contain alias names (a Q1 `config.cfg` with `viewsize 100`, `gamma 0.8`, `volume 0.5`) load through the alias conversion into the canonical entry.
5. Guest registration (QuakeC has none; Q2 `gi.cvar`, Q3 `trap_Cvar_Register`, QVM, native modules): registering an alias name returns the canonical entry seen through that alias's conversion. Registering a canonical name with a different default records it as that dialect's default and never overrides a value the user set.
6. Each game's code reads the canonical entry through its dialect view: the dialect's alias name, unit conversion, default and flags. Game code does not keep a private copy.
7. `cvarlist` shows canonical names; `cvarlist -a` (or equivalent) also shows aliases with their targets.
8. Side-scoped aliases (the only exceptions to one-name-one-target): `password` and `allow_download` resolve by registry side. On the server, `password` means `g_password` and `allow_download` means `sv_allowDownload`. On the client, `password` is the client's userinfo key and `allow_download` means `cl_allowDownload`.

## 4. Default and flag rules

- **Unset follows the active game.** A canonical entry stores either an explicit user value or "unset". When unset, a read returns the active game's native default (for example `sensitivity` 3 in Q1/Q2, 5 in Q3; `fraglimit` 0 in Q1/Q2, 20 in Q3; `cl_run` 0 in Q1, 1 in Q2/Q3; `s_volume` 0.7 vs 0.8). A value the user sets explicitly applies in every game. This is what makes "fov 110 in Q1 is also fov 110 in Q3" work without clobbering per-game stock defaults.
- **Neutral defaults where a game lacks the feature.** Additive view-feel terms default to the value that reproduces stock behaviour in games that never had them (`cg_runroll`, `cg_bobpitch`, `cg_bobroll`, `cg_runpitch` unset = 0 in Q1/QW; `cl_rollangle` unset = 0 in Q3; movement constants unset = that engine's hard-coded pmove constant, e.g. `sv_friction` unset = 6 in Q2/Q3).
- **Per-dialect flags.** In a dialect that has a native member, that member's flags apply: Q2 `fov` is USERINFO|ARCHIVE, Q3 `cg_fov` is ARCHIVE. A dialect without a native member uses the home game's flags. CHEAT and LATCH therefore follow the active game: `cg_footsteps` is CHEAT in Q3 but `cl_footsteps` is not CHEAT in Q2R. ARCHIVE is the union, because a setting the user changes is persisted once, under the canonical name.
- **Per-game cheat resets are kept.** QW forces `r_fullbright`, `r_lightmap`, `r_draworder`, `r_ambient`, `r_drawflat` to 0 every frame and forces `r_wateralpha` to 1 unless serverinfo `watervis` is set. Q2 `CL_FixCvarCheats` and Q3 `sv_cheats`/CHEAT flags keep working. These apply when that game is the active dialect.
- **Info-key mapping.** USERINFO/SERVERINFO/SYSTEMINFO projection uses the wire key of the active protocol: the Q2 protocol sends `fov`, `skin`, `gender`, `hand`; QW sends `name`, `topcolor`, `bottomcolor`, `team`, `skin`, `rate`, `msg`, `noaim`; Q3 sends `name`, `model`, `headmodel`, `sex`, `color1`, `color2`, `handicap`, `snaps`. The TS alias mechanism currently refuses aliases onto info-flagged targets. This mapping is the missing piece that lifts that restriction.

## 5. Conversion rules

Conversion types used in the CSV `conversion` column:

| Type | Rule | Rows (examples) |
|---|---|---|
| identity | value copied | `fov`->`cg_fov`, `sv_gravity`->`g_gravity`, `sv_maxspeed`->`g_speed`, `hostname`->`sv_hostname`, `maxclients`->`sv_maxclients`, `cheats`->`sv_cheats`, `rcon_password`->`rconPassword`, `gl_picmip`->`r_picmip`, `scr_centertime`->`cg_centertime`, `freelook`->`cl_freelook`, `sv_rollangle`->`cl_rollangle` |
| reciprocal | alias = 1/canonical | `gamma`, `vid_gamma` -> `r_gamma` (lower = brighter in Q1/Q2) |
| bool inversion | alias = !canonical | `nosound`->`s_initsound`, `cl_predict`->`cg_nopredict`, `cd_nocd`/`nocdaudio`->`ogg_enable`, `_windowed_mouse`->`in_nograb`, `vid_hwgamma`->`r_ignorehwgamma`, `gl_drawsky`->`r_fastsky`, `cl_skipHud`->`cg_draw2D`, `noexit`->`g_dm_allow_exit`, `g_friendly_fire` <-> dmflags 256 |
| linear scale | alias = canonical x k | `cl_bob` = `cg_bobup` x 4; `ch_scale` = `cg_crosshairSize`/24; `cl_railtrail_time` (s) = `cg_railTrailTime` (ms)/1000; `host_framerate` (s) = `fixedtime` (ms)/1000; `sys_ticrate` = 1/`sv_fps`; `s_outputRate` Hz <-> `s_khz` (11/22/44/48); Q1/QW `scr_conspeed` = canonical x 100 |
| sign flip / axis remap | Q2 `gun_x` is the RIGHT axis, `gun_y` forward, `gun_z` negated up (verified quake-2/game/p_view.c:385-387); Q3 `cg_gunX` forward, `cg_gunY` left, `cg_gunZ` up (cg_weapons.c:1428-1430) | `gun_y`=`cg_gunX`, `gun_x`=-`cg_gunY`, `gun_z`=-`cg_gunZ` (and Q2R `cl_gun_*`) |
| enum map + detail slot | alias values that have no canonical value are stored in a per-alias detail slot. A read through that alias returns exactly what was written; other dialects see the mapped canonical value | `deathmatch`/`coop`/`teamplay`/`ctf`->`g_gametype`; `skill`<->`g_spSkill` (skill = clamp(g_spSkill-1,0,3)); `autoswitch`/`qts_weapon_autoswitch`->`cg_autoswitch`; `cl_gun` 2/3; `cl_footsteps` 2; `scr_draw2d`; `in_grab` 2; `s_enable` 1/2; `gl_dynamic` 2; `gl_shadows` 2; `samelevel` 2/3; `noexit` 2; `topcolor`/`bottomcolor` <-> `color1`/`color2` via a 14->7 colour table |
| bit view | the alias is one bit of a composite | dmflags rule rows (section 6.1) |
| composite | one alias writes several entries | `_cl_color`/`color` (shirt nibble -> `color1`, pants nibble -> `color2`); `vid_fullscreen` (Q2R mode index -> `r_fullscreen` + `r_mode`); `teamplay` 1/2 also writes `g_friendlyFire` |
| table lookup | resolve through resolution | `vid_mode`/`gl_mode`/`sw_mode`/Q2R `vid_fullscreen` -> width x height -> Q3 `r_mode` index or -1 + `r_customwidth`/`r_customheight` |
| unit conversion with game-specific consumer | canonical stored in Q3 units, older games read converted | `m_forward`/`m_side` (Q3 signed-byte usercmd vs Q1/Q2 units/s, factor 400/127); Q3 key speeds from `cl_forwardspeed`/`cl_sidespeed`/`cl_upspeed`/`cl_backspeed` x speedkey x 127/400 (stock defaults reproduce Q3's hard-coded 64/127) |
| side-scoped | target depends on registry side | `password`, `allow_download` (section 3.8) |

## 6. Conflict rules (same or similar name, different meaning)

1. **dmflags bit layout.** Q2 bit 16 = instant items, Q3 bit 16 = fixed FOV. Q2 bit 32 = same level, Q3 bit 32 = no footsteps. Rule: `dmflags` is not stored. It is a composite view over named rule entries: Q2R `g_no_health`, `g_no_items`, `g_dm_weapons_stay`, `g_dm_no_fall_damage`, `g_dm_instant_items`, `g_dm_same_level`, `g_friendlyFire` (inverted bit 256), `g_dm_spawn_farthest`, `g_dm_force_respawn` (view of `g_forcerespawn`), `g_no_armor`, `g_dm_allow_exit`, `g_infinite_ammo`, `g_dm_no_quad_drop` (inverted), `g_dm_no_quadfire_drop` (inverted), the Rogue `g_no_mines`, `g_dm_no_stack_double`, `g_no_nukes`, `g_no_spheres`, and the 4 new names (skin teams 64, model teams 128, fixed FOV, no footsteps). Unified layout = Q2 layout (1 .. 0x100000) + 0x200000 for no-footsteps. Reads encode in the active dialect's layout and writes decode in it (Q3: 8 -> no fall damage, 16 -> fixed FOV, 32 -> no footsteps).
2. **password / allow_download** are used on both client and server with different meanings. Resolved by registry side (section 3.8).
3. **scr_conspeed** units: Q1/QW pixels/s (300), Q2/Q3 screen-heights/s (3). Stored in Q2/Q3 units; Q1/QW read and write x100.
4. **scr_printspeed** units: Q1/Q2 chars/s (8), Q2R rerelease cgame seconds per char (0.04). Stored as chars/s; the Q2R cgame dialect reads and writes 1/value.
5. **crosshair** values: Q1 bool, QW 0..2 styles, Q2 0..3 pics, Q2R image index (default 3), Q3 `cg_drawCrosshair` 1..10. One integer; each game maps it modulo its image count. Hipnotic QC reads `crosshair == 2` to turn footsteps on; reads through `crosshair` return the raw written integer, so this keeps working.
6. **cl_bobup (Q1) vs cg_bobup/bob_up.** Q1 `cl_bobup` is the fraction of the bob cycle spent rising; Q3 `cg_bobup` and Q2 `bob_up` are amplitudes. They are separate entries. Q1 `cl_bob` (amplitude) is the alias of `cg_bobup` (x4).
7. **cl_chasecam (QW) vs chase_active (Q1).** QW's is in-eye spectator tracking, Q1's is a third-person camera. Separate entries; `chase_active` aliases `cg_thirdPerson`.
8. **Gun offset axes** differ between Q2 and Q3 (section 5). The Q2 classic extraction text calls `gun_x` "forward"; the source (p_view.c:385-387) says right. The CSV conversion follows the source.
9. **m_forward / m_side** defaults 1/0.8 (Q1/Q2) vs 0.25 (Q3) are different units, not different taste. Stored in Q3 units; unset follows the game default.
10. **skin vs model.** Q2 `skin` is `model/skin` (= Q3 `model`); QW `skin` is a skin name only (the QW model is fixed). `skin` aliases `model`. In the QW dialect it reads and writes only the skin part.
11. **teamplay / deathmatch / coop vs g_gametype.** Q1 `teamplay` encodes friendly-fire mode (1 none, 2 on) and Rogue tag modes 3..6. Q1/QW `deathmatch` 2/3 are rule variants. Q2/Q2R are bools. The Q3 enum is extended with 8 = campaign (Q1/Q2 single player) and 9 = coop. QuakeC and the Q2 game compare these integers exactly, so the raw written value is kept in the alias detail slot and returned on read while `g_gametype` stays in the matching family. Per-game unset default: Q1/Q2/Q2R 8, QW dedicated 0 (deathmatch 1), Q3 0. Latched to the next map in every dialect.
12. **skill (0..3) vs g_spSkill (1..5).** skill = clamp(g_spSkill-1, 0, 3); writing skill N stores N+1. Stock defaults line up (skill 1 = g_spSkill 2).
13. **noskins (QW) vs cl_noskins (Q2/Q2R).** QW `noskins` >1 means "show skins but never download"; Q2R `cl_noskins` 2 means "male/grunt and female/athena only". `noskins` aliases `cl_noskins`; game-specific values stay in the detail slot.
14. **r_draworder (software debug) vs gl_draworder (Q2R alpha-sort threshold).** Unrelated; separate entries.
15. **s_ambient (Q2R mode 0..3) vs ambient_level (Q1 volume).** Unrelated; separate entries.
16. **sv_airaccelerate 0.** Q2 0 means "classic Q2 air rules", QW 0.7 and Q3 1 are multipliers. The active game's interpretation applies; Q3 unset = 1.
17. **cl_maxfps 0 (QW)** means "derive from rate/80, clamp 30..72". `cl_maxfps` aliases `com_maxfps`; that zero rule applies only when QW is the active dialect.
18. **spectator** is a password string in QW and a 0/1 bool in Q2/Q2R. Non-empty and not "0" means spectate; the QW string is forwarded unchanged.
19. **maxclients defaults.** The Q2 game registers 4 and the server 1; Q2R game 8. The per-dialect default is the server/engine value. Guest registrations never override it (rule 3.5).
20. **Canonical direction reversed in the implementations.** TS, C and Rust use `volume`, `bgmvolume`, `s_outputRate`, `s_outputBits`, `s_outputChannels`, `music_shuffle`, `music_menu_track` and `r_shadows` as canonical. Under this table the canonical names are `s_volume`, `s_musicvolume`, `s_khz`, `sndbits`, `sndchannels`, `ogg_shuffle`, `ogg_menu_track` and `cg_shadows`. The old names become aliases.
21. **Port default disagreement:** `r_swapInterval` defaults to 0 in C and 1 in Rust; C uses CHEAT for `bot_saveroutingcache` while Rust uses none; Rust uses CHEAT for `bot_interbreed*` while C uses none; `ui_spSelection` defaults to `0` in C and `""` in Rust. The ports need to agree on one value per dialect.

## 7. Single-game cvars in other games

Every one of the 1065 single-name rows is accepted in every game. The effect column says which of three cases applies:

- **Same function** (2403 cells): the subsystem is engine-level and shared, e.g. console, network, sound mixer, input, renderer, frame timing, server limits and Anthology settings. Examples: `con_notifylines`, `cl_http_downloads`, `s_doppler`, `in_midi`, `r_textureMode`, `sv_floodProtect`, `cl_hudswap`, and the q2repro client features. Q1 view cvars (`v_kick*`, `v_idlescale`, `v_i*`, `chase_up`, `cl_rollangle`) are applied by the unified view layer, with a neutral unset value where stock behaviour must not change.
- **Mapped** to the target game's equivalent mechanism. Examples: Q1/QW movement cvars replace the Q2/Q3 pmove constants (`sv_friction` -> `pm_friction`, `sv_accelerate` -> `pm_accelerate`, `sv_wateraccelerate`, `sv_waterfriction`, `sv_stopspeed`, `edgefriction`, `sv_maxvelocity` with Q3 unset = no clamp); Q3 `cg_drawStatus` hides the Q1 sbar or the Q2 layout program; dmflags rule rows drive Q1/Q3 item rules through the Anthology item/spawn layer.
- **Accepted and stored, no-op** (1685 cells), each with a reason:
  - no such data: Q3 bezier patches, portals, shader stages and MD3 LOD in Q1/Q2 data; Q1 mirrors, eyes.mdl and scrolling sky in Q2/Q3; no railgun in Q1;
  - no software rasterizer: `d_subdiv16`, `r_maxedges`, `r_aliastrans*` and similar;
  - no platform path: DOS, Win9x/DirectX, svgalib, serial/modem, IPX, IRIX cvars;
  - memory pools: `com_hunkMegs`, `com_zoneMegs`, `com_soundMegs`;
  - Q3 UI-module state (`ui_*`) and Q3 cgame presentation options with no counterpart;
  - game rules that the target game's stock code lacks. These stay readable by that game's mods through QC `cvar()`, `gi.cvar` or `trap_Cvar_Register`.

The "Same function" and "mapped" cells for a game that lacks the cvar today describe behaviour the ports must implement. They are requirements, not current behaviour. The status columns show what exists now.

## 8. How a port implements this

1. **One table.** Replace the per-dialect registries (C `qa_cvars` per source plus the engine root; Rust one `CvarRegistry` per dialect; TS one registry per owner) with a single canonical table. Load it from this CSV, compiled into a generated C table and a generated Rust table so the two ports cannot drift. Each entry holds: name, type, value or unset, per-dialect default and flags, alias list with conversion kind and parameters, side scope, detail slot per alias, and the info-key name per protocol.
2. **One lookup.** `find(name)`: case-insensitive hash over canonical names and aliases. It returns `(entry, alias_or_null)`. All console, config, command-line, menu, save and guest-import paths go through it. The TS routing chain (`ApplicationConsoleRouting.owner`, console.ts:84) and the C/Rust per-source registries become views over this table, not separate stores. That removes the cross-registry problem that blocks `fov`<->`cg_fov` today.
3. **Dialect view.** `read(entry, dialect, alias)` = stored value, or the dialect default when unset, then the alias conversion. `write` applies the inverse conversion, stores, and records the detail slot. Game code (QC builtins, Q2 `gi.cvar` handles, Q3 `trap_Cvar_*`/vmCvar_t updates, native modules) receives a handle bound to (entry, dialect, alias), so each game reads the canonical entry in its own units.
4. **Archive** canonical names only, explicitly set values only (unset stays unset, so per-game defaults keep working). Old configs and saves load through aliases. Saved checkpoints that hold a now-aliased name as a real variable (Q3 `cg_*`, Q2 `fov`, Q1 `viewsize`) migrate on load into the canonical entry. TS currently rejects these as conflicts (core/cvars/index.ts:261).
5. **Conversions to add.** Both ports already have IDENTITY, RECIPROCAL_GAMMA and KILOHERTZ (C `qa_cvars_alias_register`; Rust `CvarAlias::Converted`). Add: linear scale, sign flip, bool invert, enum map with detail slot, bit view over a composite, composite write, resolution lookup, side scope. All are pure functions of (value, dialect).
6. **Info strings.** Build userinfo/serverinfo/systeminfo from the canonical table using the active protocol's key names (section 4). Then aliases can target info-flagged entries, which the current TS rule forbids.
7. **Order of work**, by user-visible impact:
   1. reverse the 8 existing aliases where the direction is wrong;
   2. view/input groups: `cg_fov`, `cg_viewsize`, `cg_drawCrosshair`, crosshair offsets/size, `cg_drawGun`, gun offsets, bob/run/roll, third person, `cl_freelook`, `cl_mouseAccel`, `in_nograb`;
   3. server rules: `g_gametype`, `g_spSkill`, `g_gravity`, `g_speed`, `sv_hostname`, `sv_maxclients`, `sv_cheats`, `rconPassword`, `g_password`, the dmflags composite;
   4. renderer `gl_*` -> `r_*` aliases and the mode-table resolver;
   5. sound/music aliases;
   6. the single-game "Same function" behaviours, per subsystem.

## 9. Limits of this table

- The Q2 rerelease engine itself is closed source. Its own cvars come from q2repro's rerelease-compatible client as a stand-in (noted in the q2rr extraction).
- Cvars registered at runtime by guests (QuakeC mod declarations, Q2 game DLL `gi.cvar`, Q3 QVM `trap_Cvar_Register`, Q3 gear initial cvars, `set`/`seta` user variables) cannot be listed statically. They enter the same table at runtime and follow the same alias lookup.
- Meanings come from the extractions. Where an extraction's description disagreed with the source and the conversion depends on it, the source was checked and wins (gun offset axes; dmflags bit values from quake-2/game/q_shared.h:1016-1040 and quake-iii-arena/code/game/bg_public.h:657-659).
- Type and range for single-name rows are inferred from the default and the meaning text. Treat them as hints and confirm against the source line in `sources` before generating code from them.
