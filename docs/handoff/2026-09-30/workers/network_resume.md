# Network source handoff, 2026-09-30

This is a source-only handoff at the user's requested freeze. No code changes follow this boundary. No compiler, configure, build, tests, scripts, parsers, generators, game, sanitizer or benchmark validation ran in this worker. Source acceptance is bounded to the stated files and callers. It does not establish multiplayer, campaign, mod, installed-save or executable parity. B28 and BASELINE remain open.

## Exclusive source ownership and real file inventory

The worker owned `src/network/**`, network public headers, `src/app/application/network.c`, `include/qa/application_network.h`, `src/app/application/network_qw.c`, `include/qa/application_network_qw.h`, `src/app/frontend/network.c`, its actual network headers, and the NQ frontend trio. Other agents own application lifecycle, movement/control, source VM/game owners, source.c, frontend round/scheduler/save aggregates and general public application declarations. Do not edit their files without a new assignment.

Actual frontend files present:

- `src/app/frontend/network.c`: shared installed runtime, Q3 host/client adapters, browser/admin/download services, frontend network continuation, round cut, source send/drop services.
- `src/app/frontend/network_q3_restart.h`: frontend real Q3 round service contract.
- `src/app/frontend/network_nq.c`, `network_nq.h`, `network_nq_private.h`, `network_nq_save.c`: installed classic NQ host, physical peers, real frame publication and retained frontend continuation.
- There is **no frontend network_qw.c, no QW host heap and no installed frontend QW factory**. Do not infer otherwise from the public runtime or observation APIs.

Actual application files present:

- `src/app/application/network.c`, `include/qa/application_network.h`: selected-source legacy NQ reads; primary native/original Q3 source reads; typed raw Q3 glue/seed; canonical remote authority; physical source send/drop binding.
- `src/app/application/network_qw.c`, `include/qa/application_network_qw.h`: pure canonical raw userinfo, actual primary classic QC QW physical observations, world/cvars/signon, genuine Prepare wrapper.
- `src/app/application/network_q1_signon.c/.h`: actual retained stamped QC signon emissions. This producer was not part of the newly edited observer packet and remains its real dependency.

Runtime and source-codec owners:

- `src/network/runtime/{session,commands,save,prediction_save,unified,q3,q1_nq,q1_qw}.c`, `internal.h`; public `network_runtime.h`, `network_save.h`, `network_q1_runtime.h`, `network_q3_runtime.h`, `network_qw_runtime.h`.
- `src/network/q1/qw_source.c`, `include/qa/network_qw_source.h`: new true source Number codec/transmitted frame history, separate from decoded-wire authority.
- `src/network/q1/{channels,checksum,commands,demos,discovery,download_save,handshake,history,netquake,peer_save,quakeworld,scalar,session,token}.c`: original Q1 channels/handshake/sign-on/decoded codecs/native continuation. Corresponding public network_q1* headers remain real dependencies.
- `src/network/q3/{admission,channel,client,client_save,clock,codec,delta,fields_save,huffman,messages,pak_references,pure,server,server_save,visibility}.c` and their private/public headers: actual Q3 wire/admission/state machines and native codecs.
- `src/network/{connections,message,reliability,transport,socks,ipx,dosbox}.c` and private socket/service fields: shared identity, sole receive transport, reliability and native transports.
- `src/network/browser/{owner,wire,persistence}.c`, `internal.h`; `admin/owner.c`; `downloads/{owner,window}.c`; public server_browser/server_admin/downloads and network service-save headers: actual native service owners and continuation.
- `src/network/unified/{channel,commands,composition,document,packet,schema}.c`, `value_internal.h`; public network_unified.h: unified protocol/value schema, including shortest ECMAScript Number serialization reused by NQ ping output.
- Existing `src/network/q2/**` includes classic/kex/rerelease/Q2PRO/R1Q2/MVD/GTV/codec/channel/handshake libraries. This worker did not establish installed Q2 frontend hosting/client producers or complete Q2 source parity. Their existence is not installed interoperability evidence.

## Current frozen, accepted units ready for the root's checkpoint

### Primary QW world/signon/Prepare observer2

Manifest `/tmp/qa-network-qw-primary-world-prepare-20260930.sha256`:

