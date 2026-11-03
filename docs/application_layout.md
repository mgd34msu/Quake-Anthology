# Application layout (application.ts -> C)

Source: `../quake-anthology-ts/src/app/bootstrap/application.ts` (5,309 lines when this table was generated on
2026-11-03; the TS tree is edited concurrently, so re-run the outline before trusting a line number). One TS class,
`Application`, is ported as one C type, `qa_application`, built alongside the temporary "concrete host" layer in
`application.c` (`qa_app_application`, which main.c still uses until the connection step). The two types share no
code. The real class constructs the real `SharedSimulation` (`qa_app_shared_simulation`, shared_simulation.h), never
`qa_app_sim_shared_simulation` (runtime.h).

Naming: member `fooBar` is `qa_application_foo_bar(qa_application *app, ...)`; a setter is `qa_application_set_foo_bar`;
a static member takes no `app`. Fields are members of `struct qa_application` in `src/app/bootstrap/application_internal.h`,
in TS order; the constructor parameters (`launchOptions`, `loadedContent`, `session`, `worldSimulation`, `host`, `identity`,
`localSeats`, `networkPlayerIdentities`, `ownership`) are fields too. TS async members are synchronous (PORTING.md); a TS
throw is `qa_raise`, a TS catch is `qa_try`. Promises the class stores as fields become state: `stepCompletion` and
`worldOperationCompletion` are the `stepping`/`worldOperation` flags (no other C thread can await them), `closing` keeps
the first close's caught error and rethrows it on a later close, `saveOperation` keeps the queued run and a settle callback.

| C file | TS region | Group |
|---|---|---|
| `include/qa/app/bootstrap/application_class.h` | public type, `open`/`openBorrowed`, `step`, `run`, `close`, public getters | A |
| `src/app/bootstrap/application_internal.h` | the struct (every TS field, TS order), the types application.ts defines, every member's declaration | A |
| `src/app/bootstrap/application_core.c` | 408-886, 1235-1300: rankings, settings and save helpers, constructor, static config helpers, guestOptions, guestOutput, accessors, input | A (constructor, accessors) / P1 |
| `src/app/bootstrap/application_access.c` | 1244-1251: clientCommandsBlocked, advanceClientStartup, flushClientCommands (they read members of other regions, so the constructor file links without them) | A |
| `src/app/bootstrap/application_open.c` | 837-846 open/openApplication, 888-1233: openSource, prepareGuestBots, createBots | A (openSource) / P1 |
| `src/app/bootstrap/application_commands.c` | 1302-1750: dialect, input actions, debug graph, tools, source command buffers, kick, restart, game log | P7 |
| `src/app/bootstrap/application_services.c` | 1751-2308: source actions, recordings, administration, network host, openNetwork, native Q2 seats, openGraphical, video restart | P2 |
| `src/app/bootstrap/application_seats.c` | 2310-2846: publishBorrowed, local guest seats, client cvars, base arena, team arena, keys, Q3 seat clients, saved clients, replaceWorld | P3 |
| `src/app/bootstrap/application_transition.c` | 346-407 (WorldTransition), 2848-3690: world replacement transaction stages | P4 |
| `src/app/bootstrap/application_travel.c` | 3690-4239: save/load, restartSourceRound, applyTransition, travel, movies, cinematics | P5 |
| `src/app/bootstrap/application_dispatch.c` | 4241-4930: local player changes, command dispatch (application/guest/source), presentOutput | P6 |
| `src/app/bootstrap/application_step.c` | 4932-5205: step, run, recoverDedicatedFrame, readPixels, captureNextFrame | A |
| `src/app/bootstrap/application_close.c` | 5207-5306: close, closeFailedWorld, closeOwned | A |

## Parallel assignments (balanced by TS lines of members still to port)

Each group owns its C file(s) and adds its tests to `tests/app/test_application_<group>.c`. Every group needs the struct
in `application_internal.h`; add fields only where a TS field was missed, never change existing ones without updating
every user. A member is "not done" until it is ported with every branch and checked against TS.

- **P1 rankings, settings and open helpers** (about 420 TS lines): 409-886 except the fields and constructor, 1193-1300
  (prepareGuestBots, createBots, frontend getters, input, resumeInput, openDedicatedConsole). Files `application_core.c`,
  `application_open.c`.
- **P7 source commands** (about 430): 1302-1750. File `application_commands.c`.
- **P2 services and graphical open** (about 540): 1751-2308. File `application_services.c`.
- **P3 seats, guests, arenas** (about 510): 2310-2846. File `application_seats.c`.
- **P4 world replacement** (about 790): `WorldTransition` 346-407 and 2848-3690. File `application_transition.c`.
  The heaviest group; split at 3189 (stageNetworkHost onward) if two porters are free.
- **P5 save, load and travel** (about 525): 3690-4239. File `application_travel.c`.
- **P6 dispatch and presentation** (about 670): 4241-4930. File `application_dispatch.c`.
- **A core** (this pass): the struct, constructor, open/openBorrowed/openSource, step, run, recoverDedicatedFrame,
  readPixels, captureNextFrame, close, closeFailedWorld, closeOwned.

Order: P1, P7 and P2 first (openSource calls bindSourceCommands, prepareGuestBots, openGraphical, openNetwork,
publishBorrowed, createBots, rankingFrame and commands); P3-P6 in any order after.

## Module-level declarations

| TS lines | TS declaration | C | Status |
|---|---|---|---|
| 206-209 | `NativeServerHost` | `qa_application_native_server_host` (internal header) | done |
| 210-215 | `NativeServer` | `qa_application_native_server` (internal header) | done |
| 217-224 | `SavedBotClientId`, `SavedApplicationClients` | `qa_application_saved_clients` (internal header) | done |
| 225-231 | `PreparedSourceCommands` | `qa_application_prepared_source_commands` (internal header) | done |
| 232 | `ApplicationCommandRequest` | `qa_application_command_request` (internal header) | done |
| 233-234 | `Q3SeatClient` | `qa_application_q3_seat_client` (internal header) | done |
| 235 | `NativeQ2SeatClient` | `qa_application_native_q2_seat_client` (internal header) | done |
| 237-250 | `GraphicalApplication` | `qa_application_graphical` (internal header) | done |
| 252-262 | `ApplicationHost` | `qa_application_host` (application_class.h) | done |
| 264-268 | `LocalQ3GuestSeat`, `LocalQ3GuestBrowser`, `LocalQ3GuestWorld` | `qa_application_local_q3_guest_*` (internal header) | done |
| 270 | `performanceNow` | `qa_application_performance_now` | done |
| 271 | `windowMutator` | `qa_application_window_mutator` | done (application_seat_ui.c; ctx is the NativeRenderer, the ApplicationSeatUi mutate_window shape) |
| 274 | `DEDICATED_FAULT_LIMIT`, `DEDICATED_FAULT_WINDOW_MILLISECONDS` | `QA_APPLICATION_DEDICATED_FAULT_*` | done |
| 279-291 | `WorldReplacementRequest` | `qa_application_world_replacement_request` (internal header) | done |
| 294-305 | `PreviousWorld` | `qa_application_previous_world` (application_transition.c) | done |
| 308-337 | `WorldPlan` | the plan fields of `qa_application_world_transition` (application_transition.c) | done |
| 345-407 | `StageSlot`, `WorldTransition` (fields, constructor, `before`, `end`) | `SLOT`/`slot_read`, `qa_application_world_transition`, `tx_before`, `world_transition_end` (application_transition.c) | done |
| world-replacement.ts | `WorldReplacement` (`takeScripts`, `rollback`) | `qa_application_world_replacement` (application_transition.c; its only consumer) | done |
| 5309 | `openApplication` | `qa_application_open_application` | done |

## Class members

