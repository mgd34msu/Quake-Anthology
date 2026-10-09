# THE-869 QVM command conversion

`guest_q3_mod_input.c` uses the common command units and basis converter for
normalized axes and changed module view angles. Its copied `/127` and
word/delta/wrap conversion are deleted. VM field access, original command bytes
and changed-value publication retain their existing meaning.

Eight original/candidate component executions under GCC, Clang and
ASan/UBSan/float-cast-overflow produce identical 6,440,048-byte outputs per pair.
They exercise 65,536 wrapped words, signed extremes and every signed movement
byte under four floating-point rounding modes, plus unchanged/changed command
publication. The actual scalar and command callbacks run with controlled VM
memory and player-state services. This is not a full guest gameplay run.

The isolated source slice passed production, ASan/UBSan and allocation-gate
builds and the seven configured core checks in each, from frozen source
`66e6261e`. Evidence: `/tmp/qa-the869-qvm-command-adoption-20261009/receipt.json`
and `/tmp/qa-the869-qvm-adoption-build-20261009`.

No failure path, game rule, wire width, frame allocator or context layer was
added. No frame-time improvement or installation is claimed. Remaining
command-space/ABI copies and KEX float-angle fidelity are in
`docs/core-adoption.md`; THE-869 stays In Progress.