```
2d10927273416c64b4e35591e5a485b644cb0ec0fe1f4115caf8d935d1a5f672  src/app/application/network_qw.c
4734f5969918cda207d390e6f5fd0083a81acae587df5bfc337c8becd32eb0af  include/qa/application_network_qw.h
```

Independent reviewer `network_baseline` accepted complete files, actual producer/caller/lifetime paths and exact narrow QC Prepare dependency. The dependency verdict was renewed for final `/tmp/qc-qw-lifecycle-source3-20260930.sha256`, all12 hashes matched; no observer changes were needed after the QC source remove/free-clock corrections. The whole QC8 packet received its own independent `movement_source_peer` acceptance. Root should preserve the exact observer freeze until the coherent checkpoint/release.

Actual exported behavior:

- `qa_application_network_qw_userinfo_read`: pure borrowed canonical record raw string, full actor generation, valid retained empty string, explicit absent-owner failure, allowed during actual retirement callback while the owner is live. No synthetic dictionary/name/skin/spectator fallback.
- `qa_application_network_qw_source_read`: actual primary ENTITIES classic QC source, unqualified QW28 ABI, 32 physical clients, entity high-water33..512, actual Q1 geometry, idle safe completed source EXIT. Returns real source owner/cvars and **distinct** retained QC source ENTRY time and completed session EXIT time.
- `qa_application_network_qw_world_read`: actor-independent primary source protocol/32 extent, actual product directory basename, actual mapname/message, real indexed lightstyles, nine finite source movement cvars, entityGravity1, genuinely published map resource bytes for the existing `qa_qw_map_checksum2` producer. No player/Character prerequisite; level remains the actual source string, including empty text.
- `qa_application_network_qw_signon_count/at`: actual primary owner's retained stamped original signon events, without selecting a Character actor.
- `qa_application_network_qw_prepare`: pure actual physical admission checks before delegation; idle application, connected inactive client with retained parms; genuine callback-free private QC Prepare; failure faults the application so a partially mutated source row is not retried. This is currently a safe idle completed-source wrapper. The eventual after-EndFrame source action lane needs its own explicit admission/caller closure.
- `qa_application_network_qw_client_read`: real full-generation connected OWNED or BORROWED physical source row. OWNED requires true primary owner/source slot; BORROWED metadata must exactly match the canonical actor's actual owner/source metadata independently of Character namespace. Raw finite truncated Numbers retained for scalar stats/entity fields; actual view/mins/velocity/health/frags, source begun/spectator and weapon model precache.
- `qa_application_network_qw_entity_next`: physical source-owned modeled dynamic rows33+; skips genuine FREE and foreign BORROWED projections. Requires raw nonzero modelindex and real nonempty model name before truncation. No actor reconstruction or foreign physics inference.
- `qa_application_network_qw_precache`: actual strictly indexed source names1..255 with no holes or duplicates.

Final QC dependency: `/tmp/qc-qw-lifecycle-source3-20260930.sha256` freezes eight production files plus four reports. Relevant exact unchanged Prepare owner is guest_qc_clients.c `b1e1dae08d143497be08ea1942ae68015d9ff33dd6bb94b9e1fdfbf9859bcc1c`; internal header `0e2c2086f8e2b65fcb090fa675c7bb758b2cecc038b6ea12c96ac7586fa3eb1e`. Final public qc.h `c404b7873741a640c6caa94918eb1cfbf29c3ec6d46e7e47255eb46a9f7bae8f`; instance.c `b8310627c80295a3eb068b3dafe327d93bd4d6d9c3d353154cbec6597968e5df`; qc_host/game.c `43da7e4b55950634c0eeae89530682e8349d4332813b0182ef3c963d8740f436`; guest_qc.c `54900f451b055487454fe76f3d8cc3ff6b6b66819664b0149cc041833a6f1fab`. QA source remove unlinks/clears/frees before real release; ordinary canonical retirement preserves the reservation separately. Allocation/free time uses genuine retained engine time through the actual create/reset forwarder. Removed reserved rows are not fabricated back into a valid capture/admission pool.

### Source-aware nails2

Manifest `/tmp/qa-network-qw-source-nails-20260930.sha256`:

