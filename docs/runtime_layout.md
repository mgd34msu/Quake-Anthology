# SharedSimulation layout (runtime.ts -> C)

Source: `../quake-anthology-ts/src/app/bootstrap/simulation/runtime.ts` (8,752 lines when this table was generated on 2026-11-02; the TS tree is edited concurrently, so re-run the outline before trusting a line number). One TS class, `SharedSimulation`, ported as one C type, `qa_app_shared_simulation`. The C is split by TS region, not by game:

| C file | Region | Owner |
|---|---|---|
| `include/qa/app/bootstrap/simulation/shared_simulation.h` | public type and public methods | A |
| `src/app/bootstrap/simulation/shared_simulation_internal.h` | the struct (every TS field, TS order), qa_ss_map, every cross-file method declaration, dependency gaps | A |
| `src/app/bootstrap/simulation/shared_simulation_support.c` | qa_ss_map (TS Map/Set/RevisionMap), runtime.ts free helpers, modClientListeners set | A |
| `src/app/bootstrap/simulation/shared_simulation_core.c` | mod clients, constructor, load, close, actor infrastructure, SimulationEvents super binding | A |
| `src/app/bootstrap/simulation/shared_simulation_plan.c` | step() deadline planning (split out so its checks link alone) | A |
| `src/app/bootstrap/simulation/shared_simulation_source.c` | createSource (six source kinds), q1ActorHost, q2ActorHost | A |
| `src/app/bootstrap/simulation/shared_simulation_step.c` | step, stepAsync, native Q2 frames, selected-source frames, snapshot | A |
| `src/app/bootstrap/simulation/shared_simulation_access.c` | access structs for the network adapters and bot shells | A |
| `src/app/bootstrap/simulation/shared_simulation_worlds.c` | B1: monsters, foreign worlds, guest monsters, native Q2 guest bridge, match rules (tag, lmctf, deathball, horde), combat registration | done |
| `src/app/bootstrap/simulation/shared_simulation_arsenal.c` | B2: selected arsenals, weapons, pickups, equipment, grapple, hand grenades, inventory UI, presentation | B2 |
| `src/app/bootstrap/simulation/shared_simulation_players.c` | B3: players, admission, characters, views, userinfo, commands, travel, checkpoint, restore, mods | B3 |
| `src/app/bootstrap/simulation/shared_simulation_checkpoint.c` | B3: checkpoint, checkpointLoading, checkpointFromSource (the world:simulation record as a checkpoint-value tree in TS key order), the q2-native original provider, nativeRestorePrelude, restoreNativeLoading, restore, and the B1 Q1-checkpoint rows | B3 |
| `src/app/bootstrap/simulation/shared_simulation_mods.c` | B3: load's `SessionMods.open({...})` (ModHostServices, engine fragment, owner-binding adapter) | B3 |
| `src/app/bootstrap/simulation/source_strategy.{h,c}` | source-strategy.ts: `qa_app_source_strategy` tables per source class (`sim->strategy`, a pointer to the class table, set with the source); all 226 TS members ported (D1, D1b, D1c, D1d); typed-access sweep in "Typed access sweep" below; members that need a runtime file's static helpers are defined beside them as `qa_ss_<class>_<member>` | D1c |
| `src/app/bootstrap/simulation/q3/{engine.h,qvm_engine.c,native_guest_source.c,server_traps.c}` | q3/engine.ts (Q3GameEngine table), qvm-engine.ts (Q3QvmEngine), native-guest-source.ts (Q3NativeGuestSource, q3NativeProfile, prepareQ3Native), server-traps.ts (Q3ServerTraps); q3/guest_runtime.c (TS Q3ServerGame, C name qa_app_q3_qvm_server_game) drives either engine | D1b |
| `src/app/bootstrap/simulation/checkpoint_values.c` | value trees of the live TS records checkpointFromSource encodes (no TS file: TS encodes the objects themselves) | B3 |

Naming: method `fooBar` is `qa_app_shared_sim_foo_bar(qa_app_shared_simulation *sim, ...)`. The constructor is `qa_app_shared_simulation_create`, `static load` and `loadSimulation` are `qa_app_shared_simulation_load`. Signatures already used by phase A code are declared in `shared_simulation_internal.h` (phase B defines them with exactly that signature); the rest are new declarations the phase B porter adds there. A phase B method is "not done" until it is ported with every branch and checked against the TS tests.


## SourceStrategy members (source-strategy.ts -> source_strategy.{h,c}, D1c)

Every member of the TS `SourceStrategy` interface (226), the classes that override it (LoadingStrategy implements all;
LoadedStrategy overrides none), its C table member, and its status. "done" means the member is a `qa_app_source_strategy`
field, each class table carries its TS override, and the runtime C calls `sim->strategy->member(...)` where TS calls
`this.strategy.member(...)`. Since D1c, `sim->strategy` is a pointer to the class's shared table (built once; see
`qa_app_create_source_strategy`), so the simulation struct's layout no longer depends on the table's size. Member
bodies that need a runtime file's static helpers are defined beside them as `qa_ss_<class>_<member>` and declared in
shared_simulation_internal.h; the rest are static in source_strategy.c. C signature conventions: a TS `null` /
`undefined` result is a `false` return with an out parameter; list results are scratch arrays or caller buffers.

| TS member | Overridden by | C member | C status |
|---|---|---|---|
| `userinfo` | Q1, QuakeC, Q2, Q2Native, Q3, Q3Qvm | `userinfo` | done |
| `storeUserinfo` | Q1, QuakeC, Q2, Q2Native, Q3, Q3Qvm | `store_userinfo` | done |
| `updateUserinfo` | Q1, QuakeC, Q2, Q2Native, Q3, Q3Qvm | `update_userinfo` | done |
| `pause` | Q1, QuakeC | `pause` | done |
| `admitClient` | Q1, QuakeC, Q2, Q2Native, Q3, Q3Qvm | `admit_client` | done |
| `prepareBot` | Q1, Q2, Q3 | `prepare_bot` | done |
| `initialArsenalState` | Q1, Q2 | `initial_arsenal_state` | done |
| `attachEntity` | Q2 | `attach_entity` | done |
| `configureAdmitted` | Q1, Q2 | `configure_admitted` | done |
| `attachForeignCharacter` | Q1 | `attach_foreign_character` | done |
| `admitCarry` | Q1, Q2 | `admit_carry` | done |
| `enterWorld` | Q1, Q2 | `enter_world` | done |
| `captureEntryCarry` | Q1 | `capture_entry_carry` | done |
| `movementBindings` | Q1, QuakeC | `movement_bindings` | done |
| `q2Gibbed` | Q2 | `q2_gibbed` | done |
| `q3Client` | Q3 | `q3_client` | done |
| `suppliesPunch` | QuakeC | `supplies_punch` | done |
| `skipsTriggerTouch` | QuakeC, Q3 | `skips_trigger_touch` | done |
| `lmctf` | Q2 | `lmctf` | done |
| `preparesQ1Clients` | Q1, QuakeC | `prepares_q1_clients` | done |
| `intermissionFading` | Q2 | `intermission_fading` | done |
| `fadeIntermission` | Q2 | `fade_intermission` | done |
| `beginTimedFrame` | Q1 | `begin_timed_frame` | done |
| `beginGuestFrame` | QuakeC | `begin_guest_frame` | done |
| `beginActorTurns` | Q2, Q3 | `begin_actor_turns` | done |
| `intermissionActive` | Q1 | `intermission_active` | done |
| `requestIntermissionExit` | Q1 | `request_intermission_exit` | done |
| `preFrame` | Q1 | `pre_frame` | done |
| `preThink` | Q1 | `pre_think` | done |
| `playerThink` | Q3 | `player_think` | done |
| `tracksGravity` | Q2 | `tracks_gravity` | done |
| `gravityScale` | Q2 | `gravity_scale` | done |
| `movementApplied` | Q2 | `movement_applied` | done |
| `afterPlayerCommand` | Q1, QuakeC, Q2 | `after_player_command` | done |
| `beforeClientActor` | Q1, QuakeC | `before_client_actor` | done |
| `retouch` | Q1 | `retouch` | done |
| `spectating` | Q2 | `spectating` | done |
| `runPlayerActor` | Q3 | `run_player_actor` | done |
| `watchesTransitions` | Q2 | `watches_transitions` | done |
| `selectedPlayer` | Q1, QuakeC, Q2, Q2Native, Q3, Q3Qvm | `selected_player` | done |
| `q3GameType` | Q3, Q3Qvm | `q3_game_type` | done |
| `q3ServerInfo` | Q3 | `q3_server_info` | done |
| `primaryEquipment` | Q3 | `primary_equipment` | done |
| `hasEquipmentEffects` | QuakeC, Q2Native, Q3, Q3Qvm | `has_equipment_effects` | done |
| `weaponDamageFactor` | QuakeC, Q2Native, Q3, Q3Qvm | `weapon_damage_factor` | done |
| `equipmentDamageFactor` | Q3 | `equipment_damage_factor` | done |
| `equipmentFiringDelay` | QuakeC, Q2Native, Q3, Q3Qvm | `equipment_firing_delay` | done |
| `weaponDelay` | Q2Native, Q3, Q3Qvm | `weapon_delay` | done |
| `equipmentDelay` | Q2Native, Q3Qvm | `equipment_delay` | done |
| `curvesEnabled` | Q3 | `curves_enabled` | done |
| `playerCurveClip` | Q3 | `player_curve_clip` | done |
| `friendlyFire` | Q2, Q3 | `friendly_fire` | done |
| `knockback` | Q3 | `knockback` | done |
| `intermissionQueued` | Q1, Q2, Q3 | `intermission_queued` | done |
| `checkHurtCarrier` | Q3 | `check_hurt_carrier` | done |
| `checkObeliskAttack` | Q3 | `check_obelisk_attack` | done |
| `quadFactor` | QuakeC, Q3 | `quad_factor` | done |
| `proximityTimeout` | Q3 | `proximity_timeout` | done |
| `returnPickup` | Q3 | `return_pickup` | done |
| `teleportSpawn` | Q1, QuakeC, Q2, Q2Native, Q3, Q3Qvm | `teleport_spawn` | done |
| `dropObjectives` | Q1, QuakeC, Q2, Q2Native, Q3, Q3Qvm | `drop_objectives` | done |
| `originalPlayer` | Q2Native, Q3Qvm | `original_player` | done |
| `setMaxHealth` | Q1, QuakeC, Q2, Q3, Q3Qvm | `set_max_health` | done |
| `installPickupAdmission` | Q1, Q2 | `install_pickup_admission` | done |
| `supplyFamily` | Q1, QuakeC, Q2, Q2Native | `supply_family` | done |
| `ownsSupply` | QuakeC, Q3Qvm | `owns_supply` | done |
| `identitySupply` | Q3Qvm | `identity_supply` | done |
| `extraReplacedItems` | Q2 | `extra_replaced_items` | done |
| `animatesWeaponFire` | Q2Native, Q3Qvm | `animates_weapon_fire` | done |
| `noteFired` | Q1 | `note_fired` | done |
| `weaponNoise` | Q2 | `weapon_noise` | done |
| `allowsAutoSwitch` | Q1 | `allows_auto_switch` | done |
| `cvars` | Q1, QuakeC, Q3, Q3Qvm | `cvars` | done |
| `q1PowerupExpires` | Q1, QuakeC, Q2, Q3 | `q1_powerup_expires` | done |
| `damageModifier` | QuakeC | `damage_modifier` | done |
| `runsQ3Powerups` | Q3, Q3Qvm | `runs_q3_powerups` | done |
| `equipmentSuppression` | QuakeC, Q2Native, Q3, Q3Qvm | `equipment_suppression` | done |
| `monsterNoise` | Q2 | `monster_noise` | done |
| `ownsEquipmentPlayers` | Q2Native, Q3Qvm | `owns_equipment_players` | done |
| `sourceInventory` | Q2Native | `source_inventory` | done |
| `playerAvailable` | Q2Native, Q3Qvm | `player_available` | done |
| `stepsHandGrenades` | Q2Native | `steps_hand_grenades` | done |
| `teleportBits` | Q2Native, Q3Qvm | `teleport_bits` | done |
| `notarget` | Q1, QuakeC, Q2Native, Q3, Q3Qvm | `notarget` | done |
| `cloakAlpha` | Q2 | `cloak_alpha` | done |
| `godMode` | Q1 | `god_mode` | done |
| `weaponEvent` | Q2 | `weapon_event` | done |
| `hasEnemy` | Q2 | `has_enemy` | done |
| `teamDamageEnabled` | Q2 | `team_damage_enabled` | done |
| `matchDamageEffects` | Q2 | `match_damage_effects` | done |
| `requiredMonsterNoise` | Q2 | `required_monster_noise` | done |
| `requireQ2` | Q2 | `require_q2` | done |
| `pushed` | Q2 | `pushed` | done |
| `setEntityGravity` | Q2 | `set_entity_gravity` | done |
| `monstersSearching` | Q2 | `monsters_searching` | done |
| `q1Game` | Q1 | `q1_game` | done |
| `selectWeapon` | Q1 | `select_weapon` | done |
| `connectedClients` | QuakeC, Q2Native, Q3Qvm | `connected_clients` | done |
| `playerActors` | Q2Native, Q3Qvm | `player_actors` | done |
| `requireLoaded` | Loaded | `require_loaded` | done (D1d) |
| `assertSaveReady` | Q3, Q3Qvm | `assert_save_ready` | done (D1d) |
| `projectsMonsters` | Q2Native, Q3Qvm | `projects_monsters` | done |
| `usesGuestMonsters` | QuakeC, Q2Native, Q3Qvm | `uses_guest_monsters` | done |
| `reconstructsActor` | Q2Native | `reconstructs_actor` | done (D1d) |
| `checkpointProviders` | Q1, Q2, Q3 | `checkpoint_providers` | done (D1d) |
| `checkpointGuests` | QuakeC, Q3Qvm | `checkpoint_guests` | done (D1d) |
| `checkpointExtras` | Q2Native, Q3Qvm | `checkpoint_extras` | done (D1d) |
| `nativeClients` | Q2Native | `native_clients` | done (D1d) |
| `sourceCvarSnapshots` | Q1 | `source_cvar_snapshots` | done (D1d) |
| `expectedDeclaration` | Q3Qvm | `expected_declaration` | done (D1d) |
| `legacyCombat` | Q3Qvm | `legacy_combat` | done (D1d) |
| `requireInventoryProjection` | Q2Native, Q3Qvm | `require_inventory_projection` | done (D1d) |
| `inventoryCoverage` | Q2Native, Q3Qvm | `inventory_coverage` | done (D1d) |
| `travelSource` | Q1, QuakeC, Q2, Q3, Q3Qvm | `travel_source` | done |
| `travelers` | Q3Qvm | `travelers` | done |
| `travelPlayerState` | Q1, QuakeC, Q2, Q3, Q3Qvm | `travel_player_state` | done |
| `guestHealth` | Q3Qvm | `guest_health` | done |
| `setGuestHealth` | Q3Qvm | `set_guest_health` | done |
| `guestTravelPlayers` | Q3Qvm | `guest_travel_players` | done |
| `canonicalTeam` | Q1, QuakeC, Q2, Q2Native, Q3, Q3Qvm | `canonical_team` | done |
| `declaresMatch` | Q1, QuakeC, Q2, Q2Native, Q3, Q3Qvm | `declares_match` | done |
| `matchScore` | Q1, QuakeC, Q2, Q2Native, Q3, Q3Qvm | `match_score` | done |
| `setMatchScore` | Q1, QuakeC, Q2, Q2Native, Q3, Q3Qvm | `set_match_score` | done |
| `setMatchTeam` | Q1, QuakeC, Q2, Q2Native, Q3, Q3Qvm | `set_match_team` | done |
| `hasGuestUi` | Q2Native, Q3Qvm | `has_guest_ui` | done |
| `guestUi` | Q2Native, Q3Qvm | `guest_ui` | done |
| `uiSubject` | Q2 | `ui_subject` | done |
| `clientUi` | QuakeC | `client_ui` | done |
| `uiPowerups` | Q1, QuakeC, Q2, Q3 | `ui_powerups` | done |
| `arsenalUi` | Q1, Q2, Q3 | `arsenal_ui` | done |
| `guestView` | Q2Native, Q3Qvm | `guest_view` | done |
| `chasing` | Q2 | `chasing` | done |
| `cutsceneFov` | Q2 | `cutscene_fov` | done |
| `overrideView` | QuakeC | `override_view` | done |
| `foreignCharacterDeath` | Q3 | `foreign_character_death` | done |
| `rereleaseViewHeight` | Q2 | `rerelease_view_height` | done |
| `presentations` | Q2Native, Q3 | `presentations` | done |
| `guestWeaponViews` | Q2Native, Q3Qvm | `guest_weapon_views` | done |
| `usesQ2Views` | Q2 | `uses_q2_views` | done |
| `q2PlayerEntity` | Q2 | `q2_player_entity` | done |
| `q1Player` | Q1 | `q1_player` | done |
| `q1Colors` | Q1 | `q1_colors` | done |
| `weaponVisible` | Q2Native, Q3Qvm | `weapon_visible` | done |
| `guestBobCycle` | Q3Qvm | `guest_bob_cycle` | done |
| `rendersOwnIntermission` | Q2Native, Q3, Q3Qvm | `renders_own_intermission` | done |
| `forwardsCommands` | Q2Native | `forwards_commands` | done |
| `preCommand` | Q1, QuakeC, Q2, Q2Native, Q3 | `pre_command` | done |
| `afterCommand` | Q1, Q2, Q3 | `after_command` | done |
| `setIdentity` | Q1, Q2 | `set_identity` | done |
| `setFieldOfView` | Q2, Q2Native | `set_field_of_view` | done |
| `angleDelta` | Q3, Q3Qvm | `angle_delta` | done |
| `commandMovement` | Q2Native | `command_movement` | done |
| `guestGrantExclusions` | Q3Qvm | `guest_grant_exclusions` | done |
| `grantProfile` | Q1, Q2, Q2Native, Q3 | `grant_profile` | done |
| `sourceItem` | Q2 | `source_item` | done |
| `grantedWeapon` | Q2 | `granted_weapon` | done |
| `baseArsenal` | Q1, QuakeC, Q2, Q3 | `base_arsenal` | done |
| `q1Client` | Q1 | `q1_client` | done |
| `q1Impulse` | Q1 | `q1_impulse` | done |
| `weaponsPaused` | Q2 | `weapons_paused` | done |
| `stepSelectedHoldable` | Q3 | `step_selected_holdable` | done |
| `primaryWeaponStep` | Q1, Q2Native, Q3 | `primary_weapon_step` | done |
| `weaponFacts` | Q1, QuakeC, Q2, Q3 | `weapon_facts` | done |
| `dropsImpulse` | Q1 | `drops_impulse` | done |
| `requestsWeaponDirectly` | Q1, QuakeC, Q2 | `requests_weapon_directly` | done |
| `alwaysRequestsWeapon` | Q2 | `always_requests_weapon` | done |
| `ownsContacts` | QuakeC, Q3 | `owns_contacts` | done |
| `quakeWorldContacts` | QuakeC | `quake_world_contacts` | done |
| `jumpVoice` | Q3 | `jump_voice` | done |
| `animation` | QuakeC | `animation` | done |
| `attackArsenal` | QuakeC | `attack_arsenal` | done |
| `playerWeapon` | Q1, Q2 | `player_weapon` | done |
| `kind` | Q1, QuakeC, Q2, Q2Native, Q3, Q3Qvm | `kind` | done |
| `grantQuad` | Q1, Q3 | `grant_quad` | done |
| `admitsGuestMonsters` | QuakeC, Q2Native, Q3, Q3Qvm | `admits_guest_monsters` | done |
| `assertSavable` | Q2Native | `assert_savable` | done (D1d) |
| `captureGuestSave` | Q2Native | `capture_guest_save` | done (D1d) |
| `originalSave` | Q2Native | `original_save` | done (D1d) |
| `restoreLoading` | Q2Native | `restore_loading` | done (D1d) |
| `restoreGuest` | Q2Native | `restore_guest` | done (D1d) |
| `guestEdition` | Q2Native | `guest_edition` | done |
| `monsterProxy` | Q2Native, Q3Qvm | `monster_proxy` | done (D1d) |
| `asQ1` | Q1 | `as_q1` | done |
| `asQ2` | Q2 | `as_q2` | done |
| `asQuakeC` | QuakeC | `as_quakec` | done |
| `asQ3` | Q3 | `as_q3` | done |
| `asQ3Qvm` | Q3Qvm | `as_q3_qvm` | done |
| `asQ2Native` | Q2Native | `as_q2_native` | done |
| `stepRoute` | Q2Native, Q3Qvm | `step_route` | done |
| `settles` | Q3 | `settles` | done |
| `syncGravity` | Q2 | `sync_gravity` | done |
| `afterStep` | Q2 | `after_step` | done |
| `finishFrame` | Q2, Q3 | `finish_frame` | done |
| `concludeFrame` | Q1, QuakeC | `conclude_frame` | done |
| `q1Powerups` | Q1 | `q1_powerup` | done |
| `foreignPowerups` | Q1, Q3 | `foreign_powerups` | done |
| `q1CharacterFrame` | Q1 | `q1_character_frame` | done |
| `teamGame` | Q1, QuakeC, Q2, Q2Native, Q3, Q3Qvm | `team_game` | done |
| `serverCvars` | Q1, QuakeC, Q2, Q2Native, Q3, Q3Qvm | `server_cvars` | done |
| `serverSettingsOwner` | Q3 | `server_settings_owner` | done |
| `clientOf` | Q2Native, Q3Qvm | `client_of` | done |
| `disconnect` | Q1, QuakeC, Q2, Q2Native, Q3 | `disconnect` | done |
| `dropInventory` | Q1 | `drop_inventory` | done |
| `respawn` | Q1, Q2, Q3 | `respawn` | done |
| `keepsAngleDeltas` | Q3, Q3Qvm | `keeps_angle_deltas` | done |
| `telefragCause` | Q1 | `telefrag_cause_q1` | done |
| `cutsceneControl` | Q1 | `cutscene_control` | done |
| `checkOutputClaim` | QuakeC, Q2Native, Q3Qvm | `check_output_claim` | done |
| `guestGrounded` | Q2Native, Q3Qvm | `guest_grounded` | done |
| `guestClientView` | Q2Native, Q3Qvm | `guest_client_view` | done |
| `outputsSuspended` | QuakeC, Q2, Q2Native, Q3Qvm | `outputs_suspended` | done |
| `handoff` | Q1, QuakeC, Q2, Q2Native, Q3, Q3Qvm | `handoff` | done |
| `equipmentPowerups` | Q1, Q2Native, Q3, Q3Qvm | `equipment_powerups` | done |
| `refineWeaponInput` | Q2 | `refine_weapon_input` | done |
| `guestWeaponAnimation` | Q2Native, Q3Qvm | `guest_weapon_animation` | done |
| `writeCharacterAnimation` | Q3 | `write_character_animation` | done |
| `characterPose` | Q1 | `character_pose` | done |
| `reactionStart` | Q3 | `reaction_start` | done |
| `reactionEnd` | Q1, Q2 | `reaction_end` | done |
| `damageCommitted` | QuakeC, Q2 | `damage_committed` | done |
| `stopSpeed` | Q2 | `stop_speed` | done |
| `restoreContinuation` | Q3 | `restore_continuation` | done (D1d) |
| `restoreFoundation` | Q1, Q2 | `restore_foundation` | done (D1d) |
| `restoreGravity` | Q2 | `restore_gravity` | done (D1d) |
| `ammoTimer` | Q3 | `ammo_timer` | done (D1d) |
| `legacyQ3Family` | Q1, Q2 | `legacy_q3_family` | done |

