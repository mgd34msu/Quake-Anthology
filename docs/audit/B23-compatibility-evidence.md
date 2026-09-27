# Source evidence for B23

Snapshot: HEAD 2a7d5a1397ea169c963b23593ba84c360a926ab2 with existing dirty source; plan 6, original graph docs/dependencies.json SHA256 915c4c50da8fcb60ed206f09f2d325eb7755f4c6f3f48b1c3d97d83d2eb98bae. This packet does not change the goal or criterion. Full assigned file coverage and finding history: docs/audit/compatibility.md. Source reads and targeted donor/reference comparisons only; no configure, compiler, test, parser, executable or benchmark run. Judge the implementation against the original source criterion, not whether this report honestly states incompleteness.

src/compat/qvm supplies image decoding, interpreter, private RAM, memory observers, role/profile classification, common intrinsics, typed records, compatibility declarations and checkpoints. module.c:15 qa_qvm_create builds the private instance. intrinsics.c:141 classifies modern and 1.16n traps.

The decisive host boundary is intrinsics.c:212: if vm->options.syscall exists, it is called; otherwise the remaining trap is unsupported. There is no concrete game/cgame/UI engine syscall host or actual gameplay/equipment/combat composition adapter. Repository-wide source search finds qa_qvm_create only at its definition. src/compat/native_host/q3.c delegates to a supplied shared bridge; it does not provide the missing QVM engine host. docs/integration.md records these still-required consumers.

Targeted donor comparison established that the C image magic matches ../quake-typescript/src/compat/qvm/image.ts and source ABI trap distinctions are represented. No missing second image magic is claimed as a defect. Correct interpreter helpers do not implement the missing world/render/audio/input/file/bot/UI trap consumers.

Assessment: B23 remains incomplete at its required shared engine boundary and role/composition integration.