```
271d32c411c35899bd1bbef992f1dcf0f37ac314e297c265ab319a82b284ad6d  include/qa/network_qw_source.h
0cfdb3ff22ede69c7a8e3777c81bafb053c8fbd982357200198d067ce27491b7  src/network/q1/qw_source.c
```

`network_baseline` accepted whole source/header against real donor nail producer/writer; repeated hashes and whitespace passed. `qa_qw_source_write_nails(writer,const qa_qw_nail*,count)` prequalifies the whole finite binary32 inventory before opcode/output; performs full binary64 `(origin+4096)/2`, `pitch*16/360`, `yaw*256/360`, then truncation/modulo and exact six-byte low12/low4/low8 packing. Negative and huge finite source values remain valid. Codec count0..255 is deliberate; actual factory must preserve its source cap32 and omit empty output. Generic decoded writer unchanged. No actual publisher caller yet.

Final combined network evidence report: `docs/implementation/network-20260930.md`, hash `58619929b937b1b26ce7720a770446326189d0cbc539f5a9c41c372f587fe02b`, recorded in `/tmp/qa-network-qw-source-nails-evidence-20260930.sha256`. This supersedes earlier report hashes; old manifests that include that report are historical, not current exact cuts.

## Previously accepted integrated network units and retained state

### Typed raw Q3 runtime2 and app/frontend callers3

Current accepted manifests remain:

- `/tmp/qa-network-q3-source-command-runtime-20260930.sha256`: network_runtime.h `5766b8384c014a4551c8d7aa85596a76ee56e28c24a1f9a51eb83f6d8268abb0`, commands.c `686964d3e979aa4d18fb58cea46465859f756029c96287fbe1311b16d16d202f`.
- `/tmp/qa-network-q3-source-command-callers-20260930.sha256`: application/network.c `b4ee312881d67cd090267886dc90cf104740bcf44bec0ee561e467147aaec9b7`, application_network.h `a76e3a09cca0f48a5291be7e1ac2b461f2096a3e409d61bd139525b935ea06ff`, frontend/network.c `3618ed912d6c914484cb375f3b60641ce2e44fd37de86ceb0541522acf8f80ff`.

Runtime `qa_network_q3_source_command` retains full native raw usercmd separately from selected movement authority and transport ordinal. `qa_network_accept_q3_source_command` authenticates real ACTIVE peer/client/seat/full actor/epoch once; duplicate skips; dedicated hook once; accepted counter commits only success. **Native ordinal is intentionally one-based**, unlike literal donor zero-based Peer.sequence. Raw serverTime remains a separate full-width word.

Application `qa_application_network_q3_command` qualifies canonical selected movement separately, then true primary native/original physical source, then calls actual movement-owned raw intake. `qa_application_network_q3_enter` seeds the exact first usercmd before genuine canonical Begin. Native seed writes retained wire command/received only; original seed writes actual guest client.command only. No new history/ordinal/ACK/Think. Actual server receive excludes that first word from the ordinary later Think loop. Initial and restored frontend runtime options install the same dedicated hook. Capture/post-PREDICTION restore/round completion/live publication compare actual frontend ordinal against actual runtime accepted owner.

Outside this worker, movement added native queued/deferred raw Q3 and genuine original synchronous raw Think under `qa_session_command_call` plus original arsenal context. The original async-order counterexample is source-confirmed repaired: subsequent reliable commands no longer precede previous original Think through the native queue. Its complete context/codec/source packet acceptance belongs to movement/root. Original axes include genuine -128; do not route through the old generic axis clamp or call guestThink without canonical mixed-role context.

Native seed dependency: `application_native_q3_wire_command_seed` in native_q3_wire_state.c/.h; real physical full-generation pre-Begin source binding, no pendingDROP/round, no Think/history. Original seed dependency: `application_q3_guest_client_enter_command` in guest_q3_clients.c/guest_q3_restart.h, exact initialized connected nonbot/notbegun source GAME slot. Private source owners preserve seeded commands at sequence0. Full seed-owner review belongs to their owners, not just this glue acceptance.

### Primary Q3 host adapters and physical callbacks

Earlier accepted manifests: `/tmp/qa-network-native-q3-host-adapters-20260930.sha256`, `/tmp/qa-network-native-q3-source-admission-20260930.sha256`, `/tmp/qa-network-q3-host-observation-20260930.sha256`. They are earlier cuts of files subsequently superseded by accepted raw Q3 caller3; use current files/hashes, not stale aggregate manifests.