## Typed access sweep (D1d, 2026-11-03)

Every remaining `sim->strategy->as_*()` call and `ss_source_is(sim, ...)` call in shared_simulation_*.c (168 sites:
131 `as_*()` calls, of which 6 are inside the `ss_source_is` helper, and 34 `ss_source_is` calls in
shared_simulation_arsenal.c), grouped by enclosing C function. Each is a typed access TypeScript also makes at the same
place: either the TS method of the same name calls `strategy.asX()` (checked by
build/agents/d1d-sim/tmp/sweep.py against runtime.ts), or the C function is a closure, host-object member or helper
whose TS counterpart (named in the last column) makes it. Several TS counterparts reach `asX()` through a runtime
accessor (`q1Source()`, `q3Source()`, `q3Guest()`, `q2Native()`, `quakecSource()`) or through the foreign-rules host's
`sourceKind` (= `strategy.kind`).

Converted by D1d (not TS typed access): `beginSourceRoundSettlement` / `completeSourceRoundSettlement` now call
`strategy->settles(source)` (TS) instead of `as_q3 && source.q3 == settlement.source`; `prepareGuestMonsters` and
`restoreSelectedMonsters` call `admits_guest_monsters` instead of a four-kind `as_*` test; `admitGuestMonsters` and
`restoreSelectedMonsters` use the `monster_proxy` member instead of classic/QVM `as_*` branches; the Q2 character's
powerups read `strategy->q1_powerup` (TS `this.strategy.q1Powerups(actor)`) instead of `as_q1`; `ss_update_view`'s Q2
intermission test moved into the Q2 strategy's setFieldOfView (TS has it only there); restore's storage choice and
`hydrateSelectedQ1` use `owns_supply` (TS `ownsSupply()`) instead of `as_quakec || as_q3_qvm`;
`qa_app_shared_sim_source_kind` returns `strategy->kind` (TS `host.sourceKind`) instead of a `source.kind` table. The
22 save/restore members replaced their `as_*` branches in shared_simulation_checkpoint.c, shared_simulation_players.c,
shared_simulation_core.c and shared_simulation_worlds.c.

`source.kind` switches: `wl_source_identity` (shared_simulation_worlds.c) is kept. It is TS `this.source !== source`
object identity (primaryMatchPlayer, runtime.ts 9811), not per-source behavior: C's SourceRuntime is a tagged struct,
so identity is the kind plus that kind's owner pointer. The remaining `sim->source.kind = ...` writes are the TS
`setSource` installations (core.c loading, worlds.c Q3 restart).

