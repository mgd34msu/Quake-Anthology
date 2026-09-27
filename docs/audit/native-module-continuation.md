# Native module continuation gap

Source inspection at `36d7012` confirms that Q3/Quake Live capture in
`src/compat/native/checkpoint.c` requires host callbacks, selects
`QA_NATIVE_CHECKPOINT_HOST_ONLY`, and serializes only that host callback's buffer.
It does not capture the module's mutable data or heap. The runner backend follows
the same path: `child_checkpoint_capture` invokes the local capture routine and
`child_checkpoint` forwards the host part to the parent. This is not a complete
native module checkpoint merely because a host descriptor codec exists.

The public contract in `include/qa/native.h` requires complete guest-visible
continuation for APIs without native save exports. The concrete Q3 host must not
report full native save support by serializing only entity descriptors or raw
native addresses. Such addresses cannot establish a valid fresh-module restore.
QVM memory snapshots and Q2's own save APIs have different state contracts.

B24's original saved-state criterion and B30 remain open. Closing them requires
an actual native module state mechanism, its application restore transaction, and
independent source review before the later P01 execution phase. This finding does
not authorize a default TypeScript CPU-emulation implementation or reduce the
required functionality. No native executable or engine check was run for this
inspection.