Real native primary GAME support reads qa_q3_round_read, actual physical qa_q3_native_client_slot, retained native wire admission/userinfo, genuine source product/max/configstrings and source EXIT clock. It does not require canonical Character ownership or Character source slot equality. Actual remote map admission derives physical request namespace from GAME and canonical Character address independently (map_players owner fixed it).

`qa_application_network_q3_client_bound` is a **callback-safe pure binding getter**, including pending DROP, and is used by ordinary frontend server_send_command/server_drop_client. It works while actual source wire calls are held; do not replace it with idle-only full source observers. Actual physical source binding upstream qualifies recipient. Network peer is the sole real remote reliable transport/ACK owner. Native/guest retained source queues are only genuine bot or concrete local CGAME reader owners; no GAME pers.local_client, gamestate, seat, UI or IP proxy, and no second unacknowledged remote queue.

Original guest host still explicitly qualifies its complete original ABI/composition. Some foreign mixed original hosts remain unsupported; native primary source support does not remove those original source gates.

Baselines/snapshots share real physical source readers: connected before Begin, pure linked physical baseline capture without ClientThink/signon/cvar replay, actual BSP/PVS, genuine raw source words/legacy ABI, source snapshot bit and EXIT clock. Native original wire producer acceptance is a separate q3_wire/source owner packet. Do not reconstruct pure baselines from serialized snapshot history or count local seats to infer physical native source clients.

### Q3 frontend continuation and round cut

`frontend/network.c` QANF version5 retains actual source admission owner, peer generations/seat/physical slot/product/qport/rate/ordinal/connection time/world IDs/checksum feed, pending connectionless rows, real remote CGAME client projection/clock/usercmd/presentation identity, actual source round state. Round captures complete occupied roster/source raw userinfo and real last usercmd, refreshes only actual delivery histories, preserves native reliable/channel/history owners, distinguishes restarted server ID, and flips actual snapshot bit at the real mutation marker. Map/fast restart caller closure is owned by frontend round/application lifecycle peers.

Generic native CONNECTIONS QANC version1 and PREDICTION are separate owners. Isolated detached frontend/network candidates decode full enclosing stream before real source hook/admission/native peer construction. Browser/admin/download state imports are real producers; accepted command counters qualify only after PREDICTION restored. Final readonly publication compares actual live protocol/host/accepted cuts and refuses rewind when external original peers advanced. Final context/transport exchange rebinds borrowed owners and sole live transport; no replay/spawn/signon/send during import. Full frontend aggregate acceptance belongs to its owner.

### QuakeWorld runtime/source Number history

Original accepted runtime10 manifest `/tmp/qa-network-qw-runtime-source-20260930.sha256` is historical: its runtime header/q1_qw.c were superseded by accepted Number/history4, and source header/TU subsequently by accepted nails2. Other native channel/save/rebind paths remain real accepted dependencies. Previous `/tmp/qa-network-qw-source-number-history-20260930.sha256` preserves the accepted four-file Number migration cut; current source header/TU hashes are nails2.

Current runtime header remains `1f4351e80683c3eddfddf9a9b760ea92cfb10c401e956dffcb9127a6ea34d04e`; runtime/q1_qw.c remains `1ff6fa2b991fcd6681d43bde902f32a839eb0bd94547fb2c95988d10a50dcf65` from Number/history4.

Actual QW runtime owns one native source seat, qport/rebind, sole receive pump, native signon, complete ordered reliable FIFO, rate, paused/loss/choke/reply/retirement/input source sequence, raw bundle replay and requested frame history. `qa_network_attach_qw_server`, `_reliable`, `_baselines`, `_frame`, `_drop`, `_state_read`, `_rate`, `_view` are actual public owners; installed source factory/caller remains absent.

Input recovers actual oldest/previous/current commands with genuine dropped replay and literal same packet sequence; paused source replay emits no group. Atomic `qa_network_command_group` separately authenticates selected movement, carries1..20 raw QW commands with shared sequence, invokes hooks.commands once and commits accepted history only success. Manifest `/tmp/qa-network-qw-command-groups-20260930.sha256` is an earlier header/commands cut, superseded by current raw Q3 runtime2.