| C file | C function | typed access | sites | TS counterpart |
|---|---|---|---|---|
| access.c | `ss_q1_source` | asQ1 | 1 | TS `q1Source` (runtime.ts 10011) makes `strategy.asQ1()` |
| access.c | `ss_q3_source` | asQ3 | 1 | TS `q3Source` (runtime.ts 10894) makes `strategy.asQ3()` |
| access.c | `ss_q3_guest` | asQ3Qvm | 1 | TS `q3Guest` (runtime.ts 10684) makes `strategy.asQ3Qvm()` |
| access.c | `qa_app_shared_sim_q2_guest_world` | asQ2Native | 1 | stepNativeQ2Async/q2Native(): `this.strategy.asQ2Native()` (runtime.ts 10616) |
| access.c | `ss_bot_q1_source` | asQ1 | 1 | bot host `sim.q1Source()` = `this.strategy.asQ1()` (10012) |
| access.c | `ss_bot_q3_source` | asQ3 | 1 | bot host `sim.q3Source()` = `this.strategy.asQ3()?.game` (10895) |
| access.c | `ss_bot_q3_guest` | asQ3Qvm | 1 | bot host `sim.q3Guest()` = `this.strategy.asQ3Qvm()?.game` (10685) |
| access.c | `ss_q3_baseline` | asQ3 | 1 | prediction host `q3Source()` (10895) |
| access.c | `ss_q3_arsenal_runtime` | asQ3 | 1 | prediction host `q3Source()` (10895) |
| access.c | `ss_prediction` | asQuakeC | 1 | player-movement.ts 268 `simulation.quakecSource()` = `this.strategy.asQuakeC()?.game` (10009) |
| access.c | `ss_nav_guest` | asQ3Qvm | 1 | navigation host `q3Guest()` (10685) |
| access.c | `ss_nav_q3_guest` | asQ3Qvm | 1 | navigation host `q3Guest()` (10685) |
| access.c | `ss_nav_q1_game` | asQ1 | 1 | navigation host `q1Source()` (10012) |
| access.c | `ss_nav_q3_source` | asQ3 | 1 | navigation host `q3Source()` (10895) |
| access.c | `ss_m_native_edition` | asQ2Native | 1 | monster host `q2Native()` = `this.strategy.asQ2Native()?.game` (10019) |
| arsenal.c | `ss_source_is` | asQ1, asQ2, asQ2Native, asQ3, asQ3Qvm, asQuakeC | 6 | the typed-access helper itself (asQ1() ... asQ2Native() !== null) |
| arsenal.c | `qa_app_shared_sim_q1_combat_source` | asQ1, asQuakeC | 2 | TS `q1CombatSource` (runtime.ts 3244) makes `strategy.asQ1()` |
| arsenal.c | `qa_app_shared_sim_require_qvm_weapons` | asQ3Qvm | 1 | TS `requireQvmWeapons` (runtime.ts 3261) makes `strategy.asQ3Qvm()` |
| arsenal.c | `qa_app_shared_sim_arsenal_observation` | asQ2Native, asQ3Qvm | 2 | TS `arsenalObservation` (runtime.ts 3270) makes `strategy.asQ2Native()` |
| arsenal.c | `qa_app_shared_sim_sync_selected_q1_health_limit` | asQuakeC | 1 | TS `syncSelectedQ1HealthLimit` (runtime.ts 7411) makes `strategy.asQuakeC()` |
| arsenal.c | `qa_app_shared_sim_run_quake_world_new_missile` | asQuakeC | 1 | TS `runQuakeWorldNewMissile` (runtime.ts 8369) makes `strategy.asQuakeC()` |
| arsenal.c | `ss_q3_native_client` | asQ3 | 1 | helper of Q3Strategy members (weaponFacts ...): TS reads `this.source` of the Q3 class; C guards the shared helper with asQ3 |
| arsenal.c | `ss_q3_client` | asQ3 | 1 | helper of Q3Strategy members (equipmentPowerups ...): TS reads `this.source` of the Q3 class; C guards the shared helper with asQ3 |
| arsenal.c | `qa_app_shared_sim_bind_source_inventory` | asQ2Native | 1 | TS `bindSourceInventory` (runtime.ts 4564) makes `strategy.asQ2Native()` |
| arsenal.c | `ss_native_select` | asQ3Qvm | 1 | arsenalObservation: `this.strategy.asQ3Qvm()` (3278) |
| arsenal.c | `qa_app_shared_sim_bind_selected_ammo` | asQ3 | 1 | TS `bindSelectedAmmo` (runtime.ts 3961) makes `strategy.asQ3()` |
| arsenal.c | `qa_app_shared_sim_native_client_spawned` | asQ2Native | 1 | TS `nativeClientSpawned` (runtime.ts 3304) makes `strategy.asQ2Native()` |
| arsenal.c | `qa_app_shared_sim_native_weapon_step` | asQ2Native | 1 | TS `nativeWeaponStep` (runtime.ts 3329) makes `strategy.asQ2Native()` |
| arsenal.c | `qa_app_shared_sim_qvm_client_spawned` | asQ3Qvm | 1 | TS `qvmClientSpawned` (runtime.ts 3397) makes `strategy.asQ3Qvm()` |
| arsenal.c | `qa_app_shared_sim_qvm_weapon_step` | asQ3Qvm | 1 | TS `qvmWeaponStep` (runtime.ts 3412) makes `strategy.asQ3Qvm()` |
| arsenal.c | `qa_app_shared_sim_give_q2_start_items` | asQ2 | 1 | TS `giveQ2StartItems` (runtime.ts 7624) makes `strategy.asQ2()` |
| arsenal.c | `qa_app_shared_sim_selected_native_player` | asQ2Native | 1 | TS `selectedNativePlayer` (runtime.ts 3878) makes `strategy.asQ2Native()` |
| arsenal.c | `ss_drop_current` | asQ2Native | 1 | nativeDropProtection: `this.strategy.asQ2Native()` and `this.source !== source` (9476, 9483) |
| arsenal.c | `qa_app_shared_sim_native_drop_protection` | asQ2Native | 1 | TS `nativeDropProtection` (runtime.ts 9475) makes `strategy.asQ2Native()` |
| arsenal.c | `qa_app_shared_sim_native_inventory_rows` | asQ2Native | 1 | TS `nativeInventoryRows` (runtime.ts 9540) makes `strategy.asQ2Native()` |
| arsenal.c | `qa_app_shared_sim_create_hand_grenades` | asQ3Qvm | 1 | TS `createHandGrenades` (runtime.ts 5152) makes `strategy.asQ3Qvm()` |
| arsenal.c | `ss_threewave_input` | asQ1 | 1 | createGrapple input: `this.strategy.asQ1() !== null && ...` (4904) |
| arsenal.c | `ss_grapple_set_previous_velocity` | asQ2 | 1 | createGrapple setPreviousVelocity: `this.strategy.asQ2()` (4446) |
| arsenal.c | `ss_qvm_grapple_velocity` | asQ2Native, asQ3Qvm | 2 | initializeQvmGrapple: `asQ3Qvm()` / `asQ2Native()` (5059-5060) |
| arsenal.c | `ss_qvm_grapple_targets` | asQ2Native, asQ3Qvm | 3 | initializeQvmGrapple: `asQ2Native() !== null || primary !== null` (5090; primary = asQ3Qvm()?.game, 5009) |
| arsenal.c | `qa_app_shared_sim_initialize_qvm_grapple` | asQ3Qvm | 1 | TS `initializeQvmGrapple` (runtime.ts 5001) makes `strategy.asQ3Qvm()` |
| arsenal.c | `ss_original_rule` | asQ2Native | 1 | prepareSelectedOriginalPickups: `this.strategy.asQ2Native()` (3703) |
| arsenal.c | `qa_app_shared_sim_prepare_selected_original_pickups` | asQ2Native, asQ3Qvm, asQuakeC | 3 | TS `prepareSelectedOriginalPickups` (runtime.ts 3701) makes `strategy.asQ2Native()` |
| arsenal.c | `ss_q1_native_player` | asQ1, asQuakeC | 2 | createSelectedQ1Arsenal nativePlayer: `asQ1()` / `asQuakeC()` (4255-4266) |
| arsenal.c | `qa_app_shared_sim_create_selected_q1_arsenal` | asQ1, asQuakeC | 2 | TS `createSelectedQ1Arsenal` (runtime.ts 4159) makes `strategy.asQ1()` |
| arsenal.c | `ss_q3_qvm_loadout` | asQ3Qvm | 1 | createSelectedQ3Arsenal loadout: `const qvm = this.strategy.asQ3Qvm()` (3644) |
| arsenal.c | `ss_q2_mission_monster` | asQ2 | 1 | createSelectedQ2Arsenal monster: `this.strategy.asQ2()?.monsters.context` (4093) |
| arsenal.c | `ss_q2_selected_observe` | asQ2Native, asQ3Qvm | 2 | createSelectedQ2Arsenal: `asQ3Qvm() !== null ? ... : asQ2Native() !== null` (4131-4133) |
| checkpoint.c | `qa_app_shared_sim_restore` | asQ2Native, asQ3 | 3 | TS `restore` (runtime.ts 12013) makes `strategy.asQ2Native()` |
| core.c | `ss_nav_q1_game` | asQ1 | 1 | navigation host `q1Source()` (10012) |
| core.c | `ss_create` | asQ3Qvm | 1 | load (static): `const failedQvm = this.strategy.asQ3Qvm()` (1531) |
| core.c | `ss_construct_source` | asQ1, asQ2, asQ2Native, asQ3, asQ3Qvm, asQuakeC | 17 | constructor: primaryQ1/Native/Qvm/Qc/Q3/Q2 = this.strategy.asX() (1354-1392) |
| core.c | `ss_load_body` | asQ2Native | 1 | load: `const loadingNative = simulation.strategy.asQ2Native()` (1566) |
| core.c | `ss_close_one` | asQ2Native, asQ3 | 2 | close: `this.q2Native()?.close()`, `this.q3Source()?.close()` (12523, 12542) |
| core.c | `qa_app_shared_sim_close` | asQ3Qvm | 1 | close: `this.q3Guest()` = asQ3Qvm()?.game (12490) |
| players.c | `ss_quakec_kind` | asQuakeC | 1 | helper for `this.strategy.asQuakeC()?.game.kind` (queueQuakeWorldCommands, runQuakeWorldCommands 8344-8389) |
| players.c | `qa_app_shared_sim_restore_selected_travel` | asQ1 | 1 | TS `restoreSelectedTravel` (runtime.ts 7393) makes `strategy.asQ1()` |
| players.c | `qa_app_shared_sim_quake_c_client_spawned` | asQuakeC | 1 | TS `quakeCClientSpawned` (runtime.ts 7576) makes `strategy.asQuakeC()` |
| players.c | `qa_app_shared_sim_sync_quake_c_client_view` | asQuakeC | 1 | TS `syncQuakeCClientView` (runtime.ts 7597) makes `strategy.asQuakeC()` |
| players.c | `qa_app_shared_sim_resume_q2_presentation` | asQ2 | 1 | TS `resumeQ2Presentation` (runtime.ts 7608) makes `strategy.asQ2()` |
| players.c | `qa_app_shared_sim_q1_character_source` | asQ1 | 1 | TS `q1CharacterSource` (runtime.ts 7640) makes `strategy.asQ1()` |
| players.c | `ss_q1_fall_damage_allowed` | asQ1 | 1 | attachQ1Character fallDamageAllowed: `this.strategy.asQ1()?.composition` (7691) |
| players.c | `ss_q2_noise` | asQ2 | 1 | attachQ2Character noise: `this.strategy.asQ2()?.monsters.reportNoise` (7758) |
| players.c | `qa_app_shared_sim_place_q1_player` | asQ1 | 1 | TS `placeQ1Player` (runtime.ts 7853) makes `strategy.asQ1()` |
| players.c | `qa_app_shared_sim_disconnect_player` | asQ2Native | 1 | TS `disconnectPlayer` (runtime.ts 10958) makes `strategy.asQ2Native()` |
| players.c | `ss_q3_weapon` | asQ3 | 1 | spawnQ3Player/syncQ3Player: `this.strategy.asQ3()` (7104-7334) |
| players.c | `qa_app_shared_sim_restore_campaign_travel` | asQ2 | 1 | TS `restoreCampaignTravel` (runtime.ts 11618) makes `strategy.asQ2()` |
| players.c | `qa_app_shared_sim_capture_original_save` | asQuakeC | 1 | TS `captureOriginalSave` (runtime.ts 11659) makes `strategy.asQuakeC()` |
| players.c | `qa_app_shared_sim_restore_original_save` | asQuakeC | 1 | TS `restoreOriginalSave` (runtime.ts 11696) makes `strategy.asQuakeC()` |
| players.c | `qa_app_shared_sim_sync_q3_player` | asQ3 | 1 | TS `syncQ3Player` (runtime.ts 7333) makes `strategy.asQ3()` |
| players.c | `qa_app_shared_sim_spawn_q3_player` | asQ3 | 1 | TS `spawnQ3Player` (runtime.ts 7103) makes `strategy.asQ3()` |
| players.c | `qa_app_shared_sim_select_q3_spawn_weapon` | asQ3 | 1 | TS `selectQ3SpawnWeapon` (runtime.ts 7188) makes `strategy.asQ3()` |
| players.c | `qa_app_shared_sim_move_q3_client` | asQ3 | 1 | TS `moveQ3Client` (runtime.ts 7211) makes `strategy.asQ3()` |
| source.c | `ss_q2_player_view_state` | asQ2 | 1 | q2ActorHost playerViewState: `this.strategy.asQ2()` (4446) |
| source.c | `ss_q2_key_consumed` | asQ2 | 1 | q2ActorHost keyConsumed: `const q2 = this.strategy.asQ2()` (4466) |
| source.c | `ss_q2_emit` | asQ2 | 1 | q2ActorHost: `this.strategy.asQ2() !== null && ...` (4494) |
| source.c | `ss_q2_transition` | asQ2 | 1 | q2ActorHost transition: `const q2 = this.strategy.asQ2()` (4500) |
| source.c | `ss_q3_speed_multiplier` | asQ3 | 1 | syncQ3Player: `this.strategy.asQ3()` (7215) |
| source.c | `ss_q3_source_command` | asQ3 | 1 | moveQ3Client: `const src = this.strategy.asQ3()` (7190) |
| source.c | `ss_input_movement` | asQ3Qvm | 1 | constructor movement: `this.strategy.asQ3Qvm()?.weapons` (1392) |
| source.c | `qa_app_shared_sim_bind_qvm_input` | asQ3Qvm | 1 | constructor: binds input inside the `primaryQvm` (= asQ3Qvm()) arm (1356, 1378) |
| step.c | `qa_app_shared_sim_step_async` | asQ3Qvm | 1 | TS `stepAsync` (runtime.ts 8538) makes `strategy.asQ3Qvm()` |
| step.c | `qa_app_shared_sim_begin_native_q2_step` | asQ2Native | 1 | TS `beginNativeQ2Step` (runtime.ts 10573) makes `strategy.asQ2Native()` |
| step.c | `qa_app_shared_sim_native_q2_frames` | asQ2Native | 1 | stepNativeQ2Async: `const source = this.strategy.asQ2Native()` (10616) |
| worlds.c | `qa_app_shared_sim_tick_horde_wave` | asQ3 | 1 | foreign-horde-rules.ts 32 `host.sourceKind === "q3"` (host.sourceKind = strategy.kind) |
| worlds.c | `qa_app_shared_sim_foreign_ctf` | asQ1 | 1 | foreign-lmctf-rules.ts 65 `host.sourceKind !== "q1"` (host.sourceKind = strategy.kind) |
| worlds.c | `qa_app_shared_sim_q1_weapon_source` | asQ1 | 1 | TS `q1WeaponSource` (runtime.ts 9949) makes `strategy.asQ1()` |
| worlds.c | `qa_app_shared_sim_q2_weapon_source` | asQ2 | 1 | TS `q2WeaponSource` (runtime.ts 9957) makes `strategy.asQ2()` |
| worlds.c | `qa_app_shared_sim_q2_item_weapon_source` | asQ2 | 1 | TS `q2ItemWeaponSource` (runtime.ts 9954) makes `strategy.asQ2()` |
| worlds.c | `qa_app_shared_sim_q2_movement_config` | asQ2 | 1 | TS `q2MovementConfig` (runtime.ts 9982) makes `strategy.asQ2()` |
| worlds.c | `qa_app_shared_sim_q2_source` | asQ2 | 1 | TS `q2Source` (runtime.ts 10005) makes `strategy.asQ2()` |
| worlds.c | `qa_app_shared_sim_quakec_source` | asQuakeC | 1 | TS `quakecSource` (runtime.ts 10008) makes `strategy.asQuakeC()` |
| worlds.c | `qa_app_shared_sim_q1_source` | asQ1 | 1 | TS `q1Source` (runtime.ts 10011) makes `strategy.asQ1()` |
| worlds.c | `qa_app_shared_sim_q2_native` | asQ2Native | 1 | TS `q2Native` (runtime.ts 10018) makes `strategy.asQ2Native()` |
| worlds.c | `qa_app_shared_sim_q2_native_players` | asQ2Native | 1 | TS `q2NativePlayers` (runtime.ts 10021) makes `strategy.asQ2Native()` |
| worlds.c | `qa_app_shared_sim_q3_guest` | asQ3Qvm | 1 | TS `q3Guest` (runtime.ts 10684) makes `strategy.asQ3Qvm()` |
| worlds.c | `qa_app_shared_sim_q3_source` | asQ3 | 1 | TS `q3Source` (runtime.ts 10894) makes `strategy.asQ3()` |
| worlds.c | `qa_app_shared_sim_pending_match_map` | asQ2 | 1 | TS `pendingMatchMap` (runtime.ts 11710) makes `strategy.asQ2()` |
| worlds.c | `qa_app_shared_sim_source_restart_plan` | asQ3 | 1 | TS `sourceRestartPlan` (runtime.ts 10690) makes `strategy.asQ3()` |
| worlds.c | `qa_app_shared_sim_restart_source_round` | asQ3 | 1 | restartSourceRound: `previous.kind !== "q3"` (10714; the source record kind, same test as asQ3()) |
| worlds.c | `qa_app_shared_sim_q1_punch_owner` | asQ1, asQuakeC | 2 | TS `q1PunchOwner` (runtime.ts 1720) makes `strategy.asQuakeC()` |
| worlds.c | `qa_app_shared_sim_set_source_view_roll` | asQ1, asQ2Native, asQ3, asQ3Qvm, asQuakeC | 5 | TS `setSourceViewRoll` (runtime.ts 1748) makes `strategy.asQuakeC()` |
| worlds.c | `qa_app_shared_sim_advance_q1_punch` | asQuakeC | 1 | TS `advanceQ1Punch` (runtime.ts 1780) makes `strategy.asQuakeC()` |
| worlds.c | `wl_keep_ordinals` | asQ3 | 1 | spawnForeignWorld: `const guestLane = this.strategy.asQ3() !== null` (2242) |
| worlds.c | `qa_app_shared_sim_foreign_native_movement` | asQ2Native | 1 | TS `foreignNativeMovement` (runtime.ts 10172) makes `strategy.asQ2Native()` |
| worlds.c | `qa_app_shared_sim_foreign_native_character` | asQ2Native | 1 | TS `foreignNativeCharacter` (runtime.ts 10181) makes `strategy.asQ2Native()` |
| worlds.c | `qa_app_shared_sim_selection_consumption` | asQ2Native | 1 | TS `selectionConsumption` (runtime.ts 2490) makes `strategy.asQ2Native()` |
| worlds.c | `qa_app_shared_sim_q2_monster_dodge` | asQ2 | 1 | TS `q2MonsterDodge` (runtime.ts 6775) makes `strategy.asQ2()` |
| worlds.c | `qa_app_shared_sim_native_q2_client` | asQ2Native | 1 | TS `nativeQ2Client` (runtime.ts 10138) makes `strategy.asQ2Native()` |
| worlds.c | `qa_app_shared_sim_register_q2_native_client_mode` | asQ2Native | 1 | registerQ2NativeClient: `const src = this.strategy.asQ2Native()` (10090) |
| worlds.c | `qa_app_shared_sim_capture_native_q2_travel` | asQ2Native | 1 | TS `captureNativeQ2Travel` (runtime.ts 10024) makes `strategy.asQ2Native()` |
| worlds.c | `qa_app_shared_sim_admit_q2_native_player` | asQ2Native | 1 | TS `admitQ2NativePlayer` (runtime.ts 10088) makes `strategy.asQ2Native()` |
| worlds.c | `qa_app_shared_sim_presentation_players` | asQ2Native | 1 | TS `presentationPlayers` (runtime.ts 10207) makes `strategy.asQ2Native()` |
| worlds.c | `qa_app_shared_sim_bind_native_input` | asQ2Native | 1 | TS `bindNativeInput` (runtime.ts 10328) makes `strategy.asQ2Native()` |
| worlds.c | `qa_app_shared_sim_tick_native_characters` | asQ2Native | 1 | TS `tickNativeCharacters` (runtime.ts 10212) makes `strategy.asQ2Native()` |
| worlds.c | `wl_creature_ammo_granted` | asQ1 | 1 | selectedCreaturePickups ammoGranted: `this.strategy.asQ1()` (3086) |
| worlds.c | `wl_creature_weapon_granted` | asQ1, asQ2 | 2 | selectedCreaturePickups weaponGranted: `asQ1()` / `asQ2()` (3100-3101) |
| worlds.c | `wl_selected_creature_pickups` | asQ1 | 1 | TS `selectedCreaturePickups` (runtime.ts 3064) makes `strategy.asQ1()` |
| worlds.c | `wl_ms_cvar` | asQ1 | 1 | monsterSourceFor cvar: `this.strategy.asQ1()?.cvars ?? this.q2ServerRegistry` (1846) |
| worlds.c | `wl_ms_set_cvar` | asQ1 | 1 | monsterSourceFor setCvar: `this.strategy.asQ1()?.cvars ?? this.q2ServerRegistry` (1848) |
| worlds.c | `wl_ms_powerups` | asQ2 | 1 | monsterSourceFor powerups: `this.strategy.asQ2()?.product.powerups(actor)` (1895) |
| worlds.c | `qa_app_shared_sim_prepare_selected_monsters` | asQ1, asQ2 | 5 | TS `prepareSelectedMonsters` (runtime.ts 2300) makes `strategy.asQ1()` |
| worlds.c | `qa_app_shared_sim_sync_guest_monster_proxies` | asQ3Qvm | 1 | TS `syncGuestMonsterProxies` (runtime.ts 2818) makes `strategy.asQ3Qvm()` |
| worlds.c | `qa_app_shared_sim_run_guest_monsters` | asQ2Native | 1 | TS `runGuestMonsters` (runtime.ts 2764) makes `strategy.asQ2Native()` |

