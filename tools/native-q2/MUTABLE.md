# Mutable native Q2 compiler roots

`mutable_inventory.py` extends the accepted PE/PDB and embedded ELF/DWARF readers
with the actual image ranges needed by a complete source audit. It reads the
artifact directly and reports every supported mutable data symbol rather than
requiring an auditor to guess a list of global names.

The source-first gate forbids executing this reader, its imports, syntax checks
or output generation. No output or runtime validation is claimed. After that
gate is lifted, the intended commands are:

```sh
python3 tools/native-q2/mutable_inventory.py --artifact /path/to/game_x86_64.so \
  --out /path/to/mutable-roots.json
python3 tools/native-q2/mutable_inventory.py --artifact /path/to/game_x64.dll \
  --pdb /path/to/exact-game.pdb --out /path/to/mutable-roots.json
```

The PE path requires the artifact's exact RSDS GUID/age, DBI machine/age and
linked section headers. Every supported mutable data record retains its type,
module/object origin and RVA. Positive compiler extents and retained type
closures qualify ordinary image objects. Missing extents, unsupported pointer
semantics and failed closures remain explicit unresolved rows.

PE mutable ranges use the same maximum of virtual and raw section size as
`qa_native_module_mutable_range`. Read-only/executable overlaps and the actual
TLS template are excluded. The TLS directory retains its real preferred-image
VA fields, template extent, zero-fill extent and characteristics. It does not
claim to capture a live thread or admit TLS relocations. CodeView TLS records
and unsupported data-symbol kinds remain outside the admitted object roster.

The ELF path retains allocated sections, load segment flags, GNU RELRO and TLS
template spans. Ordinary mutable ranges exclude read-only/executable load
overlaps, RELRO and TLS sections/templates. TLS symbols retain their actual
thread-relative symbol value, never an invented ordinary image RVA. Defined
ordinary object symbols retain their linked section and exact extent. Every
available DWARF variable with a complete `DW_OP_addr` expression in ordinary
mutable storage contributes its referenced compiler type, actual extent and
complete DIE closure. Repeated definitions and symbol aliases remain distinct.
Exact RVA/extent equality connects linker objects with compiler definitions.

`rangesWithoutQualifiedCompilerObjects` subtracts the union of those exact
compiler extents from the ordinary mutable ranges. These gaps can include
padding, loader data and mutable objects without usable compiler definitions.
An empty gap list proves only byte coverage by compiler objects. It does not
prove that their fields are pointer-free, that callbacks are portable, or that
an allocation is tagged. A qualified compiler extent can still contain an
uninterpreted type or unsupported runtime ownership.

The JSON records `semanticCoverage: requires-original-source-audit`. It emits
no `primary.continuation.private` declaration, `original-complete` flag or
stock-artifact capability claim. A separate audit must classify every retained
root and uncovered span, then qualify complete member layouts, pointer
destinations, actual allocator tags and original-Init reconstruction. CRT and
thread state remain genuine missing producers until that work is complete.

The format evidence is Microsoft's
[PE format](https://learn.microsoft.com/en-us/windows/win32/debug/pe-format),
the ELF ABI's
[program headers](https://refspecs.linuxfoundation.org/elf/gabi4+/ch5.pheader.html)
and the accepted readers' DWARF/CodeView primary references. The runtime's
`src/compat/native/image.c` establishes the existing mutable-range boundary;
this tool does not relax it or modify any runtime source.

Source review must cover interval subtraction, TLS address semantics,
read-only/RELRO overlaps, exact compiler extent matching, aliases, unresolved
roots and atomic output ownership. Actual matched-artifact execution and
malformed input verification remain deferred until the full baseline gate.
