# RemoteApplication layout (remote-application.ts -> C)

Source: `../quake-anthology-ts/src/app/bootstrap/remote-application.ts` (2,511 lines on 2026-11-03; the TS tree is
edited concurrently, so re-run the outline before trusting a line number). One TS class, `RemoteApplication`, is ported
as one C type, `qa_remote_application` (public header `include/qa/app/bootstrap/remote_application_class.h`), in
`src/app/bootstrap/remote_application_*.c`. It is built beside the old host shell `qa_app_remote_application`
(`remote_application.c`, `remote_application.h`), which main.c and startup.c keep using until the connection step. The
two share no code.

Naming: member `fooBar` is `qa_remote_application_foo_bar(qa_remote_application *app, ...)`; a static member takes no
`app`. Fields are members of `struct qa_remote_application` in `src/app/bootstrap/remote_application_internal.h`, in TS
order; the constructor parameters (`launchOptions`, `mountedContent`, `session`, `renderer`, `host`, `imageSettings`,
`identity`, `browser`, `ownership`, `configuration`, `keys`) are fields too. TS async members are synchronous
(PORTING.md); a TS throw is `qa_raise("Error", ...)`, a TS catch is `qa_try`. TS GC lifetimes of registries, transfers,
seat inputs and configuration records become `qa_remote_arena`, a cleanup list the application runs at destroy
(the TS objects stay reachable from closures until the application is dropped).

Status: "done" = every branch ported and checked against TS; "ported" = every branch written from the TS, compiled
warning-free and linked, not yet run against TS output; "not done" = declared in the internal header only, with
the blocking dependency named. Nothing is stubbed.

## Files

| C file | TS region | Status |
|---|---|---|
| `include/qa/app/bootstrap/remote_application_class.h` | public type and public members | header complete; definitions as below |
| `src/app/bootstrap/remote_application_internal.h` | module types 137-214, the struct (every field), every member's declaration | done |
| `src/app/bootstrap/remote_application_core.c` | 216-312 fields/getters/allocateQport, 452-465 recordedOptions, 598-603, 898, 923, 936-954, 1585-1590, 2314, 2508-2511 | done |
| `src/app/bootstrap/remote_application_config.c` | 2392-2506 prepareRemoteConfiguration and the RemoteConfiguration record | done |
| `src/app/bootstrap/remote_application_open.c` | 313-597 constructor, recordedSource, open*, openSource | ported (rows below) |
| `src/app/bootstrap/remote_application_profile.c` | 603-897 flushClientCommands..publishConnecting | ported (rows below) |
| `src/app/bootstrap/remote_application_world.c` | 900-935, 956-1251 video restart, retirement, browser, frontend, content, downloads, world loading | ported (rows below) |
| `src/app/bootstrap/remote_application_seat.c` | 1252-1456 bindSeat | ported (rows below) |
| `src/app/bootstrap/remote_application_cinematic.c` | 1457-1579 cinematics | ported (rows below) |
| `src/app/bootstrap/remote_application_commands.c` | 1592-1799 dispatch, recording, downloads, executeRemoteCommand, underwater | ported (rows below) |
| `src/app/bootstrap/remote_application_seats.c` | 1800-2129 local remote seats (join, drop, prepare, bind, poll, retire, frames) | ported (rows below) |
| `src/app/bootstrap/remote_application_step.c` | 2130-2325 step, sourceSessionActions, run, readPixels, captureNextFrame | ported (rows below) |
| `src/app/bootstrap/remote_application_close.c` | 2326-2390 close, closeOwned | ported (rows below) |
| `tests/app/test_remote_application.c` | C checks | see "Tests" |

## Module-level declarations