## Phase B assignments (balanced by TS lines)

- **B1 worlds and rules**: 2104 TS lines, file `shared_simulation_worlds.c`.
- **B2 arsenal, equipment and presentation**: 1880 TS lines, file `shared_simulation_arsenal.c`.
- **B3 players, admission, travel, checkpoint and restore**: 1970 TS lines, file `shared_simulation_players.c`.
- Phase A total: 1940 TS lines.

## Methods

| TS lines | TS member | C function | C file | Group | Status |
|---|---|---|---|---|---|
| 271-271 | `add` | `qa_ss_add` | `shared_simulation_support.c` | A | done |
| 272-272 | `subtract` | `qa_ss_subtract` | `shared_simulation_support.c` | A | done |
| 273-273 | `seconds` | `qa_ss_seconds` | `shared_simulation_support.c` | A | done |
| 274-277 | `replacedSupplyItems` | `qa_ss_replaced_supply_items` | `shared_simulation_arsenal.c` | B2 | done |
| 279-279 | `isQ1Barrel` | `qa_ss_is_q1_barrel` | `shared_simulation_support.c` | A | done |
| 280-280 | `sourceModel` | `qa_ss_source_model` | `shared_simulation_support.c` | A | done |
| 478-478 | `modClientPresentationSources` | `qa_app_shared_sim_mod_client_presentation_sources` | `shared_simulation_players.c` | B3 | done |
| 479-479 | `modPresentationSources` | `qa_app_shared_sim_mod_presentation_sources` | `shared_simulation_players.c` | B3 | done |
| 502-544 | `createModClients` | `qa_app_shared_sim_create_mod_clients` | `shared_simulation_core.c` | A | done |
| 545-558 | `sourceClientView` | `qa_app_shared_sim_source_client_view` | `shared_simulation_core.c` | A | done |
| 559-576 | `activeClientOutputs` | `qa_app_shared_sim_active_client_outputs` | `shared_simulation_core.c` | A | done |
| 590-965 | `constructor` | `qa_app_shared_simulation_create` | `shared_simulation_core.c` | A | done |
| 967-1046 | `load` (static) | `qa_app_shared_simulation_load` | `shared_simulation_core.c` | A | done |
| 1048-1048 | `bodies` (get) | `qa_app_shared_sim_bodies` | `shared_simulation_core.c` | A | done |
| 1049-1049 | `timeSeconds` (get) | `qa_app_shared_sim_time_seconds` | `shared_simulation_core.c` | A | done |
| 1050-1063 | `q1PunchOwner` | `qa_app_shared_sim_q1_punch_owner` | `shared_simulation_worlds.c` | B1 | done |
| 1064-1068 | `emitQ1Addon` | `qa_app_shared_sim_emit_q1_addon` | `shared_simulation_worlds.c` | B1 | done |
| 1069-1087 | `setSourceViewRoll` | `qa_app_shared_sim_set_source_view_roll` | `shared_simulation_worlds.c` | B1 | done |
| 1088-1099 | `advanceQ1Punch` | `qa_app_shared_sim_advance_q1_punch` | `shared_simulation_worlds.c` | B1 | done |
| 1100-1100 | `q1PhysicsEdition` (get) | `qa_app_shared_sim_q1_physics_edition` | `shared_simulation_core.c` | A | done |
| 1102-1179 | `monsterSourceFor` | `qa_app_shared_sim_monster_source_for` | `shared_simulation_worlds.c` | B1 | done |
| 1183-1185 | `tsPrimary` (get) | `qa_app_shared_sim_ts_primary` | `shared_simulation_worlds.c` | B1 | done |
| 1189-1196 | `foreignQ2Identity` | `qa_app_shared_sim_foreign_q2_identity` | `shared_simulation_worlds.c` | B1 | done |
| 1199-1207 | `foreignQ1Identity` | `qa_app_shared_sim_foreign_q1_identity` | `shared_simulation_worlds.c` | B1 | done |
| 1210-1215 | `foreignAdjunctIdentity` | `qa_app_shared_sim_foreign_adjunct_identity` | `shared_simulation_worlds.c` | B1 | done |
| 1220-1267 | `createForeignQ2World` | `qa_app_shared_sim_create_foreign_q2_world` | `shared_simulation_worlds.c` | B1 | done |
| 1272-1325 | `createForeignQ1World` | `qa_app_shared_sim_create_foreign_q1_world` | `shared_simulation_worlds.c` | B1 | done |
| 1328-1331 | `createForeignWorld` | `qa_app_shared_sim_create_foreign_world` | `shared_simulation_worlds.c` | B1 | done |
| 1338-1360 | `spawnForeignWorld` | `qa_app_shared_sim_spawn_foreign_world` | `shared_simulation_worlds.c` | B1 | done |
| 1362-1375 | `attachMonster` | `qa_app_shared_sim_attach_monster` | `shared_simulation_worlds.c` | B1 | done |
| 1377-1518 | `prepareSelectedMonsters` | `qa_app_shared_sim_prepare_selected_monsters` | `shared_simulation_worlds.c` | B1 | done |
| 1532-1553 | `selectionConsumption` | `qa_app_shared_sim_selection_consumption` | `shared_simulation_worlds.c` | B1 | done |
| 1555-1567 | `guestMonsterPlan` | `qa_app_shared_sim_guest_monster_plan` | `shared_simulation_worlds.c` | B1 | done |
| 1569-1571 | `guestMonsterEntities` | `qa_app_shared_sim_guest_monster_entities` | `shared_simulation_worlds.c` | B1 | done |
| 1577-1681 | `prepareGuestMonsters` | `qa_app_shared_sim_prepare_guest_monsters` | `shared_simulation_worlds.c` | B1 | done (Q2 item hooks foreignItems: no C member) |
| 1686-1723 | `admitGuestMonsters` | `qa_app_shared_sim_admit_guest_monsters` | `shared_simulation_worlds.c` | B1 | done |
| 1727-1731 | `flushGuestMonsterReport` | `qa_app_shared_sim_flush_guest_monster_report` | `shared_simulation_worlds.c` | B1 | done |
| 1733-1766 | `runGuestMonsters` | `qa_app_shared_sim_run_guest_monsters` | `shared_simulation_worlds.c` | B1 | done |
| 1770-1777 | `syncGuestMonsterProxies` | `qa_app_shared_sim_sync_guest_monster_proxies` | `shared_simulation_worlds.c` | B1 | done |
| 1779-1786 | `captureSelectedMonsters` | `qa_app_shared_sim_capture_selected_monsters` | `shared_simulation_checkpoint.c` | B1 | done (returns the record as a checkpoint-value tree; Q1 sources through qa_persist_q1_foundation_from_content) |
| 1788-1833 | `restoreSelectedMonsters` | `qa_app_shared_sim_restore_selected_monsters` | `shared_simulation_checkpoint.c` | B1 | done (Q1 sources through qa_persist_q1_foundation_to_content) |
| 1835-1845 | `captureForeignWorld` | `qa_app_shared_sim_capture_foreign_world` | `shared_simulation_worlds.c` | B1 | done |
| 1847-1867 | `restoreForeignWorld` | `qa_app_shared_sim_restore_foreign_world` | `shared_simulation_worlds.c` | B1 | done |
| 1869-1875 | `captureForeignQ1World` | `qa_app_shared_sim_capture_foreign_q1_world` | `shared_simulation_checkpoint.c` | B1 | done (checkpoint-value tree) |
| 1877-1889 | `restoreForeignQ1World` | `qa_app_shared_sim_restore_foreign_q1_world` | `shared_simulation_checkpoint.c` | B1 | done |
| 1893-1905 | `beginForeignQ1WorldFrame` | `qa_app_shared_sim_begin_foreign_q1_world_frame` | `shared_simulation_worlds.c` | B1 | done |
| 1908-1923 | `beginForeignWorldFrame` | `qa_app_shared_sim_begin_foreign_world_frame` | `shared_simulation_worlds.c` | B1 | done |
| 1925-1948 | `beginMonsterFrames` | `qa_app_shared_sim_begin_monster_frames` | `shared_simulation_worlds.c` | B1 | done |
| 1950-1991 | `selectedCreaturePickups` | `qa_app_shared_sim_selected_creature_pickups` | `shared_simulation_worlds.c` | B1 | done |
| 1993-2015 | `createMonsterMovement` | `qa_app_shared_sim_create_monster_movement` | `shared_simulation_worlds.c` | B1 | done |
| 2017-2018 | `selectedQ3Supply` | `qa_app_shared_sim_selected_q3_supply` | `shared_simulation_arsenal.c` | B2 | done |
| 2039-2045 | `previewSelectedQ3Pickup` | `qa_app_shared_sim_preview_selected_q3_pickup` | `shared_simulation_arsenal.c` | B2 | done |
| 2047-2058 | `admitSelectedQ3Pickup` | `qa_app_shared_sim_admit_selected_q3_pickup` | `shared_simulation_arsenal.c` | B2 | done |
| 2060-2066 | `q1CombatSource` | `qa_app_shared_sim_q1_combat_source` | `shared_simulation_arsenal.c` | B2 | done |
| 2068-2070 | `selectedQ1Frame` | `qa_app_shared_sim_selected_q1_frame` | `shared_simulation_arsenal.c` | B2 | done |
| 2072-2075 | `requireQvmWeapons` | `qa_app_shared_sim_require_qvm_weapons` | `shared_simulation_arsenal.c` | B2 | done |
| 2076-2079 | `refreshArsenalCache` | `qa_app_shared_sim_refresh_arsenal_cache` | `shared_simulation_arsenal.c` | B2 | done |
| 2080-2093 | `arsenalObservation` | `qa_app_shared_sim_arsenal_observation` | `shared_simulation_arsenal.c` | B2 | done |
| 2094-2097 | `requireNativeWeapons` | `qa_app_shared_sim_require_native_weapons` | `shared_simulation_arsenal.c` | B2 | done |
| 2098-2103 | `nativeQ2WeaponInput` | `qa_app_shared_sim_native_q2_weapon_input` | `shared_simulation_arsenal.c` | B2 | done |
| 2104-2122 | `nativeClientSpawned` | `qa_app_shared_sim_native_client_spawned` | `shared_simulation_arsenal.c` | B2 | done |
| 2123-2147 | `nativeWeaponStep` | `qa_app_shared_sim_native_weapon_step` | `shared_simulation_arsenal.c` | B2 | done |
| 2148-2154 | `qvmQ2WeaponInput` | `qa_app_shared_sim_qvm_q2_weapon_input` | `shared_simulation_arsenal.c` | B2 | done |
| 2155-2164 | `qvmClientSpawned` | `qa_app_shared_sim_qvm_client_spawned` | `shared_simulation_arsenal.c` | B2 | done |
| 2165-2187 | `qvmWeaponStep` | `qa_app_shared_sim_qvm_weapon_step` | `shared_simulation_arsenal.c` | B2 | done |
| 2189-2193 | `qcDamageModifier` | `qa_app_shared_sim_qc_damage_modifier` | `shared_simulation_arsenal.c` | B2 | done |
| 2195-2329 | `createSelectedQ3Arsenal` | `qa_app_shared_sim_create_selected_q3_arsenal` | `shared_simulation_arsenal.c` | B2 | done (runtime-rest; Q3 map branches read the new q3/runtime.h accessors intermission_queued/team/world) |
| 2331-2426 | `prepareSelectedOriginalPickups` | `qa_app_shared_sim_prepare_selected_original_pickups` | `shared_simulation_arsenal.c` | B2 | done |
| 2428-2432 | `bindSelectedOriginalPickups` | `qa_app_shared_sim_bind_selected_original_pickups` | `shared_simulation_arsenal.c` | B2 | done |
| 2434-2434 | `nativePrimary` | `qa_app_shared_sim_native_primary` | `shared_simulation_arsenal.c` | B2 | done |
| 2439-2449 | `guestMonsterProjection` | `qa_app_shared_sim_guest_monster_projection` | `shared_simulation_worlds.c` | B1 | done |
| 2453-2455 | `guestMapEdition` | `qa_app_shared_sim_guest_map_edition` | `shared_simulation_worlds.c` | B1 | done |
| 2457-2462 | `selectedNativePlayer` | `qa_app_shared_sim_selected_native_player` | `shared_simulation_arsenal.c` | B2 | done |
| 2464-2476 | `selectedQ3Spawn` | `qa_app_shared_sim_selected_q3_spawn` | `shared_simulation_arsenal.c` | B2 | done |
| 2478-2492 | `dropSelectedQ3Objectives` | `qa_app_shared_sim_drop_selected_q3_objectives` | `shared_simulation_arsenal.c` | B2 | done |
| 2494-2535 | `selectedQ3ClientChanged` | `qa_app_shared_sim_selected_q3_client_changed` | `shared_simulation_arsenal.c` | B2 | done |
| 2537-2542 | `bindSelectedAmmo` | `qa_app_shared_sim_bind_selected_ammo` | `shared_simulation_arsenal.c` | B2 | done |
| 2544-2549 | `selectedAmmo` | `qa_app_shared_sim_selected_ammo` | `shared_simulation_arsenal.c` | B2 | done |
| 2551-2553 | `selectedAmmoActors` | `qa_app_shared_sim_selected_ammo_actors` | `shared_simulation_arsenal.c` | B2 | done |
| 2555-2568 | `selectedWeaponDelay` | `qa_app_shared_sim_selected_weapon_delay` | `shared_simulation_arsenal.c` | B2 | done |
| 2570-2581 | `q2SourceDamageHooks` | `qa_app_shared_sim_q2_source_damage_hooks` | `shared_simulation_arsenal.c` | B2 | done |
| 2583-2645 | `createSelectedQ2Arsenal` | `qa_app_shared_sim_create_selected_q2_arsenal` | `shared_simulation_arsenal.c` | B2 | done (runtime-rest; q2_expansion_arsenal now on the real Q2GameServices/Q2Weapons/mission-pack types) |
| 2647-2739 | `createSelectedQ1Arsenal` | `qa_app_shared_sim_create_selected_q1_arsenal` | `shared_simulation_arsenal.c` | B2 | done |
| 2741-2783 | `q1ActorHost` | `qa_app_shared_sim_q1_actor_host` | `shared_simulation_source.c` | A | done |
| 2785-2819 | `q2ActorHost` | `qa_app_shared_sim_q2_actor_host` | `shared_simulation_source.c` | A | done |
| 2821-2825 | `setHandGrenadeInput` | `qa_app_shared_sim_set_hand_grenade_input` | `shared_simulation_arsenal.c` | B2 | done |
| 2827-2827 | `handGrenadeState` | `qa_app_shared_sim_hand_grenade_state` | `shared_simulation_arsenal.c` | B2 | done |
| 2829-2843 | `requireEquipmentPlayer` | `qa_app_shared_sim_require_equipment_player` | `shared_simulation_arsenal.c` | B2 | done |
| 2845-2849 | `bindEquipmentInventory` | `qa_app_shared_sim_bind_equipment_inventory` | `shared_simulation_arsenal.c` | B2 | done |
| 2851-2869 | `bindSourceInventory` | `qa_app_shared_sim_bind_source_inventory` | `shared_simulation_arsenal.c` | B2 | done |
| 2871-2879 | `equipmentPlayerAvailable` | `qa_app_shared_sim_equipment_player_available` | `shared_simulation_arsenal.c` | B2 | done |
| 2881-2913 | `beginNativeEquipmentFrame` | `qa_app_shared_sim_begin_native_equipment_frame` | `shared_simulation_arsenal.c` | B2 | done |
| 2915-2919 | `setGrappleInput` | `qa_app_shared_sim_set_grapple_input` | `shared_simulation_arsenal.c` | B2 | done |
| 2921-2924 | `grappleState` | `qa_app_shared_sim_grapple_state` | `shared_simulation_arsenal.c` | B2 | done |
| 2926-2931 | `sharedGrapple` | `qa_app_shared_sim_shared_grapple` | `shared_simulation_arsenal.c` | B2 | done |
| 2933-2978 | `primaryHandoff` | `qa_app_shared_sim_primary_handoff` | `shared_simulation_arsenal.c` | B2 | done |
| 2980-2989 | `admitGrapple` | `qa_app_shared_sim_admit_grapple` | `shared_simulation_arsenal.c` | B2 | done |
| 2990-2995 | `bindWeaponSlot` | `qa_app_shared_sim_bind_weapon_slot` | `shared_simulation_arsenal.c` | B2 | done |
| 2996-2996 | `hasWeaponSlot` | `qa_app_shared_sim_has_weapon_slot` | `shared_simulation_arsenal.c` | B2 | done |
| 2997-3003 | `requestWeapon` | `qa_app_shared_sim_request_weapon` | `shared_simulation_arsenal.c` | B2 | done |
| 3005-3014 | `grappleAnchor` | `qa_app_shared_sim_grapple_anchor` | `shared_simulation_arsenal.c` | B2 | done |
| 3016-3020 | `q1CharacterPose` | `qa_app_shared_sim_q1_character_pose` | `shared_simulation_arsenal.c` | B2 | done |
| 3022-3036 | `equipmentWeaponInput` | `qa_app_shared_sim_equipment_weapon_input` | `shared_simulation_arsenal.c` | B2 | done |
| 3037-3041 | `grappleCharacterAnimation` | `qa_app_shared_sim_grapple_character_animation` | `shared_simulation_arsenal.c` | B2 | done |
| 3043-3062 | `weaponCharacterAnimation` | `qa_app_shared_sim_weapon_character_animation` | `shared_simulation_arsenal.c` | B2 | done |
| 3064-3075 | `grappleSlotHost` | `qa_app_shared_sim_grapple_slot_host` | `shared_simulation_arsenal.c` | B2 | done |
| 3077-3122 | `createGrapple` | `qa_app_shared_sim_create_grapple` | `shared_simulation_arsenal.c` | B2 | done |
| 3124-3184 | `initializeQvmGrapple` | `qa_app_shared_sim_initialize_qvm_grapple` | `shared_simulation_arsenal.c` | B2 | done |
| 3186-3194 | `handGrenadeInterval` | `qa_app_shared_sim_hand_grenade_interval` | `shared_simulation_arsenal.c` | B2 | done |
| 3196-3219 | `createHandGrenades` | `qa_app_shared_sim_create_hand_grenades` | `shared_simulation_arsenal.c` | B2 | done |
| 3221-3232 | `stepHandGrenade` | `qa_app_shared_sim_step_hand_grenade` | `shared_simulation_arsenal.c` | B2 | done |
| 3234-3238 | `installRereleaseNavigation` | `qa_app_shared_sim_install_rerelease_navigation` | `shared_simulation_worlds.c` | B1 | done |
| 3239-3239 | `rereleaseNavigation` | `qa_app_shared_sim_rerelease_navigation` | `shared_simulation_worlds.c` | B1 | done |
| 3240-3248 | `nativeModContext` | `qa_app_shared_sim_native_mod_context` | `shared_simulation_worlds.c` | B1 | done |
| 3249-3253 | `rereleaseNavigationServices` | `qa_app_shared_sim_rerelease_navigation_services` | `shared_simulation_worlds.c` | B1 | done |
| 3254-3274 | `createQvmWeaponBehavior` | `qa_app_shared_sim_create_qvm_weapon_behavior` | `shared_simulation_arsenal.c` | B2 | done |
| 3276-3304 | `createNativeWeaponBehavior` | `qa_app_shared_sim_create_native_weapon_behavior` | `shared_simulation_arsenal.c` | B2 | done (ownership member added to the C options, runtime-rest) |
| 3306-3883 | `createSource` | `qa_app_shared_sim_create_source` | `shared_simulation_source.c` | A | done (q3: qa_app_shared_sim_create_q3_source, q3-qvm: qa_app_shared_sim_create_q3_qvm_source; tests/app/test_q3_source.c) |
| 3885-3894 | `victimArmorContext` | `qa_app_shared_sim_victim_armor_context` | `shared_simulation_worlds.c` | B1 | done |
| 3896-3957 | `registerCombat` | `qa_app_shared_sim_register_combat` | `shared_simulation_worlds.c` | B1 | done |
| 3959-3962 | `q2MatchDamageEffects` | `qa_app_shared_sim_q2_match_damage_effects` | `shared_simulation_worlds.c` | B1 | done |
| 3964-3976 | `schedule` | `qa_app_shared_sim_schedule` | `shared_simulation_core.c` | A | done |
| 3978-3981 | `executionProvider` | `qa_app_shared_sim_execution_provider` | `shared_simulation_core.c` | A | done |
| 3983-3993 | `q2WeaponTarget` | `qa_app_shared_sim_q2_weapon_target` | `shared_simulation_worlds.c` | B1 | done |
| 3995-4005 | `q2MonsterDodge` | `qa_app_shared_sim_q2_monster_dodge` | `shared_simulation_worlds.c` | B1 | done |
| 4007-4022 | `monsterTarget` | `qa_app_shared_sim_monster_target` | `shared_simulation_worlds.c` | B1 | done (cloakAlpha: qa_monster_target_observation has no member) |
| 4024-4027 | `sourceOrder` | `qa_app_shared_sim_source_order` | `shared_simulation_core.c` | A | done |
| 4028-4033 | `sourcePosition` | `qa_app_shared_sim_source_position` | `shared_simulation_core.c` | A | done |
| 4035-4045 | `collision` | `qa_app_shared_sim_collision` | `shared_simulation_core.c` | A | done |
| 4047-4053 | `registerActorExecution` | `qa_app_shared_sim_register_actor_execution` | `shared_simulation_core.c` | A | done |
| 4055-4060 | `contents` | `qa_app_shared_sim_contents` | `shared_simulation_core.c` | A | done |
| 4062-4075 | `q1VisibilityEye` | `qa_app_shared_sim_q1_visibility_eye` | `shared_simulation_worlds.c` | B1 | done |
| 4077-4077 | `worldActor` | `qa_app_shared_sim_world_actor` | `shared_simulation_core.c` | A | done |
| 4078-4078 | `player` | `qa_app_shared_sim_player` | `shared_simulation_core.c` | A | done |
| 4079-4079 | `movementPlayerOf` | `qa_app_shared_sim_movement_player_of` | `shared_simulation_core.c` | A | done |
| 4080-4084 | `clientIdentities` | `qa_app_shared_sim_client_identities` | `shared_simulation_core.c` | A | done |
| 4085-4087 | `players` | `qa_app_shared_sim_players` | `shared_simulation_core.c` | A | done |
| 4088-4094 | `botEntity` | `qa_app_shared_sim_bot_entity` | `shared_simulation_players.c` | B3 | done |
| 4095-4095 | `classname` | `qa_app_shared_sim_classname` | `shared_simulation_core.c` | A | done |
| 4097-4101 | `powerup` | `qa_app_shared_sim_powerup` | `shared_simulation_arsenal.c` | B2 | done |
| 4103-4113 | `weaponEvent` | `qa_app_shared_sim_weapon_event` | `shared_simulation_arsenal.c` | B2 | done |
| 4115-4202 | `createPlayer` | `qa_app_shared_sim_create_player` | `shared_simulation_players.c` | B3 | done |
| 4204-4215 | `prepareBotClient` | `qa_app_shared_sim_prepare_bot_client` | `shared_simulation_players.c` | B3 | done |
| 4217-4235 | `prepareQ3Client` | `qa_app_shared_sim_prepare_q3_client` | `shared_simulation_players.c` | B3 | done |
| 4237-4247 | `admitQ3Player` | `qa_app_shared_sim_admit_q3_player` | `shared_simulation_players.c` | B3 | done |
| 4249-4294 | `spawnQ3Player` | `qa_app_shared_sim_spawn_q3_player` | `shared_simulation_players.c` | B3 | done |
| 4297-4311 | `selectQ3SpawnWeapon` | `qa_app_shared_sim_select_q3_spawn_weapon` | `shared_simulation_players.c` | B3 | done (Q3SelectedArsenal.setSpawnWeapon ported in arsenal/q3.c) |
| 4313-4379 | `moveQ3Client` | `qa_app_shared_sim_move_q3_client` | `shared_simulation_players.c` | B3 | done |
| 4381-4398 | `syncQ3Player` | `qa_app_shared_sim_sync_q3_player` | `shared_simulation_players.c` | B3 | done |
| 4400-4418 | `restoreSelectedArsenalTravel` | `qa_app_shared_sim_restore_selected_arsenal_travel` | `shared_simulation_arsenal.c` | B2 | done |
| 4420-4425 | `captureSelectedArsenalTravel` | `qa_app_shared_sim_capture_selected_arsenal_travel` | `shared_simulation_arsenal.c` | B2 | done |
| 4427-4438 | `restoreSelectedTravel` | `qa_app_shared_sim_restore_selected_travel` | `shared_simulation_players.c` | B3 | done |
| 4440-4445 | `syncSelectedQ1HealthLimit` | `qa_app_shared_sim_sync_selected_q1_health_limit` | `shared_simulation_arsenal.c` | B2 | done |
| 4447-4456 | `reserveNetQuakeClient` | `qa_app_shared_sim_reserve_net_quake_client` | `shared_simulation_players.c` | B3 | done |
| 4458-4551 | `admitPlayer` | `qa_app_shared_sim_admit_player` | `shared_simulation_players.c` | B3 | done |
| 4553-4567 | `quakeCClientSpawned` | `qa_app_shared_sim_quake_c_client_spawned` | `shared_simulation_players.c` | B3 | done |
| 4569-4577 | `syncQuakeCClientView` | `qa_app_shared_sim_sync_quake_c_client_view` | `shared_simulation_players.c` | B3 | done |
| 4579-4590 | `resumeQ2Presentation` | `qa_app_shared_sim_resume_q2_presentation` | `shared_simulation_players.c` | B3 | done |
| 4592-4602 | `giveQ2StartItems` | `qa_app_shared_sim_give_q2_start_items` | `shared_simulation_arsenal.c` | B2 | done |
| 4604-4625 | `q1CharacterSource` | `qa_app_shared_sim_q1_character_source` | `shared_simulation_players.c` | B3 | done |
| 4627-4638 | `attachQ1Character` | `qa_app_shared_sim_attach_q1_character` | `shared_simulation_players.c` | B3 | done |
| 4640-4670 | `attachQ2Character` | `qa_app_shared_sim_attach_q2_character` | `shared_simulation_players.c` | B3 | done |
| 4672-4680 | `spawnCharacterGib` | `qa_app_shared_sim_spawn_character_gib` | `shared_simulation_players.c` | B3 | done |
| 4682-4685 | `dropPlayerInventory` | `qa_app_shared_sim_drop_player_inventory` | `shared_simulation_players.c` | B3 | done |
| 4687-4693 | `respawnPlayer` | `qa_app_shared_sim_respawn_player` | `shared_simulation_players.c` | B3 | done |
| 4695-4707 | `placeQ1Player` | `qa_app_shared_sim_place_q1_player` | `shared_simulation_players.c` | B3 | done |
| 4709-4724 | `killBox` | `qa_app_shared_sim_kill_box` | `shared_simulation_players.c` | B3 | done |
| 4726-4738 | `arsenal` | `qa_app_shared_sim_arsenal` | `shared_simulation_arsenal.c` | B2 | done |
| 4740-4802 | `weaponStep` | `qa_app_shared_sim_weapon_step` | `shared_simulation_arsenal.c` | B2 | done (passive selected reads adopted as the player arsenal; step.c frees its selected input read; runtime-rest) |
| 4804-4811 | `frameSelectedQ2Weapon` | `qa_app_shared_sim_frame_selected_q2_weapon` | `shared_simulation_arsenal.c` | B2 | done |
| 4813-4819 | `playerWeapon` | `qa_app_shared_sim_player_weapon` | `shared_simulation_arsenal.c` | B2 | done |
| 4821-4837 | `q2WeaponInput` | `qa_app_shared_sim_q2_weapon_input` | `shared_simulation_arsenal.c` | B2 | done |
| 4839-4881 | `setPlayerMovement` | `qa_app_shared_sim_set_player_movement` | `shared_simulation_players.c` | B3 | done |
| 4883-4890 | `projectQuakeCAttack` | `qa_app_shared_sim_project_quake_c_attack` | `shared_simulation_arsenal.c` | B2 | done |
| 4892-4910 | `quakeCCharacterStep` | `qa_app_shared_sim_quake_c_character_step` | `shared_simulation_players.c` | B3 | done |
| 4912-4920 | `animationStep` | `qa_app_shared_sim_animation_step` | `shared_simulation_players.c` | B3 | done |
| 4922-4952 | `touch` | `qa_app_shared_sim_touch` | `shared_simulation_players.c` | B3 | done |
| 4954-4974 | `jump` | `qa_app_shared_sim_jump` | `shared_simulation_players.c` | B3 | done |
| 4976-5019 | `prepareArsenalCommand` | `qa_app_shared_sim_prepare_arsenal_command` | `shared_simulation_arsenal.c` | B2 | done |
| 5021-5031 | `queueQuakeWorldCommands` | `qa_app_shared_sim_queue_quake_world_commands` | `shared_simulation_players.c` | B3 | done |
| 5032-5037 | `queueQuakeWorldAction` | `qa_app_shared_sim_queue_quake_world_action` | `shared_simulation_players.c` | B3 | done |
| 5038-5047 | `runQuakeWorldNewMissile` | `qa_app_shared_sim_run_quake_world_new_missile` | `shared_simulation_arsenal.c` | B2 | done |
| 5048-5102 | `runQuakeWorldCommands` | `qa_app_shared_sim_run_quake_world_commands` | `shared_simulation_players.c` | B3 | done (needs qa_app_sim_movement_player_with_input_command, phase A gap) |
| 5104-5110 | `beginQvmSelectedFrame` | `qa_app_shared_sim_begin_qvm_selected_frame` | `shared_simulation_step.c` | A | done |
| 5111-5143 | `runQvmSelectedActors` | `qa_app_shared_sim_run_qvm_selected_actors` | `shared_simulation_step.c` | A | done |
| 5145-5217 | `stepAsync` | `qa_app_shared_sim_step_async` | `shared_simulation_step.c` | A | done |
| 5219-5621 | `step` | `qa_app_shared_sim_step` | `shared_simulation_step.c` | A | done |
| 5624-5666 | `finishFrameState` | `qa_app_shared_sim_finish_frame_state` | `shared_simulation_players.c` | B3 | done (phase A inlined it in shared_simulation_step.c, ss_end_player_frames) |
| 5669-5693 | `concludeSourceFrame` | `qa_app_shared_sim_conclude_source_frame` | `shared_simulation_players.c` | B3 | done (phase A inlined it in shared_simulation_step.c, the step boundary frame-exit block) |
| 5695-5695 | `currentOutput` | `qa_app_shared_sim_current_output` | `shared_simulation_step.c` | A | done |
| 5696-5704 | `snapshot` | `qa_app_shared_sim_snapshot` | `shared_simulation_step.c` | A | done |
| 5706-5741 | `slotProjection` | `qa_app_shared_sim_slot_projection` | `shared_simulation_arsenal.c` | B2 | done |
| 5742-5754 | `nativeDropProtection` | `qa_app_shared_sim_native_drop_protection` | `shared_simulation_arsenal.c` | B2 | done |
| 5755-5766 | `nativeDropAmmoSource` | `qa_app_shared_sim_native_drop_ammo_source` | `shared_simulation_arsenal.c` | B2 | done |
| 5767-5790 | `nativeDropProjection` | `qa_app_shared_sim_native_drop_projection` | `shared_simulation_arsenal.c` | B2 | done |
| 5791-5830 | `nativeInventoryRows` | `qa_app_shared_sim_native_inventory_rows` | `shared_simulation_arsenal.c` | B2 | done |
| 5831-5840 | `useSelectedInventoryItem` | `qa_app_shared_sim_use_selected_inventory_item` | `shared_simulation_arsenal.c` | B2 | done |
| 5841-5866 | `nativePlayerUi` | `qa_app_shared_sim_native_player_ui` | `shared_simulation_arsenal.c` | B2 | done |
| 5867-5867 | `playerUi` | `qa_app_shared_sim_player_ui` | `shared_simulation_arsenal.c` | B2 | done |
| 5873-5910 | `tickHordeWave` | `qa_app_shared_sim_tick_horde_wave` | `shared_simulation_worlds.c` | B1 | done |
| 5914-5920 | `requestHordeBot` | `qa_app_shared_sim_request_horde_bot` | `shared_simulation_worlds.c` | B1 | done |
| 5924-5927 | `foreignTag` (get) | `qa_app_shared_sim_foreign_tag` | `shared_simulation_worlds.c` | B1 | done |
| 5929-5964 | `tickTag` | `qa_app_shared_sim_tick_tag` | `shared_simulation_worlds.c` | B1 | done |
| 5966-5969 | `addTagScore` | `qa_app_shared_sim_add_tag_score` | `shared_simulation_worlds.c` | B1 | done |
| 5971-5974 | `giveTagInventory` | `qa_app_shared_sim_give_tag_inventory` | `shared_simulation_worlds.c` | B1 | done |
| 5976-5979 | `takeTagInventory` | `qa_app_shared_sim_take_tag_inventory` | `shared_simulation_worlds.c` | B1 | done |
| 5982-5993 | `tagBonus` | `qa_app_shared_sim_tag_bonus` | `shared_simulation_worlds.c` | B1 | done |
| 5995-6001 | `grantTagQuad` | `qa_app_shared_sim_grant_tag_quad` | `shared_simulation_worlds.c` | B1 | done |
| 6005-6018 | `tagSpawnOrigins` | `qa_app_shared_sim_tag_spawn_origins` | `shared_simulation_worlds.c` | B1 | done |
| 6020-6029 | `farthestDeathmatchSpawn` | `qa_app_shared_sim_farthest_deathmatch_spawn` | `shared_simulation_worlds.c` | B1 | done |
| 6031-6043 | `spawnTagToken` | `qa_app_shared_sim_spawn_tag_token` | `shared_simulation_worlds.c` | B1 | done |
| 6045-6057 | `tagTokenTouched` | `qa_app_shared_sim_tag_token_touched` | `shared_simulation_worlds.c` | B1 | done |
| 6059-6062 | `foreignLmctf` (get) | `qa_app_shared_sim_foreign_lmctf` | `shared_simulation_worlds.c` | B1 | done |
| 6065-6067 | `foreignCtf` (get) | `qa_app_shared_sim_foreign_ctf` | `shared_simulation_worlds.c` | B1 | done |
| 6069-6069 | `flagMode` (get) | `qa_app_shared_sim_flag_mode` | `shared_simulation_worlds.c` | B1 | done |
| 6071-6074 | `lmctfRuneHeld` | `qa_app_shared_sim_lmctf_rune_held` | `shared_simulation_worlds.c` | B1 | done |
| 6076-6078 | `lmctfHaste` | `qa_app_shared_sim_lmctf_haste` | `shared_simulation_worlds.c` | B1 | done |
| 6082-6086 | `lmctfQ1HasteRules` | `qa_app_shared_sim_lmctf_q1_haste_rules` | `shared_simulation_worlds.c` | B1 | done |
| 6089-6102 | `lmctfOrigins` | `qa_app_shared_sim_lmctf_origins` | `shared_simulation_worlds.c` | B1 | done |
| 6104-6123 | `lmctfFlagHome` | `qa_app_shared_sim_lmctf_flag_home` | `shared_simulation_worlds.c` | B1 | done |
| 6125-6138 | `spawnLmctfFlag` | `qa_app_shared_sim_spawn_lmctf_flag` | `shared_simulation_worlds.c` | B1 | done |
| 6140-6170 | `lmctfFlagTouched` | `qa_app_shared_sim_lmctf_flag_touched` | `shared_simulation_worlds.c` | B1 | done |
| 6172-6175 | `lmctfTakeFlag` | `qa_app_shared_sim_lmctf_take_flag` | `shared_simulation_worlds.c` | B1 | done |
| 6177-6190 | `spawnLmctfRune` | `qa_app_shared_sim_spawn_lmctf_rune` | `shared_simulation_worlds.c` | B1 | done |
| 6192-6201 | `lmctfRuneTouched` | `qa_app_shared_sim_lmctf_rune_touched` | `shared_simulation_worlds.c` | B1 | done |
| 6203-6226 | `prepareLmctf` | `qa_app_shared_sim_prepare_lmctf` | `shared_simulation_worlds.c` | B1 | done |
| 6228-6294 | `tickLmctf` | `qa_app_shared_sim_tick_lmctf` | `shared_simulation_worlds.c` | B1 | done |
| 6297-6300 | `foreignDeathball` (get) | `qa_app_shared_sim_foreign_deathball` | `shared_simulation_worlds.c` | B1 | done |
| 6305-6361 | `deathballOrigins` | `qa_app_shared_sim_deathball_origins` | `shared_simulation_worlds.c` | B1 | done |
| 6363-6373 | `spawnDeathball` | `qa_app_shared_sim_spawn_deathball` | `shared_simulation_worlds.c` | B1 | done |
| 6375-6379 | `prepareDeathball` | `qa_app_shared_sim_prepare_deathball` | `shared_simulation_worlds.c` | B1 | done |
| 6381-6415 | `tickDeathball` | `qa_app_shared_sim_tick_deathball` | `shared_simulation_worlds.c` | B1 | done |
| 6417-6419 | `weaponSlot` | `qa_app_shared_sim_weapon_slot` | `shared_simulation_arsenal.c` | B2 | done |
| 6421-6473 | `primaryUi` | `qa_app_shared_sim_primary_ui` | `shared_simulation_arsenal.c` | B2 | done |
| 6475-6496 | `setSourcePlayerIdentity` | `qa_app_shared_sim_set_source_player_identity` | `shared_simulation_players.c` | B3 | done |
| 6497-6533 | `setPlayerFieldOfView` | `qa_app_shared_sim_set_player_field_of_view` | `shared_simulation_players.c` | B3 | done |
| 6534-6534 | `takeModClientDrops` | `qa_app_shared_sim_take_mod_client_drops` | `shared_simulation_players.c` | B3 | done |
| 6535-6535 | `rememberModClientCommand` | `qa_app_shared_sim_remember_mod_client_command` | `shared_simulation_players.c` | B3 | done |
| 6536-6541 | `requireModClient` | `qa_app_shared_sim_require_mod_client` | `shared_simulation_players.c` | B3 | done |
| 6542-6553 | `notifyClientEvent` | `qa_app_shared_sim_notify_client_event` | `shared_simulation_players.c` | B3 | done (tested) |
| 6554-6583 | `observeClientCommand` | `qa_app_shared_sim_observe_client_command` | `shared_simulation_players.c` | B3 | done |
| 6584-6599 | `canonicalTeam` | `qa_app_shared_sim_canonical_team` | `shared_simulation_worlds.c` | B1 | done |
| 6600-6657 | `primaryMatchPlayer` | `qa_app_shared_sim_primary_match_player` | `shared_simulation_worlds.c` | B1 | done |
| 6659-6673 | `storePlayerUserinfo` | `qa_app_shared_sim_store_player_userinfo` | `shared_simulation_players.c` | B3 | done (tested) |
| 6674-6674 | `sourcePlayerName` | `qa_app_shared_sim_source_player_name` | `shared_simulation_players.c` | B3 | done (tested) |
| 6675-6675 | `q1Paused` (get) | `qa_app_shared_sim_q1_paused` | `shared_simulation_players.c` | B3 | done (tested) |
| 6676-6686 | `toggleQ1Pause` | `qa_app_shared_sim_toggle_q1_pause` | `shared_simulation_players.c` | B3 | done (tested) |
| 6687-6701 | `sourcePlayerUserinfo` | `qa_app_shared_sim_source_player_userinfo` | `shared_simulation_players.c` | B3 | done (tested) |
| 6702-6737 | `updatePlayerUserinfo` | `qa_app_shared_sim_update_player_userinfo` | `shared_simulation_players.c` | B3 | done |
| 6738-6748 | `playerView` | `qa_app_shared_sim_player_view` | `shared_simulation_players.c` | B3 | done |
| 6749-6794 | `sourcePlayerView` | `qa_app_shared_sim_source_player_view` | `shared_simulation_players.c` | B3 | done |
| 6795-6795 | `sourceEntityText` (get) | `qa_app_shared_sim_source_entity_text` | `shared_simulation_worlds.c` | B1 | done |
| 6797-6800 | `q1WeaponSource` | `qa_app_shared_sim_q1_weapon_source` | `shared_simulation_worlds.c` | B1 | done |
| 6801-6803 | `q2ItemWeaponSource` | `qa_app_shared_sim_q2_item_weapon_source` | `shared_simulation_worlds.c` | B1 | done |
| 6804-6807 | `q2WeaponSource` | `qa_app_shared_sim_q2_weapon_source` | `shared_simulation_worlds.c` | B1 | done |
| 6808-6810 | `selectedQ3WeaponSource` | `qa_app_shared_sim_selected_q3_weapon_source` | `shared_simulation_worlds.c` | B1 | done |
| 6812-6821 | `initializeServerSettings` | `qa_app_shared_sim_initialize_server_settings` | `shared_simulation_core.c` | A | done |
| 6822-6822 | `q2ServerCvars` | `qa_app_shared_sim_q2_server_cvars` | `shared_simulation_worlds.c` | B1 | done |
| 6823-6829 | `q2MovementConfig` | `qa_app_shared_sim_q2_movement_config` | `shared_simulation_worlds.c` | B1 | done |
| 6830-6843 | `serverSettings` | `qa_app_shared_sim_server_settings` | `shared_simulation_worlds.c` | B1 | done |
| 6844-6844 | `serverProfile` | `qa_app_shared_sim_server_profile` | `shared_simulation_worlds.c` | B1 | done |
| 6845-6845 | `q2Source` | `qa_app_shared_sim_q2_source` | `shared_simulation_worlds.c` | B1 | done |
| 6846-6846 | `quakecSource` | `qa_app_shared_sim_quakec_source` | `shared_simulation_worlds.c` | B1 | done |
| 6847-6847 | `q1Source` | `qa_app_shared_sim_q1_source` | `shared_simulation_worlds.c` | B1 | done |
| 6848-6852 | `playerClient` | `qa_app_shared_sim_player_client` | `shared_simulation_core.c` | A | done |
| 6853-6853 | `q2Native` | `qa_app_shared_sim_q2_native` | `shared_simulation_worlds.c` | B1 | done |
| 6854-6856 | `q2NativePlayers` | `qa_app_shared_sim_q2_native_players` | `shared_simulation_worlds.c` | B1 | done |
| 6857-6898 | `captureNativeQ2Travel` | `qa_app_shared_sim_capture_native_q2_travel` | `shared_simulation_worlds.c` | B1 | done (runtime-rest; the record owns its storage, free with qa_app_shared_sim_native_q2_travel_free; carried visited levels stay borrowed from options.nativeQ2Travel) |
| 6899-6913 | `admitQ2NativePlayer` | `qa_app_shared_sim_admit_q2_native_player` | `shared_simulation_worlds.c` | B1 | done |
| 6914-6933 | `registerQ2NativeClient` | `qa_app_shared_sim_register_q2_native_client` | `shared_simulation_worlds.c` | B1 | done |
| 6934-6938 | `nativeQ2Client` | `qa_app_shared_sim_native_q2_client` | `shared_simulation_worlds.c` | B1 | done |
| 6939-6948 | `nativeDroppedItem` | `qa_app_shared_sim_native_dropped_item` | `shared_simulation_arsenal.c` | B2 | done |
| 6950-6957 | `restoreNativeInventoryCursors` | `qa_app_shared_sim_restore_native_inventory_cursors` | `shared_simulation_arsenal.c` | B2 | done |
| 6961-6966 | `foreignNativeMovement` | `qa_app_shared_sim_foreign_native_movement` | `shared_simulation_worlds.c` | B1 | done |
| 6970-6972 | `foreignNativeCharacter` | `qa_app_shared_sim_foreign_native_character` | `shared_simulation_worlds.c` | B1 | done |
| 6977-6985 | `attachNativeCharacter` | `qa_app_shared_sim_attach_native_character` | `shared_simulation_worlds.c` | B1 | done |
| 6987-6990 | `nativeCharacterHost` | `qa_app_shared_sim_native_character_host` | `shared_simulation_worlds.c` | B1 | done |
| 6994-6997 | `presentationPlayers` | `qa_app_shared_sim_presentation_players` | `shared_simulation_worlds.c` | B1 | done |
| 6999-7044 | `tickNativeCharacters` | `qa_app_shared_sim_tick_native_characters` | `shared_simulation_worlds.c` | B1 | done |
| 7056-7077 | `syncNativeMovementCombat` | `qa_app_shared_sim_sync_native_movement_combat` | `shared_simulation_worlds.c` | B1 | done |
| 7080-7086 | `nativeWaterContents` | `qa_app_shared_sim_native_water_contents` | `shared_simulation_worlds.c` | B1 | done |
| 7088-7265 | `bindNativeInput` | `qa_app_shared_sim_bind_native_input` | `shared_simulation_worlds.c` | B1 | done (runtime-rest; native-pmove-contacts.ts ported as native_pmove_contacts.c, nativeMoveSlices added to native_commands.c, NativeInputMotionOutputs touches/groundPlane brought to TS in compat/q2/native_input) |
| 7266-7270 | `beginNativeQ2Step` | `qa_app_shared_sim_begin_native_q2_step` | `shared_simulation_step.c` | A | done |
| 7271-7278 | `stepNativeQ2` | `qa_app_shared_sim_step_native_q2` | `shared_simulation_step.c` | A | done |
| 7279-7295 | `stepNativeQ2Async` | `qa_app_shared_sim_step_native_q2_async` | `shared_simulation_step.c` | A | done |
| 7296-7343 | `nativeQ2Frames` | `qa_app_shared_sim_native_q2_frames` | `shared_simulation_step.c` | A | done |
| 7344-7344 | `q3Guest` | `qa_app_shared_sim_q3_guest` | `shared_simulation_worlds.c` | B1 | done |
| 7345-7345 | `shutdownQ3Guest` | `qa_app_shared_sim_shutdown_q3_guest` | `shared_simulation_worlds.c` | B1 | done |
| 7346-7354 | `sourceRestartPlan` | `qa_app_shared_sim_source_restart_plan` | `shared_simulation_worlds.c` | B1 | done |
| 7356-7385 | `restartSourceRound` | `qa_app_shared_sim_restart_source_round` | `shared_simulation_worlds.c` | B1 | done (qa_app_q3_source_runtime_new(options, host, mode); the created host carries over) |
| 7387-7395 | `beginSourceRoundSettlement` | `qa_app_shared_sim_begin_source_round_settlement` | `shared_simulation_worlds.c` | B1 | done |
| 7397-7403 | `completeSourceRoundSettlement` | `qa_app_shared_sim_complete_source_round_settlement` | `shared_simulation_worlds.c` | B1 | done |
| 7405-7418 | `teamGame` | `qa_app_shared_sim_team_game` | `shared_simulation_worlds.c` | B1 | done |
| 7420-7420 | `q3Source` | `qa_app_shared_sim_q3_source` | `shared_simulation_worlds.c` | B1 | done |
| 7421-7421 | `selectedQ3SourceState` | `qa_app_shared_sim_selected_q3_source_state` | `shared_simulation_worlds.c` | B1 | done |
| 7422-7422 | `movementPlayer` | `qa_app_shared_sim_movement_player` | `shared_simulation_players.c` | B3 | done |
| 7423-7425 | `weaponPresentationClock` | `qa_app_shared_sim_weapon_presentation_clock` | `shared_simulation_arsenal.c` | B2 | done |
| 7426-7426 | `q2PlayerView` | `qa_app_shared_sim_q2_player_view` | `shared_simulation_players.c` | B3 | done |
| 7427-7442 | `controlPlayer` | `qa_app_shared_sim_control_player` | `shared_simulation_players.c` | B3 | done |
| 7443-7451 | `setWorldGravity` | `qa_app_shared_sim_set_world_gravity` | `shared_simulation_players.c` | B3 | done |
| 7452-7473 | `disconnectPlayer` | `qa_app_shared_sim_disconnect_player` | `shared_simulation_players.c` | B3 | done |
| 7474-7474 | `requirePlayer` | `qa_app_shared_sim_require_player` | `shared_simulation_core.c` | A | done |
| 7475-7475 | `registerResource` | `qa_app_shared_sim_register_resource` | `shared_simulation_arsenal.c` | B2 | done |
| 7476-7480 | `submitDebugShapes` | `qa_app_shared_sim_submit_debug_shapes` | `shared_simulation_arsenal.c` | B2 | done |
| 7481-7486 | `beginPresentationFrame` | `qa_app_shared_sim_begin_presentation_frame` | `shared_simulation_arsenal.c` | B2 | done |
| 7487-7487 | `debugLines` | `qa_app_shared_sim_debug_lines` | `shared_simulation_arsenal.c` | B2 | done |
| 7488-7494 | `worldText` | `qa_app_shared_sim_world_text` | `shared_simulation_arsenal.c` | B2 | done |
| 7496-7496 | `drainPresentationEvents` | `qa_app_shared_sim_drain_presentation_events` | `shared_simulation_arsenal.c` | B2 | done (calls PresentationState.takePresentation, a presentation_state.c gap) |
| 7498-7509 | `capturePresentationSnapshot` | `qa_app_shared_sim_capture_presentation_snapshot` | `shared_simulation_arsenal.c` | B2 | done |
| 7511-7517 | `presentationFraction` | `qa_app_shared_sim_presentation_fraction` | `shared_simulation_arsenal.c` | B2 | done |
| 7518-7527 | `interpolatedPose` | `qa_app_shared_sim_interpolated_pose` | `shared_simulation_arsenal.c` | B2 | done |
| 7529-7540 | `presentations` | `qa_app_shared_sim_presentations` | `shared_simulation_arsenal.c` | B2 | done |
| 7541-7543 | `q1VisualFields` | `qa_app_shared_sim_q1_visual_fields` | `shared_simulation_arsenal.c` | B2 | done |
| 7544-7662 | `primaryPresentations` | `qa_app_shared_sim_primary_presentations` | `shared_simulation_arsenal.c` | B2 | done |
| 7664-7678 | `characterViews` | `qa_app_shared_sim_character_views` | `shared_simulation_arsenal.c` | B2 | done |
| 7680-7688 | `grantSelectedArsenal` | `qa_app_shared_sim_grant_selected_arsenal` | `shared_simulation_arsenal.c` | B2 | done |
| 7690-7702 | `giveNativeSelectedAmmo` | `qa_app_shared_sim_give_native_selected_ammo` | `shared_simulation_arsenal.c` | B2 | done |
| 7704-7748 | `giveSelectedItem` | `qa_app_shared_sim_give_selected_item` | `shared_simulation_arsenal.c` | B2 | done |
| 7750-7752 | `arsenalState` | `qa_app_shared_sim_arsenal_state` | `shared_simulation_arsenal.c` | B2 | done |
| 7754-7766 | `selectedWeaponCommand` | `qa_app_shared_sim_selected_weapon_command` | `shared_simulation_arsenal.c` | B2 | done |
| 7768-7791 | `sourceItemCommand` | `qa_app_shared_sim_source_item_command` | `shared_simulation_arsenal.c` | B2 | done |
| 7793-7910 | `playerCommand` | `qa_app_shared_sim_player_command` | `shared_simulation_players.c` | B3 | done (needs B2 qa_app_shared_sim_selected_arsenal_ui and Q3 runtime intermission/cheatsOk gaps) |
| 7912-7952 | `captureTravel` | `qa_app_shared_sim_capture_travel` | `shared_simulation_players.c` | B3 | done |
| 7955-7966 | `sharedTravelState` | `qa_app_shared_sim_shared_travel_state` | `shared_simulation_players.c` | B3 | done |
| 7969-7973 | `sharedCarryOf` | `qa_app_shared_sim_shared_carry_of` | `shared_simulation_players.c` | B3 | done (tested) |
| 7978-8001 | `applySharedTravelCarry` | `qa_app_shared_sim_apply_shared_travel_carry` | `shared_simulation_players.c` | B3 | done |
| 8005-8014 | `applySharedTravelOnce` | `qa_app_shared_sim_apply_shared_travel_once` | `shared_simulation_players.c` | B3 | done |
| 8019-8028 | `applyGuestTravelCarries` | `qa_app_shared_sim_apply_guest_travel_carries` | `shared_simulation_players.c` | B3 | done |
| 8030-8030 | `admitTravel` | `qa_app_shared_sim_admit_travel` | `shared_simulation_players.c` | B3 | done |
| 8032-8066 | `restoreCampaignTravel` | `qa_app_shared_sim_restore_campaign_travel` | `shared_simulation_players.c` | B3 | done |
| 8068-8084 | `captureOriginalSave` | `qa_app_shared_sim_capture_original_save` | `shared_simulation_players.c` | B3 | done |
| 8086-8097 | `restoreOriginalSave` | `qa_app_shared_sim_restore_original_save` | `shared_simulation_players.c` | B3 | done |
| 8099-8099 | `pendingMatchMap` | `qa_app_shared_sim_pending_match_map` | `shared_simulation_worlds.c` | B1 | done |
| 8101-8101 | `sourceObjectives` | `qa_app_shared_sim_source_objectives` | `shared_simulation_worlds.c` | B1 | done |
| 8102-8107 | `takeTransitions` | `qa_app_shared_sim_take_transitions` | `shared_simulation_players.c` | B3 | done (tested) |
| 8108-8108 | `takeLevelChange` | `qa_app_shared_sim_take_level_change` | `shared_simulation_players.c` | B3 | done (tested) |
| 8109-8112 | `assertBotRestoreReady` | `qa_app_shared_sim_assert_bot_restore_ready` | `shared_simulation_players.c` | B3 | done |
| 8113-8128 | `assertCheckpointReady` | `qa_app_shared_sim_assert_checkpoint_ready` | `shared_simulation_players.c` | B3 | done |
| 8129-8129 | `checkpoint` | `qa_app_shared_sim_checkpoint` | `shared_simulation_checkpoint.c` | B3 | done (image owned by `qa_app_shared_sim_save`) |
| 8130-8136 | `checkpointModsForTravel` | `qa_app_shared_sim_checkpoint_mods_for_travel` | `shared_simulation_players.c` | B3 | done |
| 8137-8152 | `checkpointLoading` | `qa_app_shared_sim_checkpoint_loading` | `shared_simulation_checkpoint.c` | B3 | done (mods, weapon components and the rerelease native write pumped synchronously) |
| 7548-7683 | `checkpointFromSource` | `qa_app_shared_sim_checkpoint_from_source` | `shared_simulation_checkpoint.c` | B3 | done: every member and writer; persistence hosts through fn+ctx views (`ck_hosts`); writers byte-identical with TS records (test_shared_simulation_save); the whole image not compared against a running TS simulation |
| (nativeOriginal) | rerelease `encodeQ2RereleaseNativeSave`, classic `files.capture` + `encodeQ2ClassicOriginalSave` | `qa_app_shared_sim_native_original_checkpoint` | `shared_simulation_checkpoint.c` | B3 | done |
| 7666-7683 | `nativeRestorePrelude` | `ck_restore_native_original` (static) | `shared_simulation_checkpoint.c` | B3 | done |
| 7685-7701 | `restoreNativeLoading` | `qa_app_shared_sim_restore_native_loading` | `shared_simulation_checkpoint.c` | B3 | done (pumped synchronously) |
| 7703-8073 | `restore` | `qa_app_shared_sim_restore` | `shared_simulation_checkpoint.c` | B3 | done; not run (no test constructs a SharedSimulation) |
| 975-1003 | load's `SessionMods.open` | `qa_app_shared_sim_open_mods` | `shared_simulation_mods.c` | B3 | done; not run |
| 8681-8704 | `close` | `qa_app_shared_sim_close` | `shared_simulation_core.c` | A | done |
| 8705-8705 | `assertOpen` | `qa_app_shared_sim_assert_open` | `shared_simulation_core.c` | A | done |
| 8708-8708 | `createSimulation` | `qa_app_shared_simulation_create` | `shared_simulation_core.c` | A | done |
| 8714-8742 | `orderedActorTurns` | `qa_app_sim_ordered_actor_turns_next (existing, runtime.c)` | `shared_simulation_core.c` | A | done |
| 8744-8744 | `loadSimulation` | `qa_app_shared_simulation_load` | `shared_simulation_core.c` | A | done |
| 8747-8752 | `RevisionMap` | `qa_ss_map (revision counter)` | `shared_simulation_support.c` | A | done |