Server frames now use `qa_qw_source_history`, not a decoded-client shadow decoder. Raw finite Math.trunc Numbers precede byte narrowing, delta/presence decisions and stat opcode selection; scalar4294967296 produces long-stat opcode with word0, effects256 retains presence then byte0. Source vectors remain actual binary32, with following binary64 coordinate/angle/delta arithmetic. MOREBITS-before-SOLID exactly follows donor QW28. Generic decoded-wire authority remains separate/unmodified.

Real source history stores only a complete included/transmitted unreliable frame; reliable-only/overflow failures invent no frame. Baseline replacement stages complete input before replacement/reset. Retained source frames are ordered, correct modulo64 slots/age, and strictly precede actual channel next outgoing, including genuine exhausted INT32MAX+1. Actual nested native rate must equal retained outer500..10000 policy. QAWS schema2 stores the true source owner; source history schema1 stores five f64 scalars + six f32 vector words + source physical identity/solid. No current-pose or fake frame reconstruction on restore.

### Installed NetQuake frontend and history

Manifest `/tmp/qa-network-nq-ping-history-20260930.sha256` remains current:

```
437bdfdd97face3f8afee6941bbabc779e30b5e33f3fd943af01e4c82d6d72e5  src/app/frontend/network_nq.c
778daeb740556aad6235edaa71a90113c122fba925b253f1adcd909d0b26557d  src/app/frontend/network_nq_private.h
24c2f3b51c7465e2ca4ec603e6528f4580545dae85162bf19875c4f74a23ecfd  src/app/frontend/network_nq_save.c
```

Actual classic QC NQ15 host exists: native control admission/query queue, per-peer captured source baseline, source precaches/stamped signon/stages/real Begin, retained latest command/impulse, source elapsed/subms tick before actual advance, no clock from ACK/packet count, true fat-PVS/linked source bounds, source feedback, reliable batching/datagram order, source status/name/color/frags/lightstyles caches, chat/kill/pause and actual retirement safe point. This host still deliberately enters the selected matching classic QC source; full mixed and builtin native wire pools remain separate.

QANH version3 owns64 peers,32 pending queries, all real retained baselines/text/status float/integer/color/style caches, actual input/tick counters, entered/admission order, latest16 real receipt pings, retained latest published source EXIT clock, source pause observer and known signon/status NAME cache. Mean uses present entries only, truncation/negative clamp follows donor. Number spelling uses actual public finite JS serializer, bounded non-NUL bytes, +zero normalization; all later failures release temporary owner. History/receipt order survive real travel. Actual runtime input/channel/signon/FIFO and full frontend/application/native roster bijection are retained; final cut refuses live rewind.

NQ remaining: observe typed NAME inside actual opaque source-service batches (known native signon/status name history only is accepted); complete operator/command/query behavior, local chat presentation, extended profiles, mixed source wire and Q1 remote presentation clients. Earlier NQ runtime/frontend/connections/state/chat/kill/pause manifests are historical superseded cuts; the ping trio above is current.

## Missing QW installed producers and concrete next tasks

No complete factory owner was assigned before freeze. `frontend_resume` explicitly owns source.c/Q3 services and frontend aggregate, **not** exclusive network.c/QW host implementation. The raw group hook belongs to this network worker's frontend/network.c. Earlier references to frontend_resume installing the QW factory/hook were corrected. Assign one exclusive QW factory/publisher/continuation owner on resumption, separate source action/control owner and independent source reviewer; do not split a single mutable host/cache owner across concurrent writers.

