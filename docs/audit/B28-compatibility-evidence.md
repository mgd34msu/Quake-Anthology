# Source evidence for B28

Snapshot: HEAD 2a7d5a1397ea169c963b23593ba84c360a926ab2 with existing dirty source; plan 6, original graph docs/dependencies.json SHA256 915c4c50da8fcb60ed206f09f2d325eb7755f4c6f3f48b1c3d97d83d2eb98bae. This packet does not change the goal or criterion. Full assigned file coverage and finding history: docs/audit/compatibility.md. Source reads and targeted donor/reference comparisons only; no configure, compiler, test, parser, executable or benchmark run. Judge the implementation against the original source criterion, not whether this report honestly states incompleteness.

Protocol-side implementations exist: q1/session.c:20 qa_nq_signon_receive and 59 qa_qw_signon_create, Q2 messages.c:758 qa_q2_command_replay_run, Q3 peer receive/snapshot helpers in client.c/server.c, and Anthology authenticated command/channel/schema/composition records. Shared movement supplies selected source kernels and continuation primitives.

Repository constructor/callsite search shows no production qa_q2_messages_create, qa_q3_server_peer_create or qa_unified_channel_create consumer outside its implementation. src/main.c implements --help/--version/archive listing/BSP inspection only; CMakeLists.txt:574 links qa_content alone. No application joins authoritative actor commands, selected movement prediction/reconciliation/replay, snapshots, travel, reconnect and full mixed provider identities. Implementing protocol envelopes and callbacks does not implement that gameplay owner.

Assessment: B28 remains incomplete at the exact required session-to-gameplay boundary. The finding concerns missing source, not runtime quality or a request to run the unfinished executable.

