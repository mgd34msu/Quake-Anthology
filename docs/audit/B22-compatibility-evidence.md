# Source evidence for B22

Snapshot: HEAD 2a7d5a1397ea169c963b23593ba84c360a926ab2 with existing dirty source; plan 6, original graph docs/dependencies.json SHA256 915c4c50da8fcb60ed206f09f2d325eb7755f4c6f3f48b1c3d97d83d2eb98bae. This packet does not change the goal or criterion. Full assigned file coverage and finding history: docs/audit/compatibility.md. Source reads and targeted donor/reference comparisons only; no configure, compiler, test, parser, executable or benchmark run. Judge the implementation against the original source criterion, not whether this report honestly states incompleteness.

The QC image reader, interpreter, private memory/instances, actor bindings and checkpoint code exist in src/compat/qc. include/qa/qc.h declares source profile requirements and supplied host bindings. Built-in Q1 gameplay is ordinary C under src/gameplay/q1.

At src/compat/qc/builtins.c:135 shared_builtin returns true only for setorigin, setsize, traceline, droptofloor, pointcontents, spawn, remove and findradius when their shared world/session exists. Other required builtins need explicit supplied bindings. instance.c's dispatch rejects unsupported/unbound host imports. Repository-wide source searches find qa_qc_instance_create only at instance.c:242, with no actual complete engine host binding table or gameplay composition consumer. Required sound/model/precaches, commands/cvars, protocol destinations, walkmove/movetogoal, rerelease prompt and bot/path services are therefore not connected by this implementation.

C22-2/3 source defects were corrected and independently reviewed in d728f87: traceline truncates its mode and maps only 1/2 specially; findradius excludes projected solid==0 while refreshing foreign fields and rechecking actor generations. Donor evidence: ../quake-typescript/src/compat/qc/spatial-host.ts:55,69. This closes those defects, not the missing complete host, composition or restore consumer.

Assessment: B22 remains incomplete because required host behavior is absent, not because execution is forbidden.