1. After observer checkpoint/release, add `qa_application_network_qw_commands(app,const qa_network_command_group*,error)` in the QW app header/TU. Authenticate canonical remote selected `group.movement` separately, qualify genuine primary physical QW source/actor, then call actual `qa_application_control_qw_commands` with unchanged raw group. Its public application declaration is now on disk. Install actual hooks.commands in both initial and saved runtime options in frontend/network.c. Existing generic selected control input is not a raw foreign QW producer.
2. Implement actual installed QC QW28 host factory in a real frontend QW TU/private owner. Current frontend create/restore admits only Q3_68 and classic NQ15 hosting, Q3 remote connect; it explicitly rejects QW. Use new actor-independent source world/cvars/signon/map bytes, real native checksum producer, actual32 physical source membership, configured active-player/spectator policy, trusted spectator raw info before callbacks, canonical QAPR remote generation/seat and genuine deferred source reserve.
3. Produce missing genuine QW engine registry rows in the QC constructor: donor quakeworld-cvars.ts registers password="", spectator_password="", sv_highchars="1", maxspectators="8"/SERVERINFO. Current constructor has physics cvars but none of those four. Factory itself registers real allow_download/skins/models/sounds/maps defaults1 as donor; distinguish constructor/factory producer ownership. No synthesized absent policy.
4. Capture immutable initial source baselines once at genuine source factory/admission cut: actual modeled OWNED dynamic source rows, donor effects0, source client defaults1..32 with actual precached player model/slot colormap. Build native signon buffers from actual baseline and stamped persistent source services; do not take later moving poses or use raw guessed builtin precaches. Native Q1 QW clock/reservoir membership alone is **not** native QW wire eligibility; missing native physical row/precaches/raw ABI producer remains explicit.
5. Actual source action lane must retain/reconstruct typed pending actions and full client/seat/actor/epoch source admission, execute strictly after genuine QW EndFrame with true reliable pre/post flush order. Include spawn/Prepare before Begin, pause/kill/setinfo and ordinary signon commands. Do not call actions immediately from parser or restore callbacks. Existing legacy q1 kill/pause/name paths select Character and some require record.character==QC; they cannot serve mixed primary-QC QW unchanged. Need actual source-qualified adapters plus canonical raw userinfo setter; no fake command replay.
6. Raw foreign QW intake helper/projection exists in movement's draft/packet, with same literal1..20 group and true EndFrame drain; its complete context/codec acceptance belongs to movement/root. Real network hook/caller and actual after-EndFrame source actions are still absent. Read exact current movement packet before integrating; preserve source slices/QC callbacks and convert only through actual selected movement, never relabel raw QW as foreign raw physics.
7. Build actual immutable frame publisher: source player physical states/flags, real last raw command/time/msec, source stats, source float frags, lightstyles, raw userinfo/setinfo/pause caches, PVS/linked bounds; source dynamic entity cap64 and nails cap32. Use Number-aware source player/stat/entity/nail writers before wire narrowing. Maintain real per-peer raw stat change cache, not int32-only history; retain actual full scalar difference/opcode. Drain actual source routed services/events in source order/reliability/recipient domain. Header codecs alone are not publication.
8. **New source-only timestamp counterexample, unreviewed/unfixed at handoff:** control_frame.c qa_application_control_qw_commands currently sets input.source_time_ns = `clock.frame.time_ns`, and application_control_last_qw_command returns that. Between frames it is completed session EXIT. Donor wire client.commandTime is `game.timeSeconds`, the distinct retained QC ENTRY clock. A future publisher subtracting that getter time from source.source_time_ns will be one frame off/possibly negative. Preserve scheduler admission time independently and add a real QC wire commandTime producer/caller/private owner; do not rewrite clocks to force equality or infer time from transport. Root and movement were notified. No code changed for this finding.
9. Implement actual connectionless QW handshake/challenge/query/status/ping/master/chat-flood/ban/rcon/auth/rate/download callbacks through the runtime's sole receive transport. Preserve real userinfo/name/team and actual query/private/admission counting; no second poll/socket. Actual QW service codec helpers do not establish these source producers.
10. Add enclosing portable QW frontend owner before installed save acceptance: full peer/source/baseline/stat/name/style/pause/raw command/time/retirement/query/action/download cache inventory, full typed decode before real source callback/native constructor, candidate-only qualification, current source/roster bijection, source_qw checkpoint ref and actual installed QW branch. Current network_saved_refs has no source_qw producer and QANF has no qw_host field. Final live cuts need real source/native channel/source history/accepted/frontend cache/command cuts and no-fail context/sole-transport exchange. Never serialize fake absence for installed QW or reconstruct history/current pose.

## Other remaining network gaps and external source dependencies