## Phase A: what is left

- The two Q3 `createSource` branches are done (q3-source): `qa_app_shared_sim_create_q3_source` builds the
  createQ3SourceHost operations and `qa_app_q3_source_runtime_new(options, host, mode)`, which constructs every
  content/q3 subsystem from the Q3SourceHost as the TS constructor does (q3/runtime.c `owner_fill_from_host`);
  `qa_app_shared_sim_create_q3_qvm_source` prepares the guest (Q3ServerState with filterUserCommand,
  Q3QvmServerGame over the typed PreparedQ3Game, Q3GuestCatalog, QvmCombatBindings, QvmPrimaryPickups,
  QvmPrimaryWeapons, restore). Q3QvmServerGame owns its QvmGame (artifact to module), QvmFiles, the entity-token
  parser and the qagame syscall host; `bindInput(definition, services)` is
  `qa_app_q3_qvm_server_game_bind_input`, driven by `qa_app_shared_sim_bind_qvm_input`.
- `qa_app_shared_sim_open_mods` (load's `SessionMods.open({...})`) is in shared_simulation_mods.c: weapons/match/clients
  services, damageContext, referenceSaved ("checkpoint"/"current"), the engine fragment with the typed scene/physics views of
  world/session/mod_engine.h, `clients.visibility` as the Q1ClientVisibilityScene over the shared scene and `clients.at` as a
  Q1VisibilityClient, presentation camera/players, and `bindOwner` adapted from `qa_app_presentation_state_bind_owner` to
  `qa_session_mod_bound_events` (bindings live until the simulation is destroyed; close only closes). The opaque option members
  are read as: `prepared_mods[i]` a `qa_prepared_mod *`, `enabled_mods` a `qa_app_sim_mod_selections`, `mod_travel` a
  `qa_mod_travel_checkpoint`, `mod_commands`/`mod_files` the mods.h tables (`qa_app_sim_enabled_mod_count`/`_mod_travel_count`
  are defined there).

## Checkpoint and restore (B3, shared_simulation_checkpoint.c)

- Save image contract (persistence/save_image.c): schema 4, `QTSAVE5` envelope with the u32 header length and
  `{map, content}` header, `QTSAVE4/3/2` decode, `consumption`, `qa_read_save_header`, and the two SaveImage key orders
  (`qa_save_image.key_order`: checkpointFromSource order vs parseSaveImage order, which differ in where `mods`,
  `consumption` and a think's `executionProvider` sit). Recipe resource provenance/resolution, execution modules and
  random states now encode in the TS key order. saved_game.c decodes and writes shared images. Checked byte for byte
  against bun in tests/app/test_shared_simulation_save.c.
- One TS Q1FoundationCheckpoint, two C shapes: `qa_persist_q1_foundation_from_content` / `_to_content` /
  `_content_free` and `qa_q1_foundation_checkpoint_value` (persistence/q1_foundation.h). The C Q1 content capture is
  still version 5; TS is version 6 (Q1 content drift, not ported here).
- checkpointFromSource writers (checkpoint_values.c unless noted): Q3 arsenal state, source movement (ClientMovementOptions in the
  literal order that built it: ClientThink, the spectator policy or the restored record, `qa_app_sim_client_movement_order`;
  players.h gained `debug_level`, `ladders` and `order`), saveQ2Attack, HandGrenadeRuntime.capture, GrappleRuntime.capture (four
  sources, QvmGrappleSource.capture), WeaponSlot.snapshot, captureForeignWorld, Q3SelectedArsenal.capture (ArsenalState in its
  family's literal order), Q3CharacterActor.capture, Q1TravelState, the Q2 "model" event, Q2PlayerView;
  SourcePickupCargo.capture/restore (dropped_pickups.c); `qa_q2_game_services_capture` (entity_services.c, through
  `qa_q2_entity_services_of_game`); the full `SimulationWeaponBehaviors.checkpoint/checkpointLoading` record
  (weapon_behavior_runtime.c: QuakeC, QVM and rerelease source records and the attachments); exported
  `qa_save_weapon_behavior_definition_value` and `qa_app_sim_rerelease_source_save_value`.
  `qa_app_prepared_q3_primary_declaration_digest` is defined by the q3-source porter (guest_artifact.c).
- Key order after a restore: TS readers build some records in another key order than the live owner (grapple controls and
  GrappleWeaponState, the Q3 selected `ArsenalState`, the Q2 `sourceModels` alpha, ClientMovementOptions). Only
  ClientMovementOptions tracks it in C (`order`); the others write the live order, so a checkpoint taken after a restore can
  differ from TS in key order (never in values) for those records.
- checkpointFromSource passed the opaque registry, body table, authority and inventory where persistence takes fn+ctx tables
  with the same struct tags: fixed with `ck_hosts`.
- Restore lifetimes: the deferred shared-world completion keeps its host, storage closure and save until finish(); they live in a
  `ck_restore_ctx` released with the simulation (`qa_app_shared_sim_own`, LIFO with the B2 owners). Decoded native original saves
  (their visited levels stay in source.visited), the selected-Q2-weapon and Q2 character decoders, migrated legacy Q3 supply
  profiles and read mod-client commands are kept the same way; the world:simulation tree is kept when the selected Q1 hydrate, a
  pending QVM grapple restore, Q3 selected arsenals or mod-client receipts borrow it.
- Not compared with TS yet: captureForeignWorld (module checkpoints) and non-empty weapon-behavior sources (no world-free TS
  constructor); the record shapes are the reader's, the module bodies come from the persistence encoders.
- player_checkpoint.c `qa_app_sim_capture_movement_player` (players[], not in this assignment) still writes a "minimal" arsenal
  tree (every weapon-state member of every family); it should use `qa_app_sim_arsenal_state_value`.

## Dependencies outside runtime.ts that the phase A code calls (declared in shared_simulation_internal.h)

These are not runtime.ts methods; each is a gap in another module's port. They are listed under
"Dependencies other modules do not export yet" in the internal header.

| Symbol | TS source | Gap |
|---|---|---|
| `qa_app_presentation_state_create_source`, `_emit_owned`, `_resource_for`, `_register_resource`, `_light_style`, `_assert_output_consumed`, `_retire_actor`, `_finish_owner_restore`, `qa_app_shared_sim_light_styles` | presentation-state.ts (SimulationEvents' super) | presentation_state.c ports owner lifecycle only (1341 of 9645 tokens). SimulationEvents cannot emit without it. |
| SimulationEvents capture()/restore() and emitOwned virtual (events.ts 87-92) | events.ts over presentation-state.ts | done (B3): `qa_app_shared_sim_events_capture`/`_restore` (shared_simulation_players.c) combine `qa_app_presentation_state_capture`/`_restore` with the events `sequence`; the constructor installs SimulationEvents.emitOwned with `qa_app_presentation_state_set_emit_owned_override` (shared_simulation_core.c, after events create). |
| `qa_app_prepared_q3_primary_input`, `qa_app_prepared_q3_primary_weapons` | q3/guest-artifact.ts PreparedQ3Game.primary | done: the prepared game is typed (q3/guest_artifact.h) |
| `qa_app_q3_qvm_observe_player_move`, `qa_app_q3_qvm_observer_remove` | guest.game.module.observeFunction (stepAsync grapple gravity) | no C module observer on the server game |
| `qa_app_q3_selected_source_bridge`, `qa_app_q3_selected_source_native_classname` | Q3SelectedSource.bridge / records.nativeByActor | the C owner keeps these private (`qa_app_q3_source_runtime_bridge` is done in q3/runtime.h) |
| `qa_q2m_monsters_note_power_armor` | content/q2/foundation/monsters notePowerArmor | TS drift after the C port |
| `qa_app_sim_create_native_q1_pusher_services` | native-q1-pusher.ts (26 lines) | native_q1_pusher.c is a shell |
| `qa_app_sim_quakec_source_attacks`, `_projectiles`, `_q1qc_machine` | QuakeCSource.attacks/projectiles/machine | quakec_source.c keeps them private (damageRequest needs them) |
| `qa_app_sim_native_guest_module_identity` | q2-native-world.ts nativeModuleIdentity(prepared) | C takes the wrapper prepared type, not the edition guest |
| `qa_app_sim_classic_guest_map_services_of`, `qa_app_sim_rerelease_guest_map_services_of` | the Pick<> of services the world travel binding takes | no C builder |
| `qa_compat_q2_world_text_value` | content/q2/rerelease/world-text.ts q2WorldText | `qa_compat_q2_q2_world_text` returns its input unchanged |
| `qa_app_sim_source_random_rerelease` | SourceRandom.rerelease | random.c keeps it private |
| `qa_app_sim_movement_player_with_input_command` | MovementPlayer.withInputCommand | players.c has no public form |
| `qa_app_sim_hand_grenade_runtime_independent_game` | HandGrenadeRuntime.independent?.game | no accessor |
| `qa_app_sim_selected_q2_monster_modules_end_frame` | selected Q2 monsters.endFrame(game) | q2_monster_sources.h modules are opaque |
| `qa_app_sim_foreign_q1_world_modules_game` | ForeignQ1WorldModules.game | foreign_q1_world.c is a shell |
| `qa_app_sim_player_postures_view` | playerPostures({character, standingBounds, viewHeight}) | C form has no viewHeight |
| `qa_app_sim_enabled_mod_count`, `qa_app_sim_mod_travel_count` | options.enabledMods / modTravel lengths | done (shared_simulation_mods.c reads the `const void *` members as `qa_app_sim_mod_selections` / `qa_mod_travel_checkpoint`) |
| `qa_app_loaded_content_q2_mounts`, `qa_app_loaded_content_product_directory`, `qa_app_loaded_content_q3_demo_restricted` | LoadedApplicationContent.forContent / catalog.product / q3Product.restriction | content.c has no such accessors |

Also not representable in the current C types (TS drift, reported, not stubbed): Q2PlayerHooks.selectEmpty,
Q2PlayerMovement.upMove / MovementPlayer.upMove, Q2GameOptions.instagib, Q2CompositionServices.strongMines as a live
getter (C stores a bool snapshot), SimulationEvents fog options (PresentationState constructor).

## Phase B naming and rules

- Signatures already used by phase A code are in `shared_simulation_internal.h` ("Phase B: declared for the phase A
  callers"); define them with exactly those signatures. Several are C views of a TS member that returns an object:
  `qa_app_shared_sim_monster_target_q1` / `_notarget` (monsterTarget), `qa_app_shared_sim_selected_arsenal_*`
  (selectedArsenal.select/has/remove/admit over the family union), `qa_app_shared_sim_selected_q1_game`
  (selectedArsenal.game).
- Closures: keep the record behind a ctx with `qa_app_shared_sim_keep(sim, size)` (freed by destroy).
- TS Map/Set fields are `qa_ss_map` members of the struct; OwnedActor keys use object identity.
- The phase A objects reference about 170 symbols that are not defined anywhere yet (the phase B methods and the
  dependencies above). Binaries that pull core/source/step/access do not link until they exist; support.o and plan.o
  link alone.

## Oracles

`tests/app/test_shared_simulation_core.c` checks the step() deadline planning against the TS planning block run under
bun (the block copied verbatim from `step`, from `const fixed = ...` to `const boundaries = ...`, over seven cases:
q1 elapsed clamp, q2 classic catch-up cap, q2 rerelease with a foreign Q2 world, q3 server frames with a selected Q3
source, a selected Q2 weapon clock on a Q1 world, a paused LMCTF frame, an active round settlement). The oracle
script is `tests/app/oracles/shared_simulation_plan.ts` (`bun tests/app/oracles/shared_simulation_plan.ts`).