| TS lines | kind | TS member | C name | C file | Group | Status |
|---|---|---|---|---|---|---|
| 409-409 | field | `releaseViewCvars` | `qa_application.release_view_cvars` | `application_internal.h` | A | done |
| 410-415 | field | `viewSettings` | `qa_application.view_settings` | `application_internal.h` | A | done |
| 416-416 | field | `graphical` | `qa_application.graphical` | `application_internal.h` | A | done |
| 417-417 | field | `capture` | `qa_application.capture` | `application_internal.h` | A | done |
| 418-418 | field | `q3MenuPause` | `qa_application.q3_menu_pause` | `application_internal.h` | A | done |
| 419-419 | field | `lobbyCompletion` | `qa_application.lobby_completion` | `application_internal.h` | A | done |
| 420-420 | field | `rankings` | `qa_application.rankings` | `application_internal.h` | A | done |
| 421-421 | field | `rankingMenuRequests` | `qa_application.ranking_menu_requests` | `application_internal.h` | A | done |
| 422-422 | field | `rankingStatuses` | `qa_application.ranking_statuses` | `application_internal.h` | A | done |
| 424-431 | method | `rankingAccountActions` | `qa_application_ranking_account_actions` | `application_core.c` | P1 | done |
| 432-432 | method | `takeRankingMenuRequest` | `qa_application_take_ranking_menu_request` | `application_core.c` | P1 | done |
| 433-439 | method | `publishRankingState` | `qa_application_publish_ranking_state` | `application_core.c` | P1 | done |
| 440-446 | method | `disconnectRankings` | `qa_application_disconnect_rankings` | `application_core.c` | P1 | done |
| 447-459 | method | `closeRankings` | `qa_application_close_rankings` | `application_core.c` | P1 | done |
| 460-472 | method | `queueRankingFrame` | `qa_application_queue_ranking_frame` | `application_core.c` | P1 | done |
| 473-522 | method | `rankingFrame` | `qa_application_ranking_frame` | `application_core.c` | P1 | done |
| 523-523 | field | `tools` | `qa_application.tools` | `application_internal.h` | A | done |
| 524-524 | field | `debugGraph` | `qa_application.debug_graph` | `application_internal.h` | A | done |
| 525-525 | field | `ownedVideoRestart` | `qa_application.owned_video_restart` | `application_internal.h` | A | done |
| 526-526 | field | `frontendOverrides` | `qa_application.frontend_overrides` | `application_internal.h` | A | done |
| 527-527 | field | `imageSettings` | `qa_application.image_settings` | `application_internal.h` | A | done |
| 528-528 | field | `frontendBaseline` | `qa_application.frontend_baseline` | `application_internal.h` | A | done |
| 529-529 | field | `bots` | `qa_application.bots` | `application_internal.h` | A | done |
| 530-530 | field | `clientCvars` | `qa_application.client_cvars` | `application_internal.h` | A | done |
| 531-531 | field | `publishedUserinfo` | `qa_application.published_userinfo` | `application_internal.h` | A | done |
| 532-532 | field | `teamArenaOverrides` | `qa_application.team_arena_overrides` | `application_internal.h` | A | done |
| 534-540 | method | `overrideSeats` | `qa_application_override_seats` | `application_core.c` | P1 | done |
| 541-541 | field | `archivePersistence` | `qa_application.archive_persistence` | `application_internal.h` | A | done |
| 542-542 | field | `gameLog` | `qa_application.game_log` | `application_internal.h` | A | done |
| 543-543 | field | `dedicatedConsole` | `qa_application.dedicated_console` | `application_internal.h` | A | done |
| 544-544 | field | `dedicatedCommands` | `qa_application.dedicated_commands` | `application_internal.h` | A | done |
| 545-545 | field | `requestedCommands` | `qa_application.requested_commands` | `application_internal.h` | A | done |
| 546-546 | field | `requestedCursor` | `qa_application.requested_cursor` | `application_internal.h` | A | done |
| 547-547 | field | `preparedStartup` | `qa_application.prepared_startup` | `application_internal.h` | A | done |
| 548-548 | field | `keys` | `qa_application.keys` | `application_internal.h` | A | done |
| 549-549 | field | `keyProfiles` | `qa_application.key_profiles` | `application_internal.h` | A | done |
| 550-550 | field | `clientInputs` | `qa_application.client_inputs` | `application_internal.h` | A | done |
| 551-551 | field | `deferredInput` | `qa_application.deferred_input` | `application_internal.h` | A | done |
| 552-552 | field | `stopping` | `qa_application.stopping` | `application_internal.h` | A | done |
| 553-553 | field | `closed` | `qa_application.closed` | `application_internal.h` | A | done |
| 554-554 | field | `stepping` | `qa_application.stepping` | `application_internal.h` | A | done |
| 555-555 | field | `worldOperation` | `qa_application.world_operation` | `application_internal.h` | A | done |
| 556-556 | field | `worldOperations` | `qa_application.world_operations` | `application_internal.h` | A | done |
| 557-557 | getter | `worldOperationCount` | `qa_application_world_operation_count` | `application_core.c` | P1 | done |
| 558-558 | field | `worldOperationCompletion` | `qa_application.world_operation_completion` | `application_internal.h` | A | done |
| 559-559 | field | `stepCompletion` | `qa_application.step_completion` | `application_internal.h` | A | done |
| 560-560 | field | `closing` | `qa_application.closing` | `application_internal.h` | A | done |
| 561-561 | field | `ownedRecording` | `qa_application.owned_recording` | `application_internal.h` | A | done |
| 562-562 | field | `ownedDemos` | `qa_application.owned_demos` | `application_internal.h` | A | done |
| 563-563 | field | `elapsed` | `qa_application.elapsed` | `application_internal.h` | A | done |
| 564-564 | field | `frames` | `qa_application.frames` | `application_internal.h` | A | done |
| 565-565 | field | `presentationFrames` | `qa_application.presentation_frames` | `application_internal.h` | A | done |
| 566-566 | field | `presentationMilliseconds` | `qa_application.presentation_milliseconds` | `application_internal.h` | A | done |
| 567-567 | field | `sourceEvents` | `qa_application.source_events` | `application_internal.h` | A | done |
| 568-568 | field | `unhandledEffects` | `qa_application.unhandled_effects` | `application_internal.h` | A | done |
| 569-569 | field | `serverProfileStoreValue` | `qa_application.server_profile_store_value` | `application_internal.h` | A | done |
| 570-572 | getter | `serverProfileStore` | `qa_application_server_profile_store` | `application_core.c` | P1 | done |
| 573-573 | field | `inputConfig` | `qa_application.input_config` | `application_internal.h` | A | done |
| 574-574 | field | `reportedEffectGaps` | `qa_application.reported_effect_gaps` | `application_internal.h` | A | done |
| 575-575 | field | `network` | `qa_application.network` | `application_internal.h` | A | done |
| 576-576 | field | `recordingHost` | `qa_application.recording_host` | `application_internal.h` | A | done |
| 577-577 | field | `localRecording` | `qa_application.local_recording` | `application_internal.h` | A | done |
| 578-578 | field | `localRecordingActive` | `qa_application.local_recording_active` | `application_internal.h` | A | done |
| 579-579 | field | `recordingSeat` | `qa_application.recording_seat` | `application_internal.h` | A | done |
| 581-581 | field | `transitions` | `qa_application.transitions` | `application_internal.h` | A | done |
| 582-582 | field | `pendingTransition` | `qa_application.pending_transition` | `application_internal.h` | A | done |
| 583-583 | field | `movieRequests` | `qa_application.movie_requests` | `application_internal.h` | A | done |
| 584-584 | field | `movies` | `qa_application.movies` | `application_internal.h` | A | done |
| 585-585 | getter | `campaignMovie` | `qa_application_campaign_movie` | `application_core.c` | P1 | done |
| 586-588 | setter | `campaignMovie` | `qa_application_set_campaign_movie` | `application_core.c` | P1 | done |
| 589-589 | field | `pendingMap` | `qa_application.pending_map` | `application_internal.h` | A | done |
| 590-590 | field | `pendingQ3Map` | `qa_application.pending_q3_map` | `application_internal.h` | A | done |
| 591-591 | field | `pendingNativeTravel` | `qa_application.pending_native_travel` | `application_internal.h` | A | done |
| 592-592 | field | `baseArenaProgress` | `qa_application.base_arena_progress` | `application_internal.h` | A | done |
| 593-593 | field | `playerProgress` | `qa_application.player_progress` | `application_internal.h` | A | done |
| 594-594 | field | `teamArenaDemos` | `qa_application.team_arena_demos` | `application_internal.h` | A | done |
| 595-595 | field | `pendingTeamArena` | `qa_application.pending_team_arena` | `application_internal.h` | A | done |
| 596-596 | field | `pendingTeamArenaPostgame` | `qa_application.pending_team_arena_postgame` | `application_internal.h` | A | done |
| 597-597 | field | `sourceCommands` | `qa_application.source_commands` | `application_internal.h` | A | done |
| 598-598 | field | `operatorState` | `qa_application.operator_state` | `application_internal.h` | A | done |
| 599-599 | field | `operatorOutput` | `qa_application.operator_output` | `application_internal.h` | A | done |
| 600-600 | field | `operatorSend` | `qa_application.operator_send` | `application_internal.h` | A | done |
| 601-601 | field | `sourceCommandBinding` | `qa_application.source_command_binding` | `application_internal.h` | A | done |
| 602-602 | field | `releaseSourceCommands` | `qa_application.release_source_commands` | `application_internal.h` | A | done |
| 603-603 | field | `releaseModInputCommands` | `qa_application.release_mod_input_commands` | `application_internal.h` | A | done |
| 604-604 | field | `modInputCommands` | `qa_application.mod_input_commands` | `application_internal.h` | A | done |
| 605-605 | field | `q2Console` | `qa_application.q2_console` | `application_internal.h` | A | done |
| 606-606 | field | `pendingRestart` | `qa_application.pending_restart` | `application_internal.h` | A | done |
| 607-607 | field | `lastRestartFrame` | `qa_application.last_restart_frame` | `application_internal.h` | A | done |
| 608-608 | field | `localSnapshotServerBit` | `qa_application.local_snapshot_server_bit` | `application_internal.h` | A | done |
| 609-609 | field | `fatalWorldFailure` | `qa_application.fatal_world_failure` | `application_internal.h` | A | done |
| 611-611 | field | `dedicatedFrameFaults` | `qa_application.dedicated_frame_faults` | `application_internal.h` | A | done |
| 612-612 | field | `dedicatedFrameFaultCount` | `qa_application.dedicated_frame_fault_count` | `application_internal.h` | A | done |
| 613-613 | field | `roundPresentationEvents` | `qa_application.round_presentation_events` | `application_internal.h` | A | done |
| 614-614 | field | `roundCursor` | `qa_application.round_cursor` | `application_internal.h` | A | done |
| 615-615 | field | `pendingSave` | `qa_application.pending_save` | `application_internal.h` | A | done |
| 616-616 | field | `savedGames` | `qa_application.saved_games` | `application_internal.h` | A | done |
| 617-617 | field | `savedGamesCatalog` | `qa_application.saved_games_catalog` | `application_internal.h` | A | done |
| 618-618 | field | `localGuest` | `qa_application.local_guest` | `application_internal.h` | A | done |
| 619-619 | field | `guestBrowser` | `qa_application.guest_browser` | `application_internal.h` | A | done |
| 620-620 | field | `saveOperation` | `qa_application.save_operation` | `application_internal.h` | A | done |
| 621-621 | field | `lastOutput` | `qa_application.last_output` | `application_internal.h` | A | done |
| 622-622 | field | `campaignUnit` | `qa_application.campaign_unit` | `application_internal.h` | A | done |
| 623-623 | getter | `saveDirectory` | `qa_application_save_directory` | `application_core.c` | P1 | done |
| 624-624 | getter | `externalSaveDirectories` | `qa_application_external_save_directories` | `application_core.c` | P1 | done |
| 625-637 | method | `saveUnavailable` | `qa_application_save_unavailable` | `application_core.c` | P1 | done |
| 638-641 | method | `levelRecoveryAvailable` | `qa_application_level_recovery_available` | `application_core.c` | P1 | done |
| 643-653 | method | `autosaveLevel` | `qa_application_autosave_level` | `application_saves.c` | P1 | done (saveGame is defined, application_travel.c) |
| 656-658 | method | `createWorldEffects` | `qa_application_create_world_effects` | `application_core.c` | P1 | done |
| 661-663 | method | `preloadWorldEffects` | `qa_application_preload_world_effects` | `application_core.c` | P1 | done |
| 666-676 | method | `createSeatUi` | `qa_application_create_seat_ui` | `application_core.c` | P1 | done (application_seat_ui.c, app-concrete3; the seat UI asset owners read ApplicationAssets as TS) |
| 679-689 | method | `createWorldSeatPresentation` | `qa_application_create_world_seat_presentation` | `application_presentation.c` | P1 | done (app-concrete2; run by openGraphical on retail e1m1 under the offscreen GL driver, app-final) |
| 691-706 | method | `saveMenu` | `qa_application_save_menu` | `application_core.c` | P1 | done (application_seat_ui.c, app-concrete3; StartupSaves over the catalog; see "Found by app-concrete3" for the queued result) |
| 707-707 | field | `nativeWorldCount` | `qa_application.native_world_count` | `application_internal.h` | A | done |
| 708-708 | field | `configurationScripts` | `qa_application.configuration_scripts` | `application_internal.h` | A | done |
| 709-709 | field | `profileConfiguration` | `qa_application.profile_configuration` | `application_internal.h` | A | done |
| 710-710 | field | `sourcePublished` | `qa_application.source_published` | `application_internal.h` | A | done |
| 711-711 | field | `sourceSeatsPublished` | `qa_application.source_seats_published` | `application_internal.h` | A | done |
| 712-712 | field | `sourceConnections` | `qa_application.source_connections` | `application_internal.h` | A | done |
| 713-713 | field | `sourceClientChanges` | `qa_application.source_client_changes` | `application_internal.h` | A | done |
| 714-714 | field | `sourceSeatChanges` | `qa_application.source_seat_changes` | `application_internal.h` | A | done |
| 716-716 | field | `musicControls` | `qa_application.music_controls` | `application_internal.h` | A | done |
| 717-725 | constructor | `constructor` | `qa_application_construct` | `application_core.c` | A | done |
| 727-730 | static | `sourceConfig` | `qa_application_source_config` | `application_core.c` | P1 | done |
| 732-735 | static | `clientConfig` | `qa_application_client_config` | `application_core.c` | P1 | done |
| 737-742 | method | `enableStartupPersistence` | `qa_application_enable_startup_persistence` | `application_core.c` | P1 | done |
| 744-748 | method | `saveSourceArchive` | `qa_application_save_source_archive` | `application_core.c` | P1 | done |
| 750-754 | method | `saveClientArchives` | `qa_application_save_client_archives` | `application_core.c` | P1 | done |
| 756-759 | method | `sourceCvars` | `qa_application_source_cvars` | `application_core.c` | P1 | done |
| 761-769 | static | `nativeCommandContext` | `qa_application_native_command_context` | `application_core.c` | P1 | done (app-link: the producer is owned by the content, `qa_app_loaded_content_native_command_producer`) |
| 771-818 | static | `guestOptions` | `qa_application_guest_options` | `application_core.c` | P1 | done (app-link: closures kept with the content, `qa_app_loaded_content_keep`; realTime over localtime_r, performance.now over CLOCK_MONOTONIC) |
| 820-835 | method | `guestOutput` | `qa_application_guest_output` | `application_core.c` | P1 | done (C adds default_local_guest for the TS default) |
| 837-839 | static | `open` | `qa_application_open` | `application_core.c` | A | done (calls openSource) |
| 841-846 | static | `openBorrowed` | `qa_application_open_borrowed` | `application_open.c` | A | done (calls openSource, not done) |
| 849-852 | static | `operatorHost` | `qa_application_operator_host_wrap` | `application_core.c` | P1 | done |
| 858-860 | static | `hostPrint` | `qa_application_host_print` | `application_core.c` | P1 | done |
| 862-864 | static | `frontendPrint` | `qa_application_frontend_print` | `application_core.c` | P1 | done |
| 866-878 | static | `startupDemoService` | `qa_application_startup_demo_service` | `application_core.c` | P1 | done |
| 880-886 | static | `forwardStartupCommands` | `qa_application_forward_startup_commands` | `application_core.c` | P1 | done |
| 888-1191 | static | `openSource` | `qa_application_open_source` | `application_open.c` | A | done (app-link: owned, restored and borrowed branches; see "Found by app-link") |
| 1193-1210 | method | `prepareGuestBots` | `qa_application_prepare_guest_bots` | `application_bots.c` | P1 | done (app-botsnet: loadApplicationBotAssets, createApplicationBotNavigation, a "restore" Q3 server host for snapshotEntities, Q3QvmServerGame.attachBots over the guest-bots.ts port; one record per call keeps the borrowed assets, navigation and closures until destroy) |
| 1212-1233 | method | `createBots` | `qa_application_create_bots` | `application_bots.c` | P1 | done (bot asset content and navigation content views from LoadedApplicationContent, navigation simulation view in shared_simulation_access.c; the record of each service is freed by `qa_application_bots_release` / destroy) |
| 1235-1238 | getter | `frontendSettings` | `qa_application_frontend_settings` | `application_core.c` | P1 | done |
| 1239-1239 | getter | `frontendValues` | `qa_application_frontend_values` | `application_core.c` | P1 | done |
| 1240-1240 | getter | `botClients` | `qa_application_bot_clients` | `application_core.c` | P1 | done |
| 1241-1241 | getter | `frameCount` | `qa_application_frame_count` | `application_core.c` | P1 | done |
| 1242-1242 | getter | `worldFailed` | `qa_application_world_failed` | `application_core.c` | P1 | done |
| 1243-1243 | getter | `finished` | `qa_application_finished` | `application_core.c` | P1 | done |
| 1244-1244 | getter | `clientCommandsBlocked` | `qa_application_client_commands_blocked` | `application_access.c` | P1 | done |
| 1245-1245 | getter | `videoRestart` | `qa_application_video_restart` | `application_core.c` | P1 | done (owned path) |
| 1246-1246 | method | `pumpClientInput` | `qa_application_pump_client_input` | `application_core.c` | P1 | done |
| 1247-1250 | method | `advanceClientStartup` | `qa_application_advance_client_startup` | `application_access.c` | P1 | done |
| 1251-1251 | method | `flushClientCommands` | `qa_application_flush_client_commands` | `application_access.c` | P1 | done |
| 1252-1252 | getter | `options` | `qa_application_options` | `application_core.c` | P1 | done |
| 1253-1253 | getter | `content` | `qa_application_content` | `application_core.c` | P1 | done |
| 1254-1254 | getter | `simulation` | `qa_application_simulation` | `application_core.c` | P1 | done |
| 1255-1255 | getter | `timeMilliseconds` | `qa_application_time_milliseconds` | `application_core.c` | P1 | done |
| 1256-1256 | getter | `window` | `qa_application_window` | `application_core.c` | P1 | done |
| 1257-1257 | getter | `presentationEvents` | `qa_application_presentation_events` | `application_core.c` | P1 | done |
| 1258-1258 | getter | `unhandledPresentationEffects` | `qa_application_unhandled_presentation_effects` | `application_core.c` | P1 | done |
| 1259-1259 | getter | `networkAddress` | `qa_application_network_address` | `application_core.c` | P1 | done |
| 1260-1260 | getter | `networkWire` | `qa_application_network_wire` | `application_core.c` | P1 | not done (blocked: Q1/Q2/Q3/QW server networks expose no wire) |
| 1262-1265 | method | `returnToLobby` | `qa_application_return_to_lobby` | `application_core.c` | P1 | done |
| 1266-1268 | getter | `networkClients` | `qa_application_network_clients` | `application_core.c` | P1 | done |
| 1269-1269 | getter | `localPlayers` | `qa_application_local_players` | `application_core.c` | P1 | not done |
| 1271-1280 | method | `input` | `qa_application_input` | `application_core.c` | P1 | done |
| 1282-1286 | method | `resumeInput` | `qa_application_resume_input` | `application_core.c` | P1 | done |
| 1288-1300 | method | `openDedicatedConsole` | `qa_application_open_dedicated_console` | `application_core.c` | P1 | done |
| 1302-1302 | method | `sourceDialect` | `qa_application_source_dialect` | `application_commands.c` | P7 | done |
| 1303-1306 | static | `contentDialect` | `qa_application_content_dialect` | `application_commands.c` | P7 | done |
| 1308-1352 | method | `inputActions` | `qa_application_input_actions_create` | `application_commands.c` | P7 | done |
| 1354-1360 | method | `dispatchClientInputs` | `qa_application_dispatch_client_inputs` | `application_commands.c` | P7 | done (links once ApplicationQ3Client.input lands) |
| 1362-1374 | method | `bindServerSettingCommand` | `qa_application_bind_server_setting_command` | `application_commands.c` | P7 | done |
| 1375-1380 | method | `debugGraphSettings` | `qa_application_debug_graph_settings` | `application_commands.c` | P7 | done |
| 1382-1393 | method | `createDebugGraphOverlay` | `qa_application_create_debug_graph_overlay` | `application_commands.c` | P7 | done |
| 1395-1423 | method | `refreshApplicationTools` | `qa_application_refresh_application_tools` | `application_commands.c` | P7 | done (links once the ApplicationTools services constructor lands) |
| 1425-1428 | method | `bindSourceCommands` | `qa_application_bind_source_commands` | `application_commands.c` | P7 | done |
| 1430-1435 | method | `modCommand` | `qa_application_mod_command` | `application_commands.c` | P7 | done |
| 1438-1455 | method | `createSourceCommandBuffer` | `qa_application_create_source_command_buffer` | `application_commands.c` | P7 | done |
| 1457-1470 | method | `publishSourceCommands` | `qa_application_publish_source_commands` | `application_commands.c` | P7 | done |
| 1472-1679 | method | `prepareSourceCommands` | `qa_application_prepare_source_commands` | `application_commands.c` | P7 | done (links once the P3 members and qa_app_shared_sim_q2_match_simulation land) |
| 1681-1695 | method | `kickTargets` | `qa_application_kick_targets` | `application_commands.c` | P7 | done |
| 1697-1708 | method | `kickClients` | `qa_application_kick_clients` | `application_commands.c` | P7 | done |
| 1710-1723 | method | `requestRestart` | `qa_application_request_restart` | `application_commands.c` | P7 | done |
| 1725-1731 | method | `appendQ1Commands` | `qa_application_append_q1_commands` | `application_commands.c` | P7 | done |
| 1734-1750 | method | `writeGameLog` | `qa_application_write_game_log` | `application_commands.c` | P7 | done |
| 1751-1751 | method | `closeGameLog` | `qa_application_close_game_log` | `application_commands.c` | P2 | done (ported with writeGameLog) |
| 1753-1871 | method | `sourceActions` | `qa_application_source_actions` | `application_services.c` | P2 | done (events argument: callers pass `app->source_events` for the TS default) |
| 1873-1894 | method | `standaloneRecording` | `qa_application_standalone_recording` | `application_services.c` | P2 | done |
| 1896-1910 | method | `prepareMvdRecording` | `qa_application_prepare_mvd_recording` | `application_services.c` | P2 | done |
| 1912-1926 | method | `prepareServerRecording` | `qa_application_prepare_server_recording` | `application_services.c` | P2 | done |
| 1928-1954 | method | `prepareRecording` | `qa_application_prepare_recording` | `application_services.c` | P2 | done |
| 1956-1977 | method | `localRecordingSource` | `qa_application_local_recording_source` | `application_services.c` | P2 | done |
| 1979-1986 | method | `operatorHost` | `qa_application_operator_host` | `application_services.c` | P2 | done |
| 1987-2002 | method | `executeAdministration` | `qa_application_execute_administration` | `application_services.c` | P2 | done (TS commands(undefined, undefined, false) is `qa_application_commands_routed(app, false)`, P6) |
| 2004-2041 | method | `networkHost` | `qa_application_network_host` | `application_services.c` | P2 | done |
| 2043-2047 | method | `validateNetworkHost` | `qa_application_validate_network_host` | `application_services.c` | P2 | done |
| 2049-2065 | method | `changeNetworkWorld` | `qa_application_change_network_world` | `application_services.c` | P2 | done |
| 2067-2099 | method | `openNetwork` | `qa_application_open_network` | `application_services.c` | P2 | done |
| 2102-2114 | method | `prepareNativeQ2Seat` | `qa_application_prepare_native_q2_seat` | `application_services.c` | P2 | done (application_graphical.c, app-concrete3; fills descriptor_frame/descriptor_ctx) |
| 2116-2265 | method | `openGraphical` | `qa_application_open_graphical` | `application_open_graphical.c` | P2 | done (app-final: every branch, owned and borrowed renderer, restored and new players, the TS catch cleanup; see "Found by app-final") |
| 2267-2267 | getter | `captureMap` | `qa_application_capture_map` | `application_services.c` | P2 | done |
| 2269-2286 | method | `prepareVideoRestart` | `qa_application_prepare_video_restart` | `application_presentation.c` | P2 | done (app-concrete2, over the real ApplicationQ3Client) |
| 2288-2292 | method | `prepareRetirement` | `qa_application_prepare_retirement` | `application_services.c` | P2 | done |
| 2294-2300 | method | `readClientResource` | `qa_application_read_client_resource` | `application_services.c` | P2 | done |
| 2302-2302 | method | `releaseSettings` | `qa_application_release_settings` | `application_services.c` | P2 | done |
| 2303-2306 | method | `adoptImageSettings` | `qa_application_adopt_image_settings` | `application_services.c` | P2 | done |
| 2308-2308 | method | `retire` | `qa_application_retire` | `application_services.c` | P2 | done |
| 2310-2367 | method | `publishBorrowed` | `qa_application_publish_borrowed` | `application_open_graphical.c` | P3 | done (app-final: `client.source.current = this` is the application's `qa_app_client_source_lifetime` row; the finally block collects every retirement failure into the AggregateError) |
| 2369-2375 | static | `startupScriptReader` | `qa_application_startup_script_reader` | `application_seats.c` | P3 | done (C-only `qa_application_startup_script_reader_free`) |
| 2377-2385 | method | `openGuestBrowser` | `qa_application_open_guest_browser` | `application_guest_browser.c` | app-browser | done (StartupServerBrowser.open(inputConfig) + Q3BrowserView over `qa_app_startup_server_browser_q3_host_create`; the host is closed when the view cannot be built; `qa_application_release_guest_browser` frees the record after close/rollback) |
| 2387-2391 | method | `queueLocalGuestCommand` | `qa_application_queue_local_guest_command` | `application_seats.c` | P3 | done |
| 2393-2409 | method | `prepareLocalGuestSeat` | `qa_application_prepare_local_guest_seat` | `application_seats.c` | P3 | done (app-link) |
| 2411-2413 | method | `loadClientArchive` | `qa_application_load_client_archive` | `application_seats.c` | P3 | done |
| 2415-2473 | method | `createClientCvars` | `qa_application_create_client_cvars` | `application_seats.c` | P3 | done (app-link; created registries are seat records released by destroy) |
| 2475-2478 | method | `isTeamArenaSkirmish` | `qa_application_is_team_arena_skirmish` | `application_seats.c` | P3 | done |
| 2480-2490 | method | `uiSourceOptions` | `qa_application_ui_source_options` | `application_seats.c` | P3 | done (application_seat_ui.c, app-concrete3) |
| 2492-2510 | method | `recordPlayerProgress` | `qa_application_record_player_progress` | `application_seats.c` | P3 | done |
| 2512-2529 | method | `prepareBaseArenaProgress` | `qa_application_prepare_base_arena_progress` | `application_seats.c` | P3 | done |
| 2531-2560 | method | `completeBaseArena` | `qa_application_complete_base_arena` | `application_seats.c` | P3 | done |
| 2562-2615 | method | `baseArenaMenu` | `qa_application_base_arena_menu` | `application_seats.c` | P3 | done (returns false for TS undefined; the service record lives until destroy) |
| 2617-2640 | method | `teamArenaResults` | `qa_application_team_arena_results` | `application_seats.c` | P3 | done (returns false for TS undefined; the service record lives until destroy) |
| 2642-2647 | method | `startTeamArenaSkirmish` | `qa_application_start_team_arena_skirmish` | `application_seats.c` | P3 | done (app-link) |
| 2649-2663 | method | `prepareRestoredGuestSeat` | `qa_application_prepare_restored_guest_seat` | `application_seats.c` | P3 | done (app-link; the guest WireUserCommand is converted to the int32 history record) |
| 2665-2679 | method | `restoreSourceInputAngles` | `qa_application_restore_source_input_angles` | `application_seats.c` | P3 | done (app-link) |
| 2681-2692 | method | `publishLocalGuestSnapshots` | `qa_application_publish_local_guest_snapshots` | `application_seats.c` | P3 | done (app-link; `_with(local, simulation)` and the default form) |
| 2694-2703 | method | `prepareKeys` | `qa_application_prepare_keys` | `application_seats.c` | P3 | done |
| 2705-2708 | method | `publishKeys` | `qa_application_publish_keys` | `application_seats.c` | P3 | done (keys.c no longer destroys a profile republished as current) |
| 2710-2778 | method | `createQ3SeatClient` | `qa_application_create_q3_seat_client` | `application_graphical.c` | P3 | done (app-final: QVM branch over `qa_app_local_q3_client_state_source` / `_connection`, native branch over the shared round-binding closures) |
| 2780-2782 | method | `requestRestoredScores` | `qa_application_request_restored_scores` | `application_seats.c` | P3 | done (app-link) |
| 2784-2784 | method | `requestQuit` | `qa_application_request_quit` | `application_core.c` | P3 | done |
| 2786-2789 | method | `queueCommand` | `qa_application_queue_command` | `application_core.c` | P3 | done |
| 2791-2824 | method | `prepareSavedClients` | `qa_application_prepare_saved_clients` | `application_seats.c` | P3 | done (app-link; records kept as seat records) |
| 2826-2846 | method | `replaceWorld` | `qa_application_replace_world` | `application_transition.c` | P3 | done (TS 4544-4587; ported with P4 in application_transition.c: `qa_application_replace_world_with` takes every TS parameter, `qa_application_replace_world(app, map, carry)` passes the defaults) |
| 2848-2882 | method | `prepareAndReplaceWorld` | `qa_application_prepare_and_replace_world` | `application_transition.c` | P4 | done (TS 4589-4658; staged rollback through WorldReplacement; links once the P1-P3 members and lower-layer seams it calls land (see "Found by P4/P5")) |
| 2885-2950 | method | `planWorldReplacement` | `qa_application_plan_world_replacement` | `application_transition.c` | P4 | done (TS 4660-4799) |
| 2953-2995 | method | `initializeWorldTransition` | `qa_application_initialize_world_transition` | `application_transition.c` | P4 | done (TS 4801-4849; candidate guest output, carried guest commands) |
| 2998-3015 | method | `stageCandidateInputs` | `qa_application_stage_candidate_inputs` | `application_transition.c` | P4 | done (TS 4851-4885) |
| 3022-3048 | method | `createCandidateSinks` | `qa_application_create_candidate_sinks` | `application_transition.c` | P4 | done (TS 4887-4916; ModCommands over the candidate/published buffer) |
| 3050-3064 | method | `stageCandidateCommands` | `qa_application_stage_candidate_commands` | `application_transition.c` | P4 | done (TS 4918-4938) |
| 3067-3086 | method | `stageNativeMapTravel` | `qa_application_stage_native_map_travel` | `application_transition.c` | P4 | done (TS 4940-4978) |
| 3089-3107 | method | `stageGuestMapShutdown` | `qa_application_stage_guest_map_shutdown` | `application_transition.c` | P4 | done (TS 4980-5013) |
| 3111-3123 | method | `simulationRequest` | `qa_application_simulation_request` | `application_transition.c` | P4 | done (TS 5015-5073) |
| 3125-3145 | method | `stageSimulation` | `qa_application_stage_simulation` | `application_transition.c` | P4 | done (TS 5075-5112) |
| 3148-3186 | method | `stageClientState` | `qa_application_stage_client_state` | `application_transition.c` | P4 | done (TS 5114-5185) |
| 3189-3218 | method | `stageNetworkHost` | `qa_application_stage_network_host` | `application_transition.c` | P4 | done (TS 5187-5238) |
| 3221-3229 | method | `stageSourceCommands` | `qa_application_stage_source_commands` | `application_transition.c` | P4 | done (TS 5240-5252) |
| 3232-3287 | method | `stageGuestContinuation` | `qa_application_stage_guest_continuation` | `application_transition.c` | P4 | done (TS 5254-5363) |
| 3290-3307 | method | `stageBots` | `qa_application_stage_bots` | `application_transition.c` | P4 | done (TS 5365-5393; createBots takes the candidate buffer for the TS lazy guestCommands().insert) |
| 3310-3388 | method | `stageGraphical` | `qa_application_stage_graphical` | `application_transition.c` | P4 | done (TS 5395-5595; host-shaped lower layers reached through declared seams, see "Found by P4/P5") |
| 3391-3394 | method | `createWorldAudio` | `qa_application_create_world_audio` | `application_transition.c` | P4 | done (TS 5597-5611) |
| 3397-3407 | method | `createPresentationServices` | `qa_application_create_presentation_services` | `application_transition.c` | P4 | done (TS 5613-5650) |
| 3410-3412 | method | `stagedClock` | `qa_application_staged_clock` | `application_transition.c` | P4 | done (TS 5652-5654) |
| 3414-3414 | field | `presentationClock` | `qa_application.presentation_clock` | `application_internal.h` | A | done |
| 3416-3418 | method | `createApplicationCapture` | `qa_application_create_application_capture` | `application_transition.c` | P4 | done (TS 5658-5663; capture.ts inputCaptureServices is the qa_app_application_capture_open seam) |
| 3420-3429 | method | `createVideoRestart` | `qa_application_create_video_restart` | `application_transition.c` | P4 | done (TS 5665-5686) |
| 3431-3431 | method | `hostPrinter` | `qa_application_host_printer` | `application_transition.c` | P4 | done (TS 5688-5690) |
| 3434-3447 | method | `stageCapture` | `qa_application_stage_capture` | `application_transition.c` | P4 | done (TS 5693-5707) |
| 3450-3456 | method | `createCandidateCapture` | `qa_application_create_candidate_capture` | `application_transition.c` | P4 | done (TS 5709-5723) |
| 3459-3468 | method | `stageReplacementSet` | `qa_application_stage_replacement_set` | `application_transition.c` | P4 | done (TS 5725-5737; the cleanup closes and frees the presentation) |
| 3471-3495 | method | `stageHeadlessAdoption` | `qa_application_stage_headless_adoption` | `application_transition.c` | P4 | done (TS 5739-5779) |
| 3498-3546 | method | `stageCommit` | `qa_application_stage_commit` | `application_transition.c` | P4 | done (TS 5781-5845; the published world is a qa_application_simulation_view the transaction owns) |
| 3549-3608 | method | `publishWorld` | `qa_application_publish_world` | `application_transition.c` | P4 | done (TS 5847-5920) |
| 3611-3638 | method | `finishPublication` | `qa_application_finish_publication` | `application_transition.c` | P4 | done (TS 5922-5957) |
| 3641-3643 | method | `retireStep` | `qa_application_retire_step` | `application_transition.c` | P4 | done (TS 5959-5966) |
| 3646-3675 | method | `retirePreviousWorld` | `qa_application_retire_previous_world` | `application_transition.c` | P4 | done (TS 5968-6018) |
| 3678-3688 | method | `changeLevel` | `qa_application_change_level` | `application_transition.c` | P4 | done (TS 6020-6033) |
| 3690-3697 | method | `restoreComponentClients` | `qa_application_restore_component_clients` | `application_transition.c` | P4 | done (TS 6035-6046) |
| 3699-3730 | method | `saveGame` | `qa_application_save_game` | `application_travel.c` | P5 | done (TS 6048-6102; byte-identical to TS for the composition after checkpoint capture, see Tests) |
| 3732-3736 | method | `loadGame` | `qa_application_load_game` | `application_travel.c` | P5 | done (TS 6104-6108) |
| 3738-3760 | method | `restoreSavedGame` | `qa_application_restore_saved_game` | `application_travel.c` | P5 | done (TS 6110-6159; prepareApplicationSave is the qa_app_prepare_application_save_native seam: original_save.c is host-shaped) |
| 3762-3892 | method | `restartSourceRound` | `qa_application_restart_source_round` | `application_travel.c` | P5 | done (TS 6161-6327; prediction source and Q3 setting names are seams) |
| 3894-3898 | method | `captureBlocksTransition` | `qa_application_capture_blocks_transition` | `application_travel.c` | P5 | done (TS 6329-6340) |
| 3900-3995 | method | `applyTransition` | `qa_application_apply_transition` | `application_travel.c` | P5 | done (TS 6342-6483) |
| 3998-4006 | method | `travelOrRestart` | `qa_application_travel_or_restart` | `application_travel.c` | P5 | done (TS 6485-6494) |
| 4008-4018 | method | `reportCampaignTravelError` | `qa_application_report_campaign_travel_error` | `application_travel.c` | P5 | done (TS 6496-6506) |
| 4020-4068 | method | `advanceQ2Travel` | `qa_application_advance_q2_travel` | `application_travel.c` | P5 | done (TS 6508-6595; CampaignCinematic.prepare is the qa_app_campaign_cinematic_prepare_content seam: campaign_cinematic.c is host-shaped) |
| 4070-4077 | method | `activateMovie` | `qa_application_activate_movie` | `application_travel.c` | P5 | done (TS 6597-6608) |
| 4079-4084 | method | `reserveMovie` | `qa_application_reserve_movie` | `application_travel.c` | P5 | done (TS 6610-6619) |
| 4086-4100 | method | `screenCaptions` | `qa_application_screen_captions` | `application_travel.c` | P5 | done (TS 6621-6645) |
| 4102-4181 | method | `systemCinematics` | `qa_application_system_cinematics` | `application_travel.c` | P5 | done (TS 6647-6799) |
| 4183-4191 | method | `underwater` | `qa_application_underwater` | `application_travel.c` | P5 | done (TS 6801-6819) |
| 4193-4197 | method | `localViewport` | `qa_application_local_viewport` | `application_travel.c` | P5 | done (TS 6821-6825) |
| 4199-4239 | method | `retireRemovedLocalPlayers` | `qa_application_retire_removed_local_players` | `application_travel.c` | P5 | done (TS 6827-6892) |
| 4241-4354 | method | `changeLocalPlayers` | `qa_application_change_local_players` | `application_dispatch.c` | P6 | done (TS invokingSeat null is `qa_application_change_local_players_seat(..., NULL)`; links once the P1 createSeatUi / createWorldSeatPresentation, P2 prepareNativeQ2Seat and P3 createClientCvars / prepareLocalGuestSeat / createQ3SeatClient / restoreSourceInputAngles definitions land, with the P4-declared `qa_app_load_q3_character_for_content`) |
| 4356-4360 | method | `commandActor` | `qa_application_command_actor` | `application_dispatch.c` | P6 | done (seat NULL is TS null) |
| 4362-4375 | method | `afterCommandDispatch` | `qa_application_after_command_dispatch` | `application_dispatch.c` | P6 | done |
| 4377-4383 | method | `executeApplicationRequest` | `qa_application_execute_application_request` | `application_dispatch.c` | P6 | done (public, application_class.h; the request is copied) |
| 4385-4385 | getter | `pendingShellPublication` | `qa_application_pending_shell_publication` | `application_dispatch.c` | P6 | done (reads ClientBootstrap.hasPendingSource, added to `qa_app_client_bootstrap`) |
| 4387-4390 | method | `takePendingClientCommands` | `qa_application_take_pending_client_commands` | `application_dispatch.c` | P6 | done (public: `qa_application_pending_client_commands` with `_run` / `_discard` for the TS closure) |
| 4392-4398 | method | `nativeMatchTransition` | `qa_application_native_match_transition` | `application_dispatch.c` | P6 | done (static `native_match_transition`) |
| 4401-4417 | method | `rankLocalMatch` | `qa_application_rank_local_match` | `application_dispatch.c` | P6 | done (over `qa_app_match_rankings`, match_results.c; TS String(error) prints `<name>: <message>`) |
| 4419-4435 | method | `finishQ2Match` | `qa_application_finish_q2_match` | `application_dispatch.c` | P6 | done (transition NULL is TS undefined) |
| 4437-4492 | method | `commands` | `qa_application_commands` | `application_dispatch.c` | P6 | done (`qa_application_commands` = commands(), `qa_application_commands_routed`, executeApplicationRequest and takePendingClientCommands pass the direct list; ClientBootstrap routeCommand / dispatchApplicationRequest added to `qa_app_client_bootstrap`) |
| 4495-4608 | method | `applicationCommand` | `qa_application_application_command` | `application_dispatch.c` | P6 | done (static `application_command`; cinematic links once P5 systemCinematics lands) |
| 4611-4698 | method | `guestCommand` | `qa_application_guest_command` | `application_dispatch.c` | P6 | done (static `guest_command`) |
| 4701-4813 | method | `sourceCommand` | `qa_application_source_command` | `application_dispatch.c` | P6 | done (static `source_command`; save links once P5 saveGame lands) |
| 4815-4930 | method | `presentOutput` | `qa_application_present_output` | `application_dispatch.c` | P6 | done (app-mods, TS 7742-7917: every branch, over the TS-shaped NativeQ2ClientPresentation and `qa_app_component_media_host_of` (component_media_host.c); the frame's event lists are views, drained prints and native client events are owned copies released in the TS `finally`; checked at unit level for the dedicated path only, the graphical branches need a graphical owner) |
| 4932-5172 | method | `step` | `qa_application_step` | `application_step.c` | A | done (graphical branches read qa_application_seat_presentation; presentation.prepare runs the host-shaped C prepare) |
| 5174-5182 | method | `run` | `qa_application_run` | `application_step.c` | A | done |
| 5185-5195 | method | `recoverDedicatedFrame` | `qa_application_recover_dedicated_frame` | `application_step.c` | A | done |
| 5197-5200 | method | `readPixels` | `qa_application_read_pixels` | `application_step.c` | A | done |
| 5202-5205 | method | `captureNextFrame` | `qa_application_capture_next_frame` | `application_step.c` | A | not done (renderer capture request seam) |
| 5207-5212 | method | `close` | `qa_application_close` | `application_close.c` | A | done |
| 5213-5220 | method | `closeFailedWorld` | `qa_application_close_failed_world` | `application_close.c` | A | done |
| 5221-5306 | method | `closeOwned` | `qa_application_close_owned` | `application_close.c` | A | done (owned path; the borrowed-client branches need the ClientBootstrap members) |

## Dependency gaps (found by the core pass, 2026-11-03; updated by P2, 2026-11-03)

These block members of this file; each is owned by another module's port, not by application.ts.

- **openSource (888-1191)** is not ported. State of its lower-layer calls:
  - `openInitialConfigurationContent(options, recipe)`: concrete as `qa_app_open_initial_configuration_content_native`
    (content.c; discovers the catalog, prepares the Q3 product, opens the recipe or configuration-preset content).
  - `createStartupSource` / `prepareInitialConfiguration` / `prepareProfileConfiguration`: the startup-source host bundle
    is gone; `qa_app_create_startup_source` registers the Q3 product policy, QuakeWorld engine, Q3 server and frame-time
    cvars directly, as TS does.
  - Resolved (app-deps, 2026-11-03): `ApplicationImageSettings` (image_settings.c) is concrete: open registers the
    shared client settings (shared_setting_cvars.c, now over the ported audio output and music settings), accessibility
    and Q1 client settings, executes settings/images.cfg and records persistedEntries, applies the render-worker and
    display overrides; prepareClientSettings (fov validation and the cvar transfer), bindViewSettings, refreshDisplay,
    refresh(assets, presentations, rerelease, renderer), save and close follow TS. `qa_app_prepare_initial_configuration`
    no longer takes an image host and puts persistedEntries first in the shared archive.
  - `LoadedApplicationContent`: `preparedQuakeC`, `preparedQ3Game`, `preparedQ2Game` are prepared by
    `qa_app_load_application_content_native` (another porter, 2026-11-03). Resolved (app-deps): `preparedWeaponBehaviors`
    (`qa_app_prepare_application_weapon_behavior`, filling the qvm, rerelease-native and quakec payload records) and
    `preparedQvmGrapple` (`qa_app_prepare_application_qvm_grapple`) are concrete over forContent, as TS takes them.
    Resolved (app-mods): `preparedMods`. `qa_app_loaded_content_prepare_mods` runs the TS prepareApplicationMods
    (`qa_app_prepare_application_mods`, mod_selection.c) over forContent and the effective catalog, and
    `qa_app_loaded_content_prepared_mods` is the SimulationOptions.preparedMods list (never NULL, as TS modOwners = []);
    openSource and the transaction pass it. JSON.stringify(declaration) is `qa_gameplay_mod_declaration_json`
    (content/mods/declaration_writer.c). Resolved (app-p6): the QVM mod preparer
    (simulation/qvm_mod.c) follows TS: `qa_app_sim_prepare_mounted_qvm_mod(description, declaration, digest, mounts)` and
    initialize() construct the QvmModProvider over the destination services (borrowed mod file mounts, command binding,
    registerModCallbacks, client presentation from the declared HUD); the `create_runtime` / validate hosts are gone.
    SimulationWeaponBehaviors constructs its QuakeC sources itself as TS does (weapon_behavior_runtime.c); the
    `create_quakec` host member, which SharedSimulation never set, is removed.
  - Resolved (app-deps): `resolveApplicationMovement` is `qa_app_resolve_application_movement` (content.c),
    `authoredCampaignStart` is `qa_app_authored_campaign_start_for` (authored_start.c, already ported),
    `sourceConsoleScripts` is `qa_app_source_console_scripts` (configuration.c; the files retain the content's main
    mounts until close), `serviceLoading` is `qa_app_service_loading` (loading.c, now raise-based: a service failure
    rejects the operation's next frame and wins after the operation joins).
  - Members of other groups openSource calls that have no definition yet: P1 `prepareGuestBots`, `createBots`,
    `autosaveLevel`; P3 `publishKeys`, `publishBorrowed`, `isTeamArenaSkirmish`, `requestRestoredScores`,
    `startTeamArenaSkirmish`; P5 `advanceQ2Travel`. (P6 `commands`, `commands_routed` and `changeLocalPlayers` are defined, app-p6.)
- **StartupConfig.executeFrame** (startup_config.c) queued `exec <script>` with the buffer's plain append, where TS
  configuration.ts hands it an `appendPreparation(text, context, dialect)` adapter; prepared profiles never ran their
  scripts. `qa_app_startup_config_execute_frame` now takes the TS append (`qa_app_startup_config_append`; NULL fn is the
  plain append PreparedStartup uses), and configuration.c passes the preparation adapter over `currentCommands()`.
  test_remote_application goes from 7 failures to 1; the remaining print-transcript failure passes with the concrete
  ApplicationImageSettings (its fixture now opens the concrete settings; expectations unchanged).
- **ClientBootstrap**: `qa_app_client_bootstrap` gained `keys`, `recording`, `stop_recording`, `video_restart`,
  `music_controls`, `capture`, `identity`, `session`, `image_settings`, `source_current` and `source_profile` (P2). Still
  missing: `renderer`, `settings`, `output`, `localLobby`, `captionCommands`, `activateFrontend`; `prepared` is the C
  PreparedStartup. P6 added `has_pending_source`, `route_command` and `dispatch_application_request` (ctx
  `bootstrap_ctx`), which the ClientBootstrap implementations (startup.ts, remote-application.ts) must fill.
- Resolved (app-deps): **trustedNetworksOnly** (`--lan-only`) is `qa_app_options.trusted_networks_only`, passed by
  openNetwork to the Q1 and Q2 server network options; Q1 polling and the Q2 KEX LAN host drop non-private datagrams
  (`qa_net_is_private_network_address`, network/common/endpoint.c). **QW administration** `rconPassword` is a getter
  read on each poll. **Source administration** has `qa_app_source_server_administration_destroy`.
- **WorldSeatPresentation** (presentation.c) is host-shaped: it exposes neither `local`, `ui`, `q3Client` nor
  `prepare(snapshot, presentations, characters)`. The application keeps `local`, `ui` and `q3Client` beside each
  presentation (`qa_application_seat_presentation`); step's movie branch calls the host-shaped prepare.
- **ApplicationQ3Client.userCommandSelection**: defined (`qa_app_application_q3_client_user_command_selection`,
  q3_client.c, app-q3client 2026-11-03).
- Resolved (app-p6): **q2MatchCompleted(simulation)**: `qa_app_shared_sim_q2_match_simulation` (shared_simulation_access.c,
  declared in shared_simulation_internal.h) builds the `qa_app_q2_match_simulation` view from the SharedSimulation:
  options.mode, recipe map path, q2Source players intermission and states, q2Native module / edition / player states,
  q2NativePlayers, playerClient, q2ServerCvars and randomUUID.
- Resolved (app-p6): **match-results.ts** is ported (include/qa/app/bootstrap/match_results.h, match_results.c:
  matchResultOf, userinfoIdentity, ApplicationMatchRankings over LocalAuthorization / LocalRankingService / EventRing).
  player-progress.ts drift ported with it: "ranked-match" events (`won`, `rules`), file version 2, `all()`, and the
  lenient open (torn or invalid documents open empty); list()/all() views stay valid across records.
  tests/app/test_match_results.c matches tests/app/oracles/match_results.ts on a retail Q1 dm1 deathmatch.
- Resolved (app-deps): **ImageSettings.refresh(assets, presentations, rerelease, renderer)** takes the TS arguments;
  application_step.c passes the graphical owner's. The rerelease presentation's prepareImageRefresh still loads skies
  through its host instead of the prepared images (rerelease_presentation.c; `qa_app_rerelease_refresh_discard` added).
- **prepareVideoGuests** (video-restart.ts, video_restart.c): ported over the real ApplicationQ3Client
  (`qa_app_prepare_video_guests(input, renderer, seats, count, assert_current, ctx, out)`; seats carry the client, a viewport
  closure over the window and the publication), through `qa_app_application_q3_client_capture_video_reopen` /
  `qa_app_q3_video_reopen_open` (app-q3c2, 2026-11-03). PreparedVideoPresentation's `restart` raises (no error strings) and
  carries a C `release`. Still open for P2: `prepareVideoRestart` (application.ts) needs an ApplicationQ3Client adapter to
  `qa_app_seat_q3_client` for `presentation.replaceQ3Client`, which no C code builds yet. The VideoRestartHost keeps
  `published` returning an error message (or NULL) and `failed(ctx, message)`, the shapes application_transition.c builds.
- **TS AggregateError** keeps its inner errors; the C raise keeps the TS message and the error count in `detail`.

Added for the core (additive): `qa_user_data_directory` (content/user_data.c, TS userDataDirectory),
`qa_app_minimum_frame_milliseconds` (frame_time.c, TS minimumFrameMilliseconds), and frame_loop.c/.h (TS
frame-loop.ts runFrameLoop).

### Found by P1/P7 (2026-11-03, app-p17)

- **ApplicationTools services form** (application-tools.ts): refreshApplicationTools builds `new ApplicationTools(commands,
  services)`. application_tools.c is host-shaped (camera and diagnostics bound by the host), and diagnostic_tools.c takes a
  host-shaped `qa_app_runtime_mounts` where TS takes MountedContent. `qa_app_application_tools_open` and
  `qa_app_application_tool_services` are declared (application_tools.h, additive) and undefined.
- **ApplicationQ3Client** `input(event)` and `resourceDiagnostics()`: defined (q3_client.c, app-q3client
  2026-11-03). `ApplicationQ3Client.create` is `qa_app_application_q3_client_create(options, artifacts)` over
  `qa_app_application_q3_client_options` (kind local/remote/qvm); `options.commands.reliable` is
  `qa_app_application_q3_client_reliable`; captureVideoReopen is `qa_app_application_q3_client_capture_video_reopen`
  + `qa_app_q3_video_reopen_open`. The scene renderer and cinematics run on the ApplicationAssets behind the
  ApplicationQ3Assets (app-q3c2): the options no longer carry `scene_renderer` / `cinematics_host`. openGuestBrowser: `qa_app_startup_server_browser_q3_host_create` gives the
  Q3BrowserHost for Q3BrowserView.
- **qa_app_shared_sim_q2_match_simulation** (already listed): prepareSourceCommands calls prepareQ2MatchReports through it.
- **seat-factory.ts** (createSeatUi / createSeatPresentation, 65 lines) has no C port; WorldSeatPresentation (presentation.c)
  and ApplicationSeatUi take host-shaped options. Blocks createSeatUi and createWorldSeatPresentation.
- **StartupSaves** (startup_saves.c) delegates .pak.gz / .sav inspection to a host, where TS inspects with the catalog.
  Blocks saveMenu.
- **Bots** (P3): the views are built now: `qa_application_bot_asset_content` / `qa_application_navigation_content`
  (application_bots.c, over LoadedApplicationContent) and `qa_app_shared_sim_navigation_simulation` (shared_simulation_access.c,
  including the guestMovementProjection host over Q3QvmServerGame); `loadMountedBotAssetFiles` is ported
  (`qa_load_mounted_bot_asset_files`, bots/behavior/assets.c). createBots is done. prepareGuestBots stays blocked on the
  host-shaped BotLibrary of guest_bots.c (see its row); `createQ3ApplicationServerHost` exists
  (`qa_app_sim_create_q3_application_server_host`).
- **networkWire**: the Q1, Q2, Q3 and QW server networks expose no `wire` (only unified does). Blocks networkWire.
- **ApplicationRankings account actions** (fixed by P3): a retained `rankingAccountActions` record is marked closed when
  the owner's lifecycle is destroyed (the application's Q3RankingHost `destroy_lifecycle`), keeps the ended lifecycle's
  service and player answers, and raises "Ranking match owner is closed" from submit/reset/spectate as TS assertOpen does.
  Still open in q3_rankings.c: `qa_app_q3_rankings_account_actions` keeps its ctx in a static 64-entry table shared by
  every owner.
- **Request source contexts** (fixed by P3): core/commands gained `qa_command_context_copy` / `qa_command_context_release`
  (deep copy of origin names, the script caller chain and the producer record with its strings), and the command buffer's
  stored contexts now own their producer record too. Every request the application queues with a source owns a copy, and
  `qa_application_command_request_free` releases it (test_application_core `test_request_context_lifetime`, ASan).

Ported for P1/P7 (lower layers, additive): `sourceScriptReader` (config_scripts.c, `qa_app_source_script_reader_create` /
`_destroy`); `readFrontendInput` / `readFrontendPreferences` (frontend_preferences.c; `acceleration` is a number in TS, so
`qa_app_frontend_preference_values.acceleration` and `qa_app_frontend_mouse.acceleration` are now double, every user in
frontend_preferences.c); `qa_app_q3_source_subsystems_owner_server_commands` / `_spawns` (q3/runtime.c) and
`qa_app_q3_qvm_server_game_engine_active` (q3/guest_runtime.c), the TS `serverCommands`, `spawns` and `engine.active` members.

### P6 (2026-11-03, app-p6)

The TS region moved: application.ts is 8,585 lines now and P6 is TS 6894-7917 (changeLocalPlayers 6894, commandActor 7092,
afterCommandDispatch 7098, executeApplicationRequest 7113, pendingShellPublication 7121, takePendingClientCommands 7125,
nativeMatchTransition 7131, rankLocalMatch 7147, finishQ2Match 7173, commands 7192, applicationCommand 7283, guestCommand
7450, sourceCommand 7570, presentOutput 7742); the table keeps the generated line numbers.

- `application_dispatch.c` holds every P6 member. A request the dispatch consumes is freed after it
  runs (TS GC); a prepend hands the remaining records back to `requestedCommands` with their storage.
- Checks: test_application_core `test_p6_commands` against oracle `p6.*` lines (graphics-free dispatch through the three
  stages: caught errors print their message through the host, `quit` stops, commandActor's error, pendingShellPublication).
  test_match_results covers rankLocalMatch's pieces (matchResultOf, bind/observe/settle/standings, the progress store).
- The constructor now allocates `operatorOutput` (TS field initializer `{ write: null }`); it was NULL until openSource,
  so any dispatch print before openSource dereferenced NULL. openSource must free this box when it installs the host
  wrapper's.
- Not checked at unit level: changeLocalPlayers and the graphical branches of the dispatch (they need the P1/P2/P3 seat
  members and a graphical owner).
### app-mods (2026-11-03)

- Gameplay mod declaration writer: `qa_{quakec,qvm,native}_mod_declaration_value` / `qa_gameplay_mod_declaration_value`
  rebuild the object the TS readers build (key order of each object literal and spread, optional members only when kept),
  as a qa_save_value tree; `qa_json_stringify_value` is JSON.stringify of a plain tree (-0 as 0, non-finite as null, lone
  surrogates escaped). The readers now keep an optional list that is present but empty as a non-NULL pointer
  (`qa_mod_declaration_present`), so absent and `[]` differ as in TS. Byte-identical to bun JSON.stringify for 202
  declarations (tests/app/test_mod_declarations.c, oracle tests/app/oracles/mod_declarations.ts with the capture preload
  mod_declarations_capture.ts): the 127 distinct declarations the TS compat/content/persistence tests read, 7 synthetic
  declarations reaching every optional member and union branch, and each result read again. Twelve rows (fixtures that
  put "\u0000" in names to test rejection) match only without the NUL: C strings end at NUL, so the C JSON parser cannot
  keep it. The retail corpus (~/qfiles) has no gameplay-mods.json, so no retail declaration exists to compare.
- save_image.c `gameplay_mod_value` writes the ResolvedGameplayMod: discoverGameplayMods' key order, or readGameplayMod's
  for a mod read back from a save (`qa_resolved_gameplay_mod.read_order`, set by persistence/mods.c).
- mod-selection.ts is ported to current TS (mod_selection.c, no host): applicationModChoices(catalog) opens each product's
  mounts, discovers gameplay mods and weapon-behavior choices, adds the failure rows TS adds (with sourceTitle and the
  `unavailable-weapon-behaviors+` id) and the weapon-behavior conflicts; applyApplicationMods raises the TS Error texts and
  takes the first matching choice; prepareApplicationMods(catalog, recipe, forContent, purpose) rediscovers over the
  forContent mounts, compares with the writer's JSON, checks requires/conflicts against the enabled set (recipe mods plus
  weapon-behavior selections), filters description.requires to the recipe's mods, filters presentation to QVM mods with a
  presentation, and calls prepareMounted{QuakeC,Qvm,Native}Mod. The discovery mounts stay open for the choices' life (rows
  borrow their references) where TS closes them when discovery ends. content.c's configured host now calls it.
- `qa_app_sim_prepare_mounted_quakec_mod_content` (quakec_mod.c) is prepareMountedQuakeCMod over MountedContent (owns
  the program bytes and prepareQuakeCResources); the QuakeC mod runtime gained `register` (registerModCallbacks) and the
  services.commands binding TS initialize does.
- world/session/mods.c passed the runtime a stack copy of `{ ...services, engine: { ...engine, events } }`; the copies now
  live in the mod's resource scope (a QuakeC or QVM mod read freed stack memory at close).
- native-q2-client.ts is ported over the real types (native_q2_client.c: Q2NativeWorld classic/rerelease guest world,
  Q2ServerMessageReader, q2ApplicationLayout, the Q2 service presentation host, EventRing of owned
  SimulationPresentationEvent copies); `qa_app_component_media_host_of` is defined (component_media_host.c).
- world/session/mods.c close freed the active entries before closing their scopes, whose resource is the runtime's close
  on `&active->runtime` (use after free under ASan); the scopes close first now.
- Checks: test_sim_guests drives NativeQ2ClientPresentation over the retail rerelease API2023 world (admitted-player guard,
  configstrings copy, receive over the frame's messages, print's q2-player event); test_application_core covers
  presentOutput's dedicated path (presentationFrames, presentationMilliseconds restored). All four binaries pass normally
  and under ASan/UBSan with leak detection; the only leaks are the known shared_simulation_source.c cvar snapshots.
- Retail check: a QuakeC gameplay mod over the retail id1 progs.dat in a user content root, `--mod q1-classic-id1/count`,
  through `qa_app_load_application_content_native` and a SharedSimulation over e1m1 for 10 frames; recipe JSON, prepared
  identity, enabled session mods and checkpointed modules equal bun (tests/app/oracles/mod_retail.ts).

## Tests

`tests/app/test_application_services.c` with the bun oracle `tests/app/oracles/application_services.ts` (QTS_TEST_CORPUS):
openInitialConfigurationContent and createStartupSource for Q1 e1m1, Q2 base1 and Q3 q3dm1 match TS (providers, dialect,
Q3 product, startup cvars). The application_services.c members cannot link into a test binary until the P3/P6 members
they call exist.

`tests/app/test_application_core.c` with the bun oracle `tests/app/oracles/application_core.ts` (run from
../quake-anthology-ts). Members whose files reference members still "not done" (step, close, the access getters,
open) cannot link into a test binary until those members exist; their checks belong to the group that lands the last
of them.

P1/P7 checks (same binary and oracle): contentDialect, sourceConfig/clientConfig, debugGraphSettings, prepareSourceCommands
(Q1 branch: registered names, frame-time/graph/autosave cvars and flags, queued requests, a component queueing a declared
engine command), kickTargets/requestRestart/appendQ1Commands errors and appends, guestOutput's offline guard, operatorHost
routing, openDedicatedConsole. Members of groups not yet ported that these files reference are weak test doubles in the
test file (an archive definition overrides them).

P3 checks (same binary and oracle): isTeamArenaSkirmish (no Q3 source; the missionpack/nextmap cases are oracle-only, the C
check has no constructible Q3 source), queueLocalGuestCommand routing, startupScriptReader loose/base-loose/game-loose roots
over a temp tree, the request-context lifetime fix (a request read after its script chunk is freed), and
`qa_app_shared_sim_q2_native` with NULL out-pointers.

P4/P5 checks (same binary and oracle): the TS guards of saveGame (closed, the save policy), loadGame (hosting, a stepping
application), restoreSavedGame and replaceWorld (a running world operation, a campaign cinematic), changeLevel; the
reportCampaignTravelError output and its fatal rethrow; applyTransition's no-op, campaign-complete and unknown-decision
branches; captureBlocksTransition without a capture; localViewport over seats inserted out of order; reserveMovie
tokens. The test no longer defines a saveGame double: application_saves.c links the real one, which pulls
application_travel.c and application_transition.c; the members they call that are still undefined are weak doubles
that abort if reached. No check writes a shared save: the shared path needs a constructed SharedSimulation and
Application, so byte-identical save output was not compared (see the final report of app-p45).

### Found by P3 (2026-11-03, app-p3)

- `qa_app_shared_sim_q2_native` wrote through NULL out-pointers; it now writes only those given.
- The SharedSimulation keeps a value copy of its recipe, so navigation.ts's `content.recipe !== simulation.recipe` identity
  check cannot compare pointers; createBots passes the content's recipe as the simulation's when the copy is bytewise the
  content's (`simulation_recipe` in application_bots.c).
- bot-assets.ts `loadApplicationBotAssets(content, simulation, legacyProvenance)` calls `prepareLegacyProvenance()` for
  restores; `qa_app_sim_load_application_bot_assets` has no such parameter and the C BotAssetFiles loads navigation files
  eagerly (TS `addUnread`), so a restored provenance can differ from TS. Bot-assets layer, not ported here.
- keys.c `qa_app_keys_publish` destroyed the current profile when the same profile was published again (publishKeys runs
  on every publication); it keeps it now.
- Not reached in this pass (no blocker found, context): prepareLocalGuestSeat, createClientCvars, uiSourceOptions,
  prepareRestoredGuestSeat, restoreSourceInputAngles, publishLocalGuestSnapshots, prepareSavedClients.

### Found by P4/P5 (2026-11-03, app-p45)

P4 (`application_transition.c`) and P5 (`application_travel.c`) are ported member for member against the TS tree of
2026-11-03 (application.ts reformatted to 8,585 lines; the TS line numbers in the P4/P5 rows are the current ones). The
transaction keeps the TS shape: the plan fields and every `StageSlot` are fields of `qa_application_world_transition`
(a read before the stage ran raises the TS "World transition stage output X was read before its stage ran"), the
stages run in TS order inside one `qa_try`, and a failure before commit rolls back through `WorldReplacement.rollback`
(world-replacement.ts, ported into the same file). Closures the new world keeps hold the transaction, so it and the
records the TS GC keeps alive (planned options, kept skirmish setups, restored save images, captions, cinematic handles,
the published simulation view) are owned by the application until destroy (`seat_records`).

Changes outside the two files:
- `application_close.c` `step_close_world` compared the session world's simulation ctx with the embedded
  `simulation_view`; after a replacement the session publishes the transaction's view, so it now compares the view's
  simulation with `world_simulation` (TS `session.world?.simulation === simulation`).
- `application_internal.h`: `qa_application_replace_world_with` (every TS replaceWorld parameter),
  `qa_application_world_replacement_request`, the P4/P5 member declarations, and full TS signatures for the undefined
  P1 `createSeatUi` / `createWorldSeatPresentation` (they had no callers). `shared_simulation_internal.h` declares the
  existing `qa_app_shared_sim_capture_travel`, `qa_app_shared_sim_capture_original_save` and
  `qa_app_shared_sim_checkpoint_mods_for_travel`.

Members of other groups P4/P5 call that are declared and not defined: P1 `guestOptions`, `nativeCommandContext`,
`prepareGuestBots`, `createSeatUi`, `createWorldSeatPresentation`; P2 `prepareNativeQ2Seat`, `prepareVideoRestart`; P3
`prepareSavedClients`, `createClientCvars`, `prepareLocalGuestSeat`, `prepareRestoredGuestSeat`, `openGuestBrowser`,
`createQ3SeatClient`, `restoreSourceInputAngles`, `publishLocalGuestSnapshots(local, simulation)`
(`qa_application_publish_local_guest_snapshots_with`), `startTeamArenaSkirmish`, `requestRestoredScores`.

Lower layers whose C port is host-shaped or missing; the application calls a TS-shaped seam declared in
application_internal.h ("TS-shaped members of other modules"), to be defined by that module:
- original-save.ts `prepareApplicationSave` (original_save.c takes a host for the readers, loader and simulation):
  `qa_app_prepare_application_save_native` / `qa_app_prepared_application_save_free`. Blocks restoreSavedGame.
- campaign-cinematic.ts `CampaignCinematic.prepare/restore` (campaign_cinematic.c takes a decode/playback host and a
  captions flag, not ScreenCinematicCaptions): `qa_app_campaign_cinematic_prepare_content` / `_restore_content`.
- `qa_app_shared_sim_prediction_source` (createSimulationPredictionHost's SharedSimulation view),
  `qa_app_shared_sim_presentation_access` (ApplicationInput's simulation view), `qa_app_shared_sim_scene_queries`
  (ApplicationAudio.prepareEnvironment), `qa_app_q3_source_runtime_setting_names` (settings.definitions).
- frontend-preferences.ts `applyFrontendPreferences` / `applyFrontendInput`; audio-settings.ts `saveAudioSettings(store,
  audio)` (`qa_app_save_audio_settings_from`); precache.ts `prepareApplicationResources` over assets/simulation/audio/
  effects (`qa_app_prepare_world_resources`; precache.c is host-shaped); rerelease-presentation over ApplicationAssets
  (`qa_app_rerelease_presentation_open`); loadQ3Character over `content.forContent` (`qa_app_load_q3_character_for_content`);
  capture.ts `inputCaptureServices` (`qa_app_application_capture_open`); ApplicationAudio's haptics view of the input
  (`qa_app_application_input_audio_haptics`); componentMediaControl's host (`qa_app_component_media_host_of`); the Q3
  presentation hosts over ApplicationAssets (`qa_app_application_assets_q3_presentation_hosts`); NativeRenderer
  `driver?.renderer` and its QVM display renderer (`qa_app_native_renderer_driver_name`, `_qvm_display`);
  ClientBootstrap `output.current` (`qa_app_client_bootstrap_publish_output`).
- `qa_app_resolve_application_travel` (content.c) still goes through the content host's `resolve_travel`; the concrete
  loader the plan stage uses has no host for it.
- C `loadNativeUiArt` (`qa_ui_load_native_art`) has no `{ frames, mainBackground }` options; ApplicationAudioOptions has
  no music shuffle / menu track.
- `qa_app_campaign_unit_attach` aborts (qa_range_error) on "Campaign save does not match the active map" where TS throws
  a catchable Error (autosave catches it); `qa_write_saved_game` reports I/O failure as false, so the raised message is
  the errno text, not Node's.
- The SharedSimulation options' `prepare_rerelease_navigation` is typed with the old `qa_app_sim_shared_simulation`; the
  shared simulation passes the real one cast, and the transaction's callback casts it back.
- createBots' `commands` is a buffer in C; the TS passes `{ insert: text => guestCommands().insert(text) }`, so the
  transaction hands it the candidate buffer, which publication makes the source buffer.

### Found by app-link (2026-11-03)

Goal: every application_*.o links with no unresolved symbol. Not reached; the remaining symbols are listed below. Link check:
`build/agents/app-link/probe/undef.sh` links a probe calling qa_application_open/step/close/destroy against every
`application_*.o` plus the whole archive (built with `ar qc` + `ranlib`), no ignore-all, no weak doubles.

Defined in this pass:
- Application members: openSource (owned, restored and borrowed branches, TS 1314-1820), guestOptions, nativeCommandContext,
  createClientCvars, prepareLocalGuestSeat, prepareRestoredGuestSeat, restoreSourceInputAngles, publishLocalGuestSnapshots
  (both forms), startTeamArenaSkirmish, requestRestoredScores, prepareSavedClients. C-only: `qa_application_open_free`
  (destroy releases what openSource created: simulation, content, configuration content, session, startup/profile owners,
  operator state, local guest world) and `qa_application_options_copy` (the content loader takes over its options).
- Lower-layer seams: `qa_app_q3_source_runtime_setting_names` (q3/runtime.c), `qa_app_shared_sim_prediction_source`,
  `qa_app_shared_sim_presentation_access`, `qa_app_shared_sim_scene_queries` (shared_simulation_access.c; the prediction
  source supplies relativeQ3SourceCommand and publishQ3CharacterMovementEvent, which TS imports; the presentation access
  leaves drain_presentation_events NULL because the C SharedSimulation drains event pointers), `qa_app_apply_frontend_preferences`
  / `qa_app_apply_frontend_input` (frontend_preferences.c), `qa_app_save_audio_settings_from` (audio_settings.c),
  `qa_app_application_input_audio_haptics` (input_platform.c), `qa_app_client_bootstrap_publish_output` (client_bootstrap.c;
  ClientBootstrap gained `renderer` and `output_current`), `qa_app_load_q3_character_for_content` (content.c; assets owned
  by the content), `qa_app_native_renderer_driver_name` / `qa_app_native_renderer_qvm_display` (renderer.c),
  `qa_app_resolve_application_travel` over the native catalog and mounts (content.c; the host path stays for host-built
  content), `qa_app_loaded_content_native_command_producer` and `qa_app_loaded_content_keep` (content.c).
- `qa_app_campaign_unit_attach` and `_stage` raise "Campaign save/departure does not match the active map" (qa_raise Error)
  where they aborted.

openSource notes: TS prepareInitialConfiguration builds its ConsoleScriptFiles; the C one borrows them, so openSource builds
them over the configuration content (closing them closes the content, as TS). The C saved bot connections carry the client
slot only, so the restored bot resolveClient cannot compare generations. The C `--server-profile` value arrives parsed
(options.server_profile is a qa_server_profile); `--server-profile-path` is read and parsed here.
prepareConfiguredApplicationRecipe has no separate C entry: the native content loader applies the configured weapon behavior.

Still undefined (link probe, 2026-11-03): owned by others: `qa_application_present_output` (P6),
`qa_application_prepare_video_restart` (Q3 client follow-up). Mine, not done:
`qa_application_prepare_guest_bots`, `qa_application_open_graphical`, `qa_application_create_seat_ui`,
`qa_application_create_world_seat_presentation`, `qa_application_create_q3_seat_client`, `qa_application_open_guest_browser`,
`qa_application_prepare_native_q2_seat`, `qa_application_publish_borrowed`, and the seams
`qa_app_application_assets_q3_presentation_hosts`, `qa_app_application_capture_open`, `qa_app_application_tools_open`,
`qa_app_campaign_cinematic_prepare_content`, `qa_app_campaign_cinematic_restore_content`, `qa_app_component_media_host_of`,
`qa_app_prepare_application_save_native`, `qa_app_prepared_application_save_free`, `qa_app_prepare_world_resources`,
`qa_app_rerelease_presentation_open`. Not referenced yet but unported: saveMenu, uiSourceOptions, windowMutator, networkWire.

Lower layers that block them (each is a port of its own TS module, not application.ts):
- guest-bots.ts: done by app-botsnet (see "Found by app-botsnet").
- server-browser.ts StartupServerBrowser.open: done (app-browser, see "Found by app-browser").
- native-q2-client.ts: `qa_app_native_q2_world` has no adapter over the q2Native world.
- ui.ts: `qa_app_seat_ui_asset_hosts` has no ApplicationAssets adapters.
- original-save.ts, rerelease presentation over assets, Q3 presentation hosts over assets (capture.ts,
  application-tools.ts, precache.ts and campaign-cinematic.ts are concrete now: see "Found by app-concrete").

### Found by app-concrete (2026-11-03)

Made concrete (TS shape over the real collaborators, host tables removed, callers updated):
- capture.ts: `qa_app_application_capture_services` (TS ApplicationCaptureServices), `qa_app_input_capture_services`
  (inputCaptureServices over ApplicationInput: locals, commands, binding capabilities, script writer) and
  ApplicationCapture over console/commands.c, FrameCapture and NativeRenderer.captureNextFrame. A frame-readback operation
  arms one renderer capture and runs when the renderer presents (or aborts) it; beforeWorldChange aborts armed reads
  through the new `qa_app_native_renderer_abort_capture` (the TS AbortSignal). Seam `qa_app_application_capture_open` is
  defined in capture.c (declared in capture.h; removed from application_internal.h). The application now frees owned
  captures on close, retirement and rollback (`qa_app_capture_destroy`).
- application-tools.ts: ApplicationTools over the spline camera (camera/application.c gained the TS `apply`), the
  diagnostic, runtime-diagnostic and renderer-diagnostic registrations, ConfigStore and MountedContent. The runtime
  diagnostics reach the MountedContent through `qa_app_runtime_mounts_of` (diagnostic_tools.c: path report, opened resources
  as JSON.stringify(..., null, 2) via `qa_save_resource_reference_value`, listFiles, open). The OmniTimer clock is the
  monotonic process clock (TS performance.now()), not the services clock. Close is followed by
  `qa_app_application_tools_destroy` in application_close.c.
- precache.ts: applicationResourceRequests / prepareApplicationResources take the real SharedSimulation (q1Source,
  quakecSource, q1WeaponSource, q2Source: entities, items.resourcePaths, weapons.registeredDefinitions, players' skins),
  ApplicationAudio and ApplicationEffects; nativeQ2MonsterResources takes Q2Entity and applies the rerelease medic
  reinforcements. Seam `qa_app_prepare_world_resources` is defined in precache.c (declared in precache.h).
- campaign-cinematic.ts: CampaignCinematic over the mounts, ApplicationAssets images, the ApplicationAudio engine as the
  cinematic mixer, the NativeRenderer and the media layer; prepare/restore are `qa_app_campaign_cinematic_prepare_content`
  / `_restore_content` (declared in campaign_cinematic.h, which now owns ScreenCinematicCaptions and its timeline; the
  application's `qa_application_screen_cinematic_captions` / `qa_application_cinematic_timeline` are aliases). Added the TS
  `timeline` getter.

Tests (tests/app/test_app_concrete_{capture,tools,precache,cinematic}.c, oracles tests/app/oracles/app_concrete_*.ts):
capture and cinematic reproduce bun transcripts line for line (cinematic on retail baseq2 video/eou1_.cin with
SDL_AUDIODRIVER=dummy); tools ports tests/camera/application-tools.test.ts plus the runtime diagnostics transcript;
precache ports tests/first-playable/precache.test.ts except the prepareApplicationResources case (it needs a fake
audio/effects; the C function takes the real ones).

Link probe (build/agents/app-concrete/probe/undef.sh, same probe as app-link over a private `ar qc` archive): 20 undefined
before this pass, 13 after: `qa_app_application_assets_q3_presentation_hosts`, `qa_application_create_q3_seat_client`,
`qa_application_create_seat_ui`, `qa_application_create_world_seat_presentation`, `qa_application_open_graphical`,
`qa_application_open_guest_browser`, `qa_application_prepare_guest_bots`, `qa_application_prepare_native_q2_seat`,
`qa_application_prepare_video_restart`, `qa_application_publish_borrowed`, `qa_app_prepare_application_save_native`,
`qa_app_prepared_application_save_free`, `qa_app_rerelease_presentation_open` (`qa_app_component_media_host_of` and
`qa_application_present_output` were defined concurrently by the mods/presentOutput porter).

Not reached (context): original-save.ts (blocked: SharedSimulation has no C `restoreOriginalSave`; the QuakeC source has
`qa_app_sim_quakec_source_restore_original_save`, the SharedSimulation member that updates each player's arsenal,
animation and state is missing), the rerelease presentation over assets, the Q3 presentation hosts over assets, the seat UI
asset hosts and createSeatUi, StartupSaves on the catalog and saveMenu, createWorldSeatPresentation, uiSourceOptions,
windowMutator, openGraphical, publishBorrowed, createQ3SeatClient.

### Found by app-concrete2 (2026-11-03)

- original-save.ts: SharedSimulation's restoreOriginalSave was ported (shared_simulation_players.c, every branch: the staged
  QuakeC guard, the QuakeC restore, then each player's arsenal, Q1 animation, state and syncQuakeCClientView) but declared in
  no header, which is why the previous pass saw it as missing; runtime_layout.md was right. It is now declared in
  shared_simulation_internal.h. `qa_app_prepare_application_save_native` / `qa_app_prepared_application_save_free`
  (original_save.c, declared in original_save.h; the host table is gone) run readSavedGame, readKexQ2SaveArchive /
  kexQ2SaveRestorable, selectQ1SaveProduct over the content catalog (new `qa_select_q1_save_product_in_catalog`; the
  persistence mirror catalog shares the selection code), loadApplicationContent with the catalog, createSimulation,
  admitPlayer, restoreOriginalSave and checkpoint. The image of an imported save borrows its recipe from the closed
  simulation and content the backing keeps; a shared save's image its decoded save. A missing file raises the Node
  message ("ENOENT: no such file or directory, open '<path>'").
- rerelease-presentation.ts: ApplicationRereleasePresentation runs on ApplicationAssets (`qa_app_rerelease_presentation_create(assets,
  ...)`; the `qa_app_rerelease_host` table and `qa_app_rerelease_presentation_open` are gone): per-seat NativeLanguageSettings
  catalogs over the provider's localization/loc_*.txt (languages listed and sorted as TS), q2LocalizedText, retired catalogs
  kept with the presentation (TS GC keeps a returned binding's settings), the languageBinding read/write closure,
  skies through provider.textures (cached skyImages for the assets, uncached loadSkyImages for prepareImageRefresh, which now
  takes the PreparedApplicationImages), sourceLocalizer as a localizer handle bound to the catalog current at the call
  (`qa_app_rerelease_source_localizer` / `_localizer_text`). "Unknown localization seat" and "Story font belongs to another
  seat" raise Error (they aborted). ui.c threads the assets into its native HUD localizer (the seat UI host member is gone);
  the graphical owner now destroys its rerelease presentation (graphical_release).
- Q3 presentation hosts: `qa_app_application_assets_q3_presentation_hosts` (q3_presentation_hosts.c, declared in
  mod_presentation.h) reads the assets' world fog selections live, the content world's map, and ApplicationAudio's engine
  streams and music.
- ApplicationQ3Client as WorldSeatPresentation's q3Client: `qa_app_application_q3_client_seat` (q3_seat_client.c /
  q3_seat_client.h), the one view used by createWorldSeatPresentation and by prepareVideoRestart's replaceQ3Client.
- Application members: createWorldSeatPresentation (seat-factory spec: cull factor, field of view, debug shapes, console
  scale, view size, planar shadows, tools camera, debug graph overlay, native Q2 descriptor) and prepareVideoRestart
  (VideoGuestSeat per QVM seat: viewport over the drawable size, publish replaces the client in graphical.q3, the local
  guest seat and the presentation, drops the seat's client inputs and frees the previous client) in
  application_presentation.c. NativeQ2SeatClient gained `descriptor_frame`/`descriptor_ctx` (TS descriptor.frame) for
  prepareNativeQ2Seat to fill. VideoRestartHost `published` returns void and may raise, `failed` receives the caught error
  (video_restart.h; application_transition.c and test_top_q3_client.c updated).

Tests: tests/app/test_app_concrete_original_save.c (oracle tests/app/oracles/app_concrete_original_save.ts; retail id1 e1m1,
the v5 capture bytes match TS by sha), tests/app/test_app_concrete_rerelease.c (oracle app_concrete_rerelease.ts; retail Q2
rerelease baseq2 localization, English/Russian/French), the seat view checks in tests/app/test_top_q3_client.c
(`test_seat_view`, sentinel hook ctx). createWorldSeatPresentation and prepareVideoRestart are link-checked only (no
application-level fixture with a graphical owner exists yet).

Link probe (build/agents/app-concrete2/probe/undef.sh over a private `ar qc` archive): 13 undefined before, 7 after:
`qa_application_create_q3_seat_client`, `qa_application_create_seat_ui`, `qa_application_open_graphical`,
`qa_application_open_guest_browser`, `qa_application_prepare_guest_bots`, `qa_application_prepare_native_q2_seat`,
`qa_application_publish_borrowed`.

Not reached: the seat UI asset hosts (weapon HUD assets, Q1 wheel, sound captions, native Q2 HUD, game prompt: each sibling
port is host-shaped where TS reads ApplicationAssets) and createSeatUi; StartupSaves over the catalog and saveMenu;
createQ3SeatClient, uiSourceOptions, windowMutator, openGraphical, publishBorrowed.

ASan/UBSan (build/agents/app-concrete2-asan): the three tests pass with no memory or UB error. ASan found that
`qa_app_rerelease_publish_seats` moved a seat whose sky view pointed into the old seat array (use after free); fixed. With
leaks on, the remaining reports are outside these files: the Q1 pusher candidate arrays (simulation/physics.c
q1_services_candidates, 7 KB over 30 QuakeC frames) and the known texture/material/shader registry leaks (textures.c
load_uncached, material_registrations.c, shaders.c).

### Found by app-botsnet (2026-11-03)

- compat/qvm bot-library-syscalls.ts, legacy-bot-syscalls.ts and bot-navigation-syscalls.ts now take the real
  BotLibrary (`qa_bot_library_bots_a`), SourceBotNavigation and AasBspEntities; the `qa_qvmx_bot_library` (~100 members)
  and `qa_qvmx_bot_navigation` / `qa_qvmx_aas_bsp_entities` mirror tables are gone. Services gained `started_frame`
  (TS optional `startedFrame`) and `console_message` returns an owned string. C accommodation: SourceBotNavigation's
  moveToGoal / movementViewTarget / predictVisiblePosition take goal, result and target values where TS passes live
  guest references, so the dispatcher stages them and stores back the fields the operation wrote (aliasing guest
  records observe the stores after the call). FIND_MATCH stages the 328-byte match record the same way.
- guest_bots.c is a port of guest-bots.ts (Q3GuestBots constructs BotLibrary, AasBspEntities and SourceBotNavigation;
  observations keep TS Map insertion order and the key order of the object that holds each one for checkpoints).
  guest_spatial.c gained `modelBounds`. Q3GuestBotPreparation (guest_runtime.h) is the TS shape: library options,
  ApplicationBotNavigation, createClient/freeClient/snapshotEntities. attachBots no longer caps pending bot commands
  at 64x16 arguments, bot allocation writes `s.number`, bot console messages are no longer leaked, bot input errors
  raise instead of aborting.
- Saved bot connections (simulation/save.h) carry the client generation; openSource decodes the saved bots checkpoint
  (it passed the undecoded value tree as `qa_app_sim_decoded_bots_checkpoint`) and its restored resolveClient compares
  slot and generation as TS does.
- Retail check: tests/app/test_guest_bots_verify.c against tests/app/oracles/guest_bots_verify.ts (bun) on baseq3
  qagame.qvm q3dm1 with bot_enable 1 and "addbot sarge 1".

### Found by app-concrete3 (2026-11-03)

Made concrete (TS shape over ApplicationAssets; the `qa_app_seat_ui_asset_hosts` table is gone from ui.h):
- weapon-hud.ts: `qa_app_weapon_hud_assets_create(assets)`; loadIcon runs over the assets' providers (shader pictures,
  Q1 qpic/WAD lumps registered in the assets' image registry, textures with logical aspects), `prepareImageRefresh(providers)`
  takes the PreparedApplicationImages (`qa_app_seat_ui_prepare_image_refresh(ui, providers)`; presentation.c passes its images),
  the material aspect walks the compiled stages. Errors raise Error (they raised RangeError) and a failed load caches nothing. The icon
  kept for refresh is a deep copy (the catalog result that produced it was freed).
- q1-wheel.ts: prepare(assets, icons, PlayerUi) reads the catalog expectation and wwheel.txt from the provider mounts; an
  invalid wheel and an unavailable icon go to `warnOnce` (both raised); the PlayerUi mirror is gone (ui.c had dropped the
  weapon source, so the authored wheel never loaded); label scratch is NULL-terminated (it overflowed).
- sound-captions.ts: prepare(LoadedApplicationContent) over `forContent` mounts; the stop event retires finished voices; catalogs
  are stable heap entries (voices pointed into a realloc'd array); a failed sidecar warns through the seat console.
- q2-native-hud.ts: prepare(content, ApplicationAssets, ...) over provider textures, the 512-picture cap with its warnOnce, and the
  NativeQ2HudLayoutError catch (by qa_raise name) in prepare and commands.
- game-prompt.ts: catalogs from loadLocalizationResources over the provider mounts (Map per content:language), the paged
  controls (the C menu had none), prepare(assets, focus); `qa_app_seat_game_prompt_menu` exposes the registered factory.
- startup-saves.ts: `qa_app_startup_saves_create(catalog, directory, external...)`; rows inspect with readKexQ2SaveArchive,
  readSaveHeader, readSavedGame, selectQ1SaveProduct and savedSimulationSettings, a raised error becoming the row's text (the
  host table is gone; main.c's empty host called NULL). namedPath/path raise Error; scans use lstat (Dirent semantics).
- Application: windowMutator, createSeatUi, saveMenu, uiSourceOptions (application_seat_ui.c), prepareNativeQ2Seat
  (application_graphical.c). The rerelease language binding's failed callback now receives String(error).

Found while porting: ui/saves/menu.c takes synchronous service results, so saveMenu's queued save/load/restart (run by step,
as TS) settles after the menu returned; the menu shows "Game saved." at once and a failed run is not shown there (TS awaits the
promise). ui/hud/q2_native.c raises its layout errors as RangeError (qa_range_error), so the NativeQ2HudLayoutError catch
rethrows them; the TS class is distinct. text/media_captions.c has no `warn` parameter (TS passes the sound-caption warn to
parseSubtitleText). The q2 HUD commands read op.text as bytes where TS uses charCodeAt.

Not done, next free slot:
- createQ3SeatClient: the native branch is straightforward (the round-restart closures in application_travel.c are the same
  linkBounds/sourceActor/predictionCommand/serverSettings); the QVM branch passes `state.source` and `state` (LocalQ3ClientState)
  as `qa_app_q3_client_source` and `qa_q3_client_state`, and no C view exists over `qa_app_local_q3_client_state` (TS
  client-state.ts implements both interfaces; the C needs game-state record copies, wire-to-base user command conversion and
  the snapshot `current`).
- openGraphical and publishBorrowed (not reached).

Tests: tests/app/test_app_concrete_seat_ui.c (oracle tests/app/oracles/app_concrete_seat_ui.ts, transcript
app_concrete_seat_ui.txt; retail Q1 rerelease id1: weapon HUD icons and aspects, the cache and missing-picture error, the id1
authored wheel items and switchWeapon, the localized prompt controls and keys; sentinel contexts on every callback).
tests/persistence/test_persistence_left_a: the Q1 foundation case encodes a version 6 checkpoint (TS Q1 content writes 6;
persistence/q1-foundation.ts reads choice(5, 6)), so its rejected literal is now 7.

Link probe (build/agents/app-concrete3/probe/undef.sh over a private `ar qc` archive): 7 undefined before, 4 after:
`qa_application_create_q3_seat_client`, `qa_application_open_graphical`, `qa_application_publish_borrowed` (mine, not done) and
`qa_application_open_guest_browser` (guest bots and browser porter). `qa_application_prepare_guest_bots` was defined concurrently.

### Found by app-browser (2026-11-03)

openGuestBrowser is `qa_application_open_guest_browser` (application_guest_browser.c). StartupServerBrowser
(server_browser.c) is re-ported over the real network code: `qa_app_startup_server_browser_open(config)` binds a
`qa_udp_transport` (0.0.0.0:0, broadcast) and builds the four ServerBrowser cores with the Q1/QW/Q2/Q3 discovery wires;
the 15-callback host is gone. The local guest browser record gained `adapter` (the Q3BrowserHost view the
Q3BrowserView reads); application_close.c and the world-replacement rollback call `qa_application_release_guest_browser`
after closing it (TS GC). maxPing/statusResendTime come from the first guest seat's `cl_maxPing` /
`cl_serverStatusResendTime` (800 / 750 when there is none); the Q3BrowserView takes int64 thresholds, so a fractional cvar
is truncated toward zero. Link probe (build/agents/app-browser/probe/undef.sh, the app-concrete2 probe over a private
`ar qc` archive): `qa_application_open_guest_browser` resolves; 3 undefined remain, none from this work:
`qa_application_create_q3_seat_client`, `qa_application_open_graphical`, `qa_application_publish_borrowed`.

### Found by app-final (2026-11-03)

Members done: createQ3SeatClient (application_graphical.c), openGraphical and publishBorrowed (application_open_graphical.c).
- createQ3SeatClient: the QVM branch passes LocalQ3ClientState through two new views in q3_client/client_state.c:
  `qa_app_local_q3_client_state_source` (TS `state.source`, ApplicationQ3ClientSource with the assertCurrent guards, game
  state strings, system info, server commands, commands with `{x,y,z}` angles and the snapshot history) and
  `qa_app_local_q3_client_state_connection` (the state as Q3ClientState: generation, sequences, snapshotPing,
  getServerCommand, gameState get/copySourceRecord, commands). Its "retired gamestate" and receive errors raise Error (they
  aborted). The native branch shares restartSourceRound's closures, now `qa_application_round_*` over
  `qa_application_q3_round_binding` (internal header). remapShader is primaryShaderControl over
  `qa_app_component_media_host_of`; a rejected shader preparation raises Error.
- openGraphical: owned renderer owner `{identity, session, generation 0}` with its root SceneImageRegistry (freed with the
  application), NativeRenderer.open with r_smp and deferWindowVisibility, restored and new players, team arena overrides,
  ApplicationInput open / prepareForClient (live registries as TS), configuration + startupReader (the captured
  configuration is `qa_application_input_actions.startup_configuration`), audio volume/output binding, frontend baseline,
  native UI art with `{ frames: false, mainBackground: false }` (new `qa_ui_load_native_art_selected`; stageGraphical now
  uses it too), effects, resources, rerelease, per-seat Q3 client / native Q2 seat / seat UI / presentation (attached to
  the seat for owned sources, cleanup closes and frees it), presentation services over `presentationClock`, capture and
  video restart, and the TS catch cleanup.
- publishBorrowed: the application is published as `client.source_current`, a `qa_app_client_source_lifetime` row the
  application owns (`source_lifetime`, errors as borrowed text; capture_map follows content changes in stageCommit).
- Destroy freed `base_arena_progress` before `qa_application_seats_free` released its rows (use after free on every
  Q3 close); `qa_cvar_registry_find(..., NULL)` crashed (find fills its out argument) in openGraphical's volume check and
  two transition sites; kept input-action records were freed twice (`qa_application_input_actions_release`).

Lower layers fixed on the way (each blocked the graphical run):
- prepared_startup.c: the command buffer copied the startup's cvar routing by value, so after ApplicationInput adopted a
  new routing (and freed the old owner) every registration read freed memory. The buffer now reads the current routing,
  as TS `cvarRouting: { owner: (n, c) => this.routing.owner(n, c), ... }`.
- menu_font.c / assets.c: the conchars image carried no source (TS registers it with the gfx.wad resource or the loaded
  texture's source), so openGraphical's "Native menu font has no mounted resource identity" fired; the menu font host has
  `register_sourced`.
- render/scene/geometry.c: every brush face pointed at one static copy of the last decoded BSP's lighting (a Quake ammo
  box's 87 bytes), so world lightmaps read out of range; faces now reference their map's lighting.
- render/scene/resources.c: queued create operations kept the producer's content by reference (stack levels, freed
  lumps, atlas scratch); the registry now copies created content until the drain after its release (the
  "CPU image dimensions disagree" conchars failure noted by app-seat-pres has the same cause). ui/common/assets.c keeps its
  white image level static.
- Item 4 of the assignment: ui/saves/menu.c takes settle closures (`qa_ui_saves_settle`): the menu stays busy with
  "Working..." until the queued save/load/restart settles in step, then refreshes and closes or shows the rejection, as the
  TS run() awaits; player_death.c's restart likewise. ui/hud/q2_native.c and q2_rerelease_layout.c raise
  NativeQ2HudLayoutError (and plain Error for the missing environment), q2LayoutOperations converts TextParseError, and the
  HUD frees the partial operations it drops. text/media_captions.c already had `warn` (`qa_seat_media_captions_set_warn`,
  used by sound_captions.c). Q2 HUD text: the non-xor HUD strings are Q2 strings decoded one char per byte in TS
  (String.fromCharCode / latin1), which C keeps as raw bytes, so `op.text[i]` is `charCodeAt(i)`; the xor strings are
  UTF-8 in C, which is the TextEncoder byte form (comment at the site).

Tests: tests/app/test_application_open_real.c with oracle tests/app/oracles/application_open_real.ts (live bun): the real
Application opened dedicated on retail Q1 id1 e1m1, Q2 baseq2 base1 and Q3 baseq3 q3dm1, 60 steps of 50 ms, closed: console
output, recipe, source kind, frame count, live actors, players and last snapshot counts match TS line for line (46, 10 and 29
lines). One graphical Q1 e1m1 application under SDL_VIDEODRIVER=offscreen with `--hidden` (real GL context) opens, steps 5
frames and closes. Without `--hidden` the offscreen driver rejects SDL_SetRelativeMouseMode when the router captures the
mouse for the focused game seat; TS calls the same SDL function through the same path. tests/ui/test_ui.c checks the queued
save (busy, refused second write, rejection message, resolved refresh and close, settle after dispose).

Also found under ASan: openSource gave `qa_identity_owner_create` a stack buffer for the identity name, which every
SessionId keeps (presentation events copied freed stack text); the name now lives in the open record.
`qa_app_prepared_startup_bindings` returned shallow copies whose strings binding the prepared seat freed; it returns deep
copies now (input.c and test_configuration.c dispose them). The GraphicalApplication record openGraphical keeps destroys
the seat Q3 clients and prediction hosts, seat UIs, effects, rerelease presentation, input, audio and assets, then the
renderer and root registry.

ASan/UBSan (build/agents/app-final-asan, leaks on): q1, q2 and q3 dedicated have no memory or UB error and their transcripts
match the oracle; their leaks (about 1-2.5 KB) are in openSource's demo-command bindings, startup/configuration script
readers, collected server settings and the Q3 arsenal (content/q3/foundation/arsenal.c). The graphical run has no memory or
UB error; its 2.7 MB of leaks are the known texture-loader, material and lightmap allocations (render/scene, effects/q3
shaders) and update-level copies in resources.c.

Link probe (build/agents/app-final/probe/undef.sh, private `ar qc` archive): 5 undefined before (the three members above,
`qa_application_open_guest_browser` and `qa_q3_mirror_server_status_destroy` from the server browser re-port), 0 after.