- Q3 pure: current native/original world/signon/round APIs explicitly reject real sv_pure without complete live package/reference/checksum/auth metadata. Native common cvar default sv_pure1 or user-provided package strings is not a metadata producer. Need genuine package inventory/reference source and native pure admission/continuation, not fabricated empty pools or a backend fallback.
- Q3 hosted package download currently emits real explicit denial. Remote download_size/download reject unsolicited package transfer without admitted exact identity. Generic URL/digest/size downloads are separate supported service jobs, not Q3 native package interoperability.
- Q3 remote presentation client source getter still requires actual matching external CGAME owner; native CGAME/UI/wire producer/geometry/roles/prediction holders have separate source packets. Complete installed remote original/native prediction must retain genuine client VM/player prediction state, rather than claiming generic runtime PREDICTION is enough.
- Q3 ordinary query/getinfo/status private-slot accounting had preexisting mismatches: status excludes connected notbegun rows; getinfo private-client accounting differs donor. Earlier host adapter review explicitly kept broader query parity open. Do not label unchanged query code as covered.
- Native host serverinfo/systeminfo source API writes true source CS0/CS1 from actual registry, real source sv_serverid/restartedserverid/snapshotbit/fs_game actual product. Source gameplay round/load/mode/cached cvar producers and common startup registration belong to round/console owners. No source cvar replay or cached bootstrap simulation.
- Browser/admin native state and complete bounded save/import exist, with actual detached candidate hooks and real live transport at final exchange. Console-command registration, web HTTP pending state and frontend aggregate consumer/source ownership require their whole source acceptance. Existing manifests include services, console-owner, frontend-continuation, accepted-sequence, runtime/server continuation. Do not infer actual QW/Q2 protocol queries or end-to-end discovery from native libraries alone.
- Generic downloads own bounded jobs/windows/pending buffers/admission/stages and private continuation; frontend supports explicit URL + digest + bounded size, inspect actual installed map/package, source exact mounted/remount identity and safe stage namespace. Missing mounted/native source download equivalence, Q3 package admission and source-specific QW transfer remain distinct. Manifest families download-jobs/window/consumer and generic service continuation are earlier accepted cuts; requalify actual current consumers when integrating.
- Actual source field and gameplay owners: native_q3_wire.c/.h and native_q3_wire_state.c/.h (q3_wire owner), q3 physical player/entity producer (q3 source owner), original guest_q3_{clients,restart,wire,...} (native_resume), map_players.c actual GAME-vs-Character namespaces (round owner), guest_qc lifecycle8 and INFOKEY/raw owner semantics (qc_resume), control_frame.c/arsenal_guest.c/control codec/public ingress (movement/root), native Q1 reservoir/source input (q1 owner). Their exact frozen manifests and aggregate dispositions come from those owners; network glue acceptance is not their complete review.
- Source QW fields retain finite-source/high-water33..512 domain; no general declared-mod ABI or extended-wire acceptance was added. True simultaneous mixed gameplay for builtin Q1/Q2/Q3/re-releases/expansions/mods still requires source-qualified producers beyond these bounded network contracts.

## Most useful behavioral/source references

- TypeScript donor `src/app/bootstrap/simulation/network-qw.ts`: actual classic QC factory, baseline construction, stats/flag/PVS/nails production, source actions, admitted role/raw userinfo and commandTime.
- `src/app/bootstrap/network/qw-server.ts`, `qw-server-types.ts`: actual native replay, commandPhase, signon/channel/frame store/choke/rate/queries/chat/ping/masters/restart.
- `src/network/q1/codecs/qw28.ts`, `message.ts`, `quakeworld.ts`, `qw-constants.ts`: actual field/opcode/Number/wire narrowing/bit order.
- `src/app/bootstrap/simulation/quakec-source.ts`, `quakeworld-cvars.ts`; QC entity-host/source-slots and original sv_user receive/run source: real Prepare/Begin/disconnect/lifetime/time and source ABI.
- `src/app/bootstrap/simulation/network-q3.ts`, actual original Q3 GAME/CGAME sources and source wire owners: raw synchronous original vs queued native intake and admission/command/snapshot truth.

Historical per-unit source manifests remain in `/tmp/qa-network-*.sha256`. Many share files or report paths and are intentionally superseded; use the current frozen hashes above plus owner manifests, not blanket old-manifest success. Root's combined handoff should carry broader project context and exact integration status separately. This file intentionally omits repository operations.