| TS lines | TS declaration | C | Status |
|---|---|---|---|
| 137-144 | `RemoteWorldFrontend` | `qa_remote_world_frontend` | done (type) |
| 145-160 | `RemoteSeatPeer` | `qa_remote_seat_peer` | done (type) |
| 161 | `RemoteCommand` | `qa_remote_command` (deep-copied context: origin chain and names owned) | done |
| 162-165 | `RemoteApplicationHost`, `RemoteDemoApplicationHost` | `qa_remote_application_host` (public header) | done (type) |
| 166-171 | `RemoteCommandOwner`, `MutableRemoteCommandOwner` | `qa_remote_command_owner` | done (type) |
| 172 | `RemoteOwnership` | `qa_remote_ownership` | done (type) |
| 173-190 | `RemoteConfiguration` | `qa_remote_configuration` + `qa_remote_configuration_validate_source` / `_publish_source` / `_prepare_transport` / `_routing` | done |
| 191-197 | `PeerDirectoryAdmission` | `qa_remote_peer_admission` | done (type) |
| 198-203 | `RemoteAdvanceOutcome`, `PendingRemoteAdvance` | `qa_remote_pending_advance` (pumped state, no promise) | done (type) |
| 204 | `RemoteCinematic` | `qa_remote_cinematic` | done (type) |
| 206 | `RemoteBrowser` | `qa_remote_browser` | done (type) |
| 208-214 | `LiveNetwork`, `DemoLaunch`, `GtvLaunch`, `RemoteLaunch`, `RemoteSource` | `qa_remote_live_network`, `qa_remote_launch`, `qa_remote_source` | done (types) |
| 2392-2506 | `prepareRemoteConfiguration` | `qa_remote_prepare_configuration` | done |
| 2508-2511 | `requireQwAddress` | `qa_remote_require_qw_address` | done |

## Class members

