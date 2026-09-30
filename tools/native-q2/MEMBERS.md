# Exact compiler member paths

`member_inventory.py` resolves source-audited named member paths against the
actual artifact's compiler definitions. It reads an exact matched PE/PDB pair
or the artifact's own embedded ELF/DWARF. It does not accept an unrelated SDK
header or a hand-edited JSON layout as compiler evidence.

The source-first gate forbids execution, imports, syntax checks and generation.
After that gate is lifted, a source auditor can request exact compiled paths:

```sh
python3 tools/native-q2/member_inventory.py --artifact /path/to/game_x86_64.so \
  --type gclient_t --path old_pmove.origin --path pers.spawned \
  --out /path/to/client-member-facts.json
```

These are source query examples. No invocation or assertion that a given binary
contains these exact names is claimed. `--pdb` supplies the required partner for
a PE image. `--definition` can select one actual named compiler type ID from an
accepted inventory; it cannot select a type with another compiler name. Without
that selector, ELF output retains every matching actual definition and its
compilation unit. It never silently merges repeated definitions or asserts they
are equivalent.

Each path traverses direct instance members in actual struct/class records.
The output records the enclosing type ID, every member name, local offset,
referenced type and extent, followed by the summed root-relative offset. Every
member's positive compiler extent must fit its enclosing record, and the final
span must fit the actual root. Qualifiers and uniquely resolved PDB forward
types retain their original compiler references in the complete output closure.

DWARF member locations must be explicit nonnegative constants or a complete
single `DW_OP_plus_uconst` expression. The latter adds the exact compiler
constant to the containing object's base as specified by
[DWARF5](https://dwarfstd.org/doc/DWARF5.pdf), sections 2.5 and 5.7.6.
Arbitrary expressions, dynamic locations, bitfield ranges, pointer traversal,
union traversal and virtual/inherited base traversal remain unsupported.
Array elements are not indexed or guessed; a leaf array requires an already
admitted explicit compiler extent. Such cases fail before replacing output.

The PE path preserves the accepted reader's actual RSDS/DBI/linked-section
qualification and the mutable inventory's image/file/TLS extent guards. It
resolves CodeView direct members from their actual field lists and retains the
full selected type closure. The ELF path retains complete raw DIE closure,
attribute/reference provenance and compilation-unit facts. Query count, nesting,
definition/member products and final output size are bounded. Atomic output
cannot replace either input or an existing input path alias.

The result is compiler member evidence, not a runtime declaration. It emits no
private storage class, allocator tag, pointer destination, reconstructed-state
disposition or `original-complete` assertion. The separate original source audit
must establish those semantics, qualify every mutable root and remaining span,
and decide which actual compiler definition applies to each source owner.
Unsupported CRT/TLS/process allocations and missing retail compiler partners
remain genuine runtime admission gaps.

Source review is the only current validation. Actual artifact execution,
malformed member/type/expression boundaries and repeated-definition behavior
remain deferred until the complete baseline source gate is lifted.