| TS lines | kind | TS member | C name | C file | Status |
|---|---|---|---|---|---|
| 217 | getter | `serverBrowser` | `qa_remote_application_server_browser` | core | done |
| - | getter | `session` (constructor `readonly session`) | `qa_remote_application_session` | core | done |
| 218 | field | `initialComponents` | `.initial_components` | internal.h | done |
| 219-220 | field/getter | `clientCommandOwner`, `clientCommands` | `.client_command_owner`, `qa_remote_application_client_commands` | core | done |
| 221-225 | field | `viewSettings` (+ fov change closure) | `.view_settings`, `qa_remote_application_view_fov_changed` | core | done (ran in the q1/qw/q2/q3 loopback against TS, tests/app/test_remote_loopback.c) |
| 226-231 | fields | `releaseViewCvars`, `socksSettings`, `clientConfig`, `inputConfig`, `playerProgress`, `downloadPermission` | struct fields | internal.h | done |
| 232-234 | field/getter | `initialRemote`, `primaryPeer`, `remote` | `.initial_remote`, `.primary_peer`, `qa_remote_application_remote` | core | done (the RemoteSeatPresentation union of remote_seat_source.h; `qa_remote_presentation` is its typedef) |
| 235-242 | field/getter | `initialSource`, `promotedSource`, `source` | `.initial_source`, `.promoted_peer`/`.promoted_source`, `qa_remote_application_source` | core | done |
| 243 | field | `family` | `.family` | internal.h | done |
| 244 | getter | `network` | `qa_remote_application_network` | core | done (`qa_remote_live_network` is the RemoteSeatNetwork union) |
| 245 | method | `sendCommand` | `qa_remote_application_send_command` | core | done (through `network`; a Q2 network before connection raises "Q2 client is not connected" as TS) |
| 246-252 | fields/getters | `loadedContent`, `frontend`, `cinematic`, `cinematicRequest`, `cinematicPresented`, `presentedCinematic`, `hasCinematic` | fields, `qa_remote_application_presented_cinematic`, `_has_cinematic` | core | done |
| 254-258 | fields | `retiredFrontends`, `controls`, `capture`, `ownedVideoRestart`, `initialPresentation` | fields | internal.h | done |
| 259-263 | getter/setter | `presentation` | `qa_remote_application_presentation`, `_set_presentation` | core | done |
| 264-299 | fields | `commands` .. `uiPreferences` | fields | internal.h | done |
| 275 | getter | `primaryViewCurrent` | `qa_remote_application_primary_view_current` | core | done |
| 298 | getter | `seatId` | `qa_remote_application_seat_id` | core | done |
| 301-304 | fields | `remoteSeats`, `qports`, `initialQport`, `initialUserinfo` | fields | internal.h | done |
| 305-310 | method | `allocateQport` | `qa_remote_application_allocate_qport` | core | done |
| 311-312 | fields | `musicControls`, `keyProfile` | fields | internal.h | done |
| 313-438 | constructor | `constructor` | `qa_remote_application_construct` | open | done (ran in the q1/qw/q2/q3/unified loopback against TS, tests/app/test_remote_loopback.c) |
| 440-450 | method | `recordedSource` | `qa_remote_application_recorded_source` | open | ported; not run against TS output yet |
| 452-464 | static | `recordedOptions` | `qa_remote_application_recorded_options` | core | done |
| 466-469 | static | `openDemoBorrowed` | `qa_remote_application_open_demo_borrowed` | open | ported; the missing-browser error checked against TS |
| 471-485 | static | `openGtvBorrowed` | `qa_remote_application_open_gtv_borrowed` | open | ported; not run against TS output yet |
| 487-489 | static | `open` | `qa_remote_application_open` | open | done (ran in the q1/qw/q2/q3 loopback against TS, tests/app/test_remote_loopback.c) |
| 491-493 | static | `openBorrowed` | `qa_remote_application_open_borrowed` | open | ported; not run against TS output yet |
| 495-596 | static | `openSource` | `qa_remote_application_open_source` | open | done (ran in the q1/qw/q2/q3 loopback against TS, tests/app/test_remote_loopback.c) |
| 598 | getter | `options` | `qa_remote_application_options` | core | done |
| 599 | getter | `finished` | `qa_remote_application_finished` | core | done |
| 600 | getter | `clientCommandsBlocked` | `qa_remote_application_client_commands_blocked` | core | done (ran in the q1/qw/q2/q3 loopback against TS, tests/app/test_remote_loopback.c) |
| 601 | getter | `videoRestart` | `qa_remote_application_video_restart` | core | done (ran in the q1/qw/q2/q3 loopback against TS, tests/app/test_remote_loopback.c) |
| 602 | method | `pumpClientInput` | `qa_remote_application_pump_client_input` | core | done |
| 603 | method | `advanceClientStartup` | `qa_remote_application_advance_client_startup` | core | done |
| 604-607 | method | `flushClientCommands` | `qa_remote_application_flush_client_commands` | profile | done (ran in the q1/qw/q2/q3 loopback against TS, tests/app/test_remote_loopback.c) |
| 609-612 | method | `ownsPublishedSource` | `qa_remote_application_owns_published_source` | profile | done (`client.source.current === this` compares with the application's `source_lifetime` view; `remote.client` is configuration.seat's client, or the promoted peer's seat client) |
| 614-617 | method | `publishRemote` | `qa_remote_application_publish_remote` | profile | done |
| 619-621 | method | `disconnectSource` | `qa_remote_application_disconnect_source` | profile | done |
| 623-625 | method | `activateSourceCommands` | `qa_remote_application_activate_source_commands` | profile | done (ran in the q1/qw/q2/q3 loopback against TS, tests/app/test_remote_loopback.c) |
| 627-631 | method | `clearSourcePresentation` | `qa_remote_application_clear_source_presentation` | profile | done |
| 633-640 | method | `retireFrontends` | `qa_remote_application_retire_frontends` | world | done (ran in the q1/qw/q2/q3 loopback against TS, tests/app/test_remote_loopback.c) |
| 642-648 | method | `startupReader` | `qa_remote_application_startup_reader` | profile | done (ran in the q1/qw/q2/q3 loopback against TS, tests/app/test_remote_loopback.c) |
| 650-686 | method | `adoptUnifiedProfile` | `qa_remote_application_adopt_unified_profile` | profile | done (the unified loopback cases take the unchanged-profile return; unified-cross, unified-cross-q1 and unified-cross-q3 take the profile change over the loaded content's mounts, ran against TS) |
| 688-779 | method | `publishPeerConfiguration` | `qa_remote_application_publish_peer_configuration` | profile | ported; not run against TS output yet |
| 781-803 | method | `advanceSource` | `qa_remote_application_advance_source` | profile | done (ran in the q1/qw/q2/q3 loopback against TS, tests/app/test_remote_loopback.c) |
| 805-810 | method | `presentLoadingFrame` | `qa_remote_application_present_loading_frame` | step | done (ran in the q1/qw/q2/q3 loopback against TS, tests/app/test_remote_loopback.c) |
| 812-856 | method | `publishConfiguration` | `qa_remote_application_publish_configuration` | profile | done (ran in the q1/qw/q2/q3 loopback against TS, tests/app/test_remote_loopback.c) |
| 858-896 | method | `publishConnecting` | `qa_remote_application_publish_connecting` | profile | ported; not run against TS output yet |
| 898 | getter | `captureMap` | `qa_remote_application_capture_map` | core | done |
| 900-917 | method | `prepareVideoRestart` | `qa_remote_application_prepare_video_restart` | world | ported; not run against TS output yet |
| 919-922 | method | `prepareRetirement` | `qa_remote_application_prepare_retirement` | world | ported; not run against TS output yet |
| 923 | method | `releaseSettings` | `qa_remote_application_release_settings` | core | done |
| 924 | method | `retire` | `qa_remote_application_retire` | close | ported; not run against TS output yet |
| 926-934 | method | `saveSourceSettings` | `qa_remote_application_save_source_settings` | world | done (ran in the q1/qw/q2/q3 loopback against TS, tests/app/test_remote_loopback.c) |
| 936-939 | getter | `content` | `qa_remote_application_content` | core | done |
| 940 | getter | `mounts` | `qa_remote_application_mounts` | core | done |
| 941 | getter | `frameCount` | `qa_remote_application_frame_count` | core | done |
| 942 | getter | `timeMilliseconds` | `qa_remote_application_time_milliseconds` | core | done (a promoted peer's source is always live) |
| 943 | getter | `window` | `qa_remote_application_window` | core | done |
| 944 | getter | `networkAddress` | `qa_remote_application_network_address` | core | done (returns a copy) |
| 945 | getter | `networkPhase` | `qa_remote_application_network_phase` | core | done (TS phase strings; a promoted peer through its typed network) |
| 946 | method | `readClientResource` | `qa_remote_application_read_client_resource` | core | done |
| 947 | getter | `localPlayers` | `qa_remote_application_local_players` | core | done (ApplicationInput locals: `qa_app_application_input_local_count`/`_local`) |
| 948-949 | getters | `presentationEvents`, `unhandledPresentationEffects` | `qa_remote_application_presentation_events`, `_unhandled_presentation_effects` | core | done |
| 951-954 | method | `print` | `qa_remote_application_print` | core | done |
| 956-972 | method | `initializeQ3Browser` | `qa_remote_application_initialize_q3_browser` | world | done |
| 974-1001 | method | `browserCommand` | `qa_remote_application_browser_command` | world | done (ran in the q1/qw/q2/q3 loopback against TS, tests/app/test_remote_loopback.c) |
| 1003-1026 | method | `loadFrontend` | `qa_remote_application_load_frontend` | world | done (ran in the q1/qw/q2/q3 loopback against TS, tests/app/test_remote_loopback.c) |
| 1028-1033 | method | `releaseFrontend` | `qa_remote_application_release_frontend` | world | ported; not run against TS output yet |
| 1035-1041 | method | `refreshImages` | `qa_remote_application_refresh_images` | world | done (ran in the q1/qw/q2/q3 loopback against TS, tests/app/test_remote_loopback.c) |
| 1043-1050 | method | `loadQ2ServerWorld` | `qa_remote_application_load_q2_server_world` | world | done |
| 1052-1087 | method | `selectRemoteContent` | `qa_remote_application_select_remote_content` | world | done (ran in the q1/qw/q2/q3 loopback against TS, tests/app/test_remote_loopback.c) |
| 1089-1093 | method | `refreshRemoteContent` | `qa_remote_application_refresh_remote_content` | world | ported; not run against TS output yet |
| 1095-1113 | method | `prepareQwDownloads` | `qa_remote_application_prepare_qw_downloads` | world | done (ran in the q1/qw/q2/q3 loopback against TS, tests/app/test_remote_loopback.c) |
| 1115-1148 | method | `prepareQ3Downloads` | `qa_remote_application_prepare_q3_downloads` | world | done |
| 1150-1168 | method | `loadQ3ServerWorld` | `qa_remote_application_load_q3_server_world` | world | done |
| 1170-1250 | method | `loadServerWorld` | `qa_remote_application_load_server_world` | world | done (ran in the q1/qw/q2/q3 loopback against TS, tests/app/test_remote_loopback.c) |
| 1252-1455 | method | `bindSeat` | `qa_remote_application_bind_seat` | seat | done (ran in the q1/qw/q2/q3/unified loopback against TS; the unified branch builds the RemoteComponentView and the recipe's movement dialect; its Q3-character branch is not run yet) |
| 1457-1465 | method | `componentCinematics` | `qa_remote_application_component_cinematics` | cinematic | ported; not run against TS output yet |
| 1467-1486 | method | `systemCinematicHost` | `qa_remote_application_system_cinematic_host` | cinematic | done (ran in the q1/qw/q2/q3 loopback against TS, tests/app/test_remote_loopback.c) |
| 1488-1495 | method | `startCinematic` | `qa_remote_application_start_cinematic` | cinematic | ported; not run against TS output yet |
| 1497-1509 | method | `activateCinematic` | `qa_remote_application_activate_cinematic` | cinematic | ported; not run against TS output yet |
| 1511-1552 | method | `prepareCinematic` | `qa_remote_application_prepare_cinematic` | cinematic | ported; not run against TS output yet |
| 1554-1560 | method | `stopCinematic` | `qa_remote_application_stop_cinematic` | cinematic | done (ran in the q1/qw/q2/q3 loopback against TS, tests/app/test_remote_loopback.c) |
| 1562-1569 | method | `completeCinematic` | `qa_remote_application_complete_cinematic` | cinematic | ported; not run against TS output yet |
| 1571-1578 | method | `closeCinematic` | `qa_remote_application_close_cinematic` | cinematic | ported; not run against TS output yet |
| 1580-1583 | method | `input` | `qa_remote_application_input` | core | ported; not run against TS output yet |
| 1585-1590 | method | `queueCommand` | `qa_remote_application_queue_command` | core | done |
| 1592-1600 | method | `dispatchCommands` | `qa_remote_application_dispatch_commands` | commands | done (ran in the q1/qw/q2/q3 loopback against TS, tests/app/test_remote_loopback.c) |
| 1602-1605 | method | `attachConnectingRecording` | `qa_remote_application_attach_connecting_recording` | commands | ported; not run against TS output yet |
| 1607-1620 | method | `standaloneRecording` | `qa_remote_application_standalone_recording` | commands | ported; not run against TS output yet |
| 1622-1636 | method | `prepareRecording` | `qa_remote_application_prepare_recording` | commands | ported; not run against TS output yet |
| 1638-1641 | method | `primaryDownloadProgress` | `qa_remote_application_primary_download_progress` | commands | done |
| 1643-1645 | getter | `downloadProgress` | `qa_remote_application_download_progress` | commands | done |
| 1647-1653 | method | `cancelDownloads` | `qa_remote_application_cancel_downloads` | commands | done |
| 1655-1661 | method | `retryDownloads` | `qa_remote_application_retry_downloads` | commands | done |
| 1663-1665 | method | `executeApplicationRequest` | `qa_remote_application_execute_application_request` | commands | ported; not run against TS output yet |
| 1667-1790 | method | `executeRemoteCommand` | `qa_remote_application_execute_remote_command` | commands | done (ran in the q1/qw/q2/q3 loopback against TS, tests/app/test_remote_loopback.c) |
| 1792-1798 | method | `underwater` | `qa_remote_application_underwater` | commands | done (ran in the q1/qw/q2/q3 loopback against TS, tests/app/test_remote_loopback.c) |
| 1800-1802 | method | `drainSeatPublications` | `qa_remote_application_drain_seat_publications` | seats | done |
| 1804-1812 | method | `sourceForActor` | `qa_remote_application_source_for_actor` | seats | done |
| 1814-1818 | method | `seatViewport` | `qa_remote_application_seat_viewport` | seats | done |
| 1820-1827 | method | `publishRemoteLayouts` | `qa_remote_application_publish_remote_layouts` | seats | done |
| 1829-1869 | method | `joinRemoteSeat` | `qa_remote_application_join_remote_seat` | seats | ported; not run against TS output yet |
| 1871-1931 | method | `dropRemoteSeat` | `qa_remote_application_drop_remote_seat` | seats | ported; not run against TS output yet |
| 1933-2027 | method | `prepareRemoteSeats` | `qa_remote_application_prepare_remote_seats` | seats | done (ran in the q1/qw/q2/q3 loopback against TS, tests/app/test_remote_loopback.c) |
| 2029-2033 | method | `peerWorldCurrent` | `qa_remote_application_peer_world_current` | seats | done |
| 2035-2068 | method | `bindRemoteSeat` | `qa_remote_application_bind_remote_seat` | seats | ported; not run against TS output yet |
| 2070-2072 | method | `pollRemoteSeats` | `qa_remote_application_poll_remote_seats` | seats | done |
| 2074-2090 | method | `retireRemoteSeat` | `qa_remote_application_retire_remote_seat` | seats | ported; not run against TS output yet |
| 2092-2099 | method | `retireRemoteViews` | `qa_remote_application_retire_remote_views` | seats | done (ran in the q1/qw/q2/q3 loopback against TS, tests/app/test_remote_loopback.c) |
| 2101-2128 | method | `prepareRemoteSeatFrames` | `qa_remote_application_prepare_remote_seat_frames` | seats | done (ran in the q1/qw/q2/q3 loopback against TS, tests/app/test_remote_loopback.c) |
| 2130-2288 | method | `step` | `qa_remote_application_step` | step | done (ran in the q1/qw/q2/q3/unified loopback against TS: publishRemote with the drained simulation events, typed presentation events, the component view's prepareMedia/prepare, unified audio batches without audio controls) |
| 2290-2312 | method | `sourceSessionActions` | `qa_remote_application_source_session_actions` | step | done (ran in the q1/qw/q2/q3 loopback against TS, tests/app/test_remote_loopback.c) |
| 2314 | method | `requestQuit` | `qa_remote_application_request_quit` | core | done |
| 2315-2322 | method | `run` | `qa_remote_application_run` | step | ported; not run against TS output yet |
| 2323 | method | `readPixels` | `qa_remote_application_read_pixels` | step | ported; not run against TS output yet |
| 2324 | method | `captureNextFrame` | `qa_remote_application_capture_next_frame` | step | ported; not run against TS output yet |
| 2326-2330 | method | `close` | `qa_remote_application_close` | close | done (ran in the q1/qw/q2/q3 loopback against TS, tests/app/test_remote_loopback.c) |
| 2332-2389 | method | `closeOwned` | `qa_remote_application_close_owned` | close | done (ran in the q1/qw/q2/q3 loopback against TS, tests/app/test_remote_loopback.c) |

## Lower layers ported for RemoteApplication (app-remote2, 2026-11-03)

The TS file was reformatted (Biome) and is 4,712 lines on 2026-11-03; the TS line numbers in the tables above are
from the 2,511-line tree. Member order is unchanged.

- `remote_seat_source.h/.c`: RemoteSeatSource on the typed family classes. `qa_app_remote_seat_presentation` and
  `qa_app_remote_seat_network` are the RemoteSeatPresentation / RemoteSeatNetwork unions with dispatch helpers
  (client, player, scene, isPlayer, output, presentations, characterViews, drain events, playerView, worldText;
  phase, poll, command, submit, close). The hooks are typed (`qa_app_remote_seat_source_hooks`): options, mounts,
  content, current/select/refresh content, loadQ1/Q2/Q3World, initialize/shutdownQ3, cinematic, publish,
  disconnected, print, recording. QW downloads (QwDownloadReceiver), Q3 downloads (Q3ApplicationPackages +
  Q3ApplicationClientDownloads, missing-package report, mkdir) and loadQ3World (Q3ClientContent over the seed
  content) are ported in the file. No mirror struct remains.
- `remote_access.h/.c`: the C-only accessors the typed remotes take (TS reads content.recipe / content.world /
  createSceneQueries / identity / scene.link and trace directly): world host, identity, recipe, snapshot world,
  mount read, scene link/unlink/model bounds/actor-is-model, the QW camera trace, sound resolve, unified prepared
  mods/resources, a Q2 RemoteContentMounts view (scoped open over the game-local roots), and the Q3 package mounts
  of a content in defaultOrder. RemoteApplication's constructor uses the same accessors.
- `remote_seat_view.h/.c`: RemoteSeatView on WorldSeatPresentation, ApplicationEffects, ApplicationAssets
  (createWorldAssets), ApplicationSeatUi and ApplicationQ3Client (kind "qvm" over the connection's client-state
  view). For the unified replica the constructor builds a RemoteComponentView and prepareFrame hands the seat the
  drained simulation events (unified-network packet, 2026-11-03; not run against TS output yet: the loopback has one
  seat). `systemCinematics` is now the TS function (fn + its own ctx, RemoteApplication.componentCinematics(audio)).
- `remote_seats.c`: RemoteSeatChannel takes the typed network/remote; prepareRemoteSeatChannels closes the created
  channels on failure (AggregateError when a cleanup fails); the TS Errors are catchable raises.
- `server_map.h/.c`: serverMapPath.
- `client_bootstrap.h`: `local_lobby`, `settings`, `caption_commands`, `activate_frontend` (with
  `qa_app_client_frontend_configuration`) join the members other porters added.

## Members ported by app-remote3 (2026-11-03)

Every remaining member is now written from the TS (rows above marked "ported"): the constructor, recordedSource
and the openers (`remote_application_open.c`), the profile members (`_profile.c`), the world members (`_world.c`),
bindSeat (`_seat.c`), the cinematics (`_cinematic.c`), dispatch/recording/executeRemoteCommand/underwater/input
(`_commands.c`), the local remote seats (`_seats.c`), step/sourceSessionActions/run/readPixels/captureNextFrame
(`_step.c`), close/closeOwned/destroy and the ClientSourceLifetime (`_close.c`), the RecordedRemoteSource family
inputs (`_recorded.c`) and shared helpers (`_support.c`). The application is the ctx of the presentations it builds
(its first member is the `qa_app_remote_access`).

C accommodations (each noted at its site):
- advanceSource runs its operation synchronously; a peer directory admission requested while it runs publishes at
  once (selectRemoteContent), so no frame boundary is left pending; closeOwned therefore has no pending advance to await.
- prepareRemoteSeats' peer hooks run their pumped operation at once through the seat's RemoteSeatPump (enqueue +
  drain); when its frame gate is closed the synchronous receive cannot wait and the hook raises.
- CampaignCinematic gained `qa_app_campaign_cinematic_prepare_engine` (the `{ images }` / `{ engine }` members TS
  passes); SeatInput gained `qa_seat_input_set_gamepad_tuning`; ClientBootstrap's frontend configuration gained
  `release_target` (the program's release-dialect append target).

Unified network family (unified-network packet, 2026-11-03):
- `network/unified_event_values.h/.c`: SimulationPresentationEvent <-> the unified event tree (the TS event object the
  codec reads and writes), for every kind and payload variant (q1, q1-composition with its addon events, q1-level,
  q2, q2-weapon, q2-composition (ctf, lmctf, missionpack), q2-rerelease, q2-player, q3-character, q3-source, and the
  inline kinds), one writer/reader pair per TS writer; identities ride as node attachments. Also the client's
  SimulationEvent view of a decoded simulation event (sound and message payloads, the ones a frame carries).
  tests/network/test_unified_event_values.c round-trips every presentation row of the TS codec corpus (217 rows)
  through the typed events to the TS bytes. C domain gaps the test names: sourceEntity null vs undefined (C
  has_source_entity; emitOwned always sets it), Q1ClientSnapshot slot/shirt/pants and CtfStatus flags/runeItems are
  int64_t, Q2 model attachedModels is a fixed `[3]`.
- Server: simulation/network_unified.c `presentationEvents` converts the application's typed events to trees before
  the audience filter; unified_prediction.c `projectPrediction` deep-copies the simulation pointers it took (jump pad,
  client movement outputs, each linked body's ground and collision owner), which qa_app_unified_prediction_projection_free
  released as if owned (the `free(): invalid size`); the server's cookie secret and the client's nonce draw from
  getrandom (TS randomBytes / crypto.getRandomValues).
- Client: `remote_unified.c` keeps its PresentationState (`media`, replaced per offer) and receives each decoded event
  typed; the consumers admit/retire owners on it; drainPresentationEvents is takePresentation, drainSimulationEvents
  returns typed SimulationEvents (with the local sourcePresentationSequence); the predictor gets the replica's scene.
  `remote_components.c` is RemoteComponentView over ApplicationModPresentations with componentMediaControl /
  preparePresentationAudio / preparePresentationShaders on the replica's media. `remote_application_unified.c` is the
  UnifiedContentHost over the installed catalog (mountsFor as ContentMount trees, openMountPlan over the rebound plan
  (new host members open_plan/close_plan), applicationOptionsForRecipe + loadApplicationContent with source
  {kind "unified", family}) and adoptUnifiedProfile's mounted content (`qa_app_configuration_content_over_loaded`,
  content.c: catalog/mounts/q3Product of the loaded content, selection from its recipe, close releasing the main-mount
  lease once).
- Not run against TS output yet: the unified RemoteSeatView (local remote seats), component sources (no mod
  presentation in the loopback recipes), a Q3 character over the unified replica. The mounted-content path of
  adoptUnifiedProfile (profile change) runs in the three cross loopback cases (unified-cross-* below).
- Profile change fix (unified2, 2026-11-03): `qa_app_client_socks_settings_route` stored the routing's `base` on the
  shared ClientSocksSettings, so building the candidate's staged routing (TS `stagedRouting`, socks.route over the
  candidate profile's routing) rewrote what the published configuration's routing reported: the prepared startup's
  live set then contained the candidate's fresh source registry and prepareClientCommands raised "Candidate client
  commands require isolated cvar owners". TS `route(base)` returns a new closure per call; the C route is now its own
  object (base + visible storage) owned by the settings (network/socks_settings.c).
- initializeQ3Browser uses the server-browser porter's `qa_app_startup_server_browser_q3_host_create`.

## Tests

`tests/app/test_remote_loopback.c` (app-remote4, 2026-11-03): the C server is the real Application (`qa_application_open`
`--dedicated --listen/--listen-q2 0 --bind 127.0.0.1` on retail id1 e1m1, q1-quakeworld e1m1, baseq2 base1 coop, baseq3
q3dm1 deathmatch, and two unified servers (`--listen-unified`): retail id1 e1m1 and the mixed `--preset q2-q1-q3`
recipe). A UDP relay in the test records every client datagram (a NetQuake accept's per-client port is rewritten
to a relay lane). The C `qa_remote_application` (`--renderer cpu --hidden`, offscreen SDL) and the TS RemoteApplication
under bun (`tests/app/oracles/remote_loopback.ts`, driven over a pipe in lockstep) each connect to a fresh server; the
transcripts (client datagrams through signon and after, server datagram sizes, network phase changes, the first 4 decoded
snapshots with actors and body origins/angles/velocities, client console output, server console) match line for line for
q1, qw, q2 and q3. Clock and randomness are fixed on both sides: the test defines `clock_gettime(CLOCK_MONOTONIC)` as a
virtual clock and `getrandom` as a stateless byte pattern; the oracle replaces `performance.now` and
`crypto.getRandomValues` with the same. A unified server's connect token is an HMAC over the client address, so both
runs of a family send from the same relay port. Every family runs by default (2026-11-03: q1 116, qw 191, q2 169, q3
251, unified 1458, unified-mixed 3111 lines, 0 differing; server consoles 0 differing). Clients whose profile differs from
the served recipe also run by default (2026-11-03, unified2): `unified-cross` (Q2 profile, Q1 unified server, 1461
lines), `unified-cross-q1` (Q1 profile, Q2 base1 coop unified server, 3137 lines) and `unified-cross-q3` (Q3 profile,
Q1 unified server, 1461 lines), each 0 differing, server consoles 0 differing; each takes adoptUnifiedProfile's profile
change (the client re-execs the served product's quake.rc/default.cfg/config.cfg). Under ASan+UBSan
(build/agents/unified-asan) every default family, with the TS runs, matches (0 differing) with no ASan or UBSan error;
LeakSanitizer reports 68.2 MB, none allocated in src/app (render textures/lightmaps/world operations and a few
simulation admission/prediction rows; list in PORT_COMPLETION.md, RemoteApplication item).

`tests/app/test_remote_seat_source.c` (2026-11-03): RemoteSeatSource for q1, qw, q2 and q3 over a fake
ApplicationTransport (opened through `qa_app_open_application_transport` with fake capabilities): first poll
(connection request), the server's accept/challenge, two polls (connect packet with userinfo; Q3's is the Huffman
connect), close (Q1's disconnect datagram), the constructor errors, serverMapPath, and the RemoteApplication getters
over a promoted q2 peer (networkPhase, seatId, networkAddress, localPlayers, downloadProgress, source/network identity,
sourceForActor's error, peerWorldCurrent, sendCommand's "Q2 client is not connected", ownsPublishedSource). Every
datagram byte and line matches `tests/app/oracles/remote_seat_source.txt` (bun `tests/app/oracles/remote_seat_source.ts`).
110 checks; clean under ASan+UBSan with leak detection (the test and the remote/network objects instrumented).

`tests/app/test_remote_application_open.c` (2026-11-03): openSource's early errors (no connect address, Q2
character model, recorded playback without a retained browser) against `tests/app/oracles/remote_application_open.ts`
under bun; host print with a sentinel ctx. No loopback test against a C server yet.

`tests/app/test_remote_application.c` reproduces the unit-reachable parts of the TS tests that import
remote-application.ts. Every TS test that imports it (`tests/first-playable/remote-application/*`, `remote-socks`,
`network/q1/*application*`, `network/q3/application`, `capture/application`, `settings/audio-preferences`,
`input/mouse-application`, ...) runs a full client against a server and needs openSource and step; those remain.
What runs now (2026-11-03): allocateQport, recordedOptions (q1/qw/q3 resources; q2 needs a dm2 fixture), queueCommand
(origin unwinding, deep-copied context, closed guard), requireQwAddress, and prepareRemoteConfiguration over the corpus for
q2-classic, q1-netquake, q1-quakeworld (two seats) and q3 (dialect, seats, seat sources, download permissions,
identities, every registered client cvar's value and flags, userinfo, validate/publish). 125 checks pass without the
corpus; with QTS_TEST_CORPUS 323 checks run and 7 fail, all where TS runs the profile's first StartupConfig frame
(default.cfg/config.cfg/autoexec.cfg prints, config.cfg `rate 24900`, client.cfg `allow_download 0`): in C the
profile's `exec` never reaches its script reader or print (configuration.c / startup_config.c: StartupConfig appends with
`qa_command_buffer_append` where TS hands it `appendPreparation`). ASan+UBSan (this file's objects and the test
instrumented, library uninstrumented): no errors; LeakSanitizer reported 8.5 KB, all allocated in configuration.c
(default bindings, script reader, program inputs) and socks_settings.c (register snapshots never freed). Fixed
(unified2, 2026-11-03): with the whole library instrumented (build/agents/unified-asan) the default run reports no ASan
or UBSan error and no leak.
