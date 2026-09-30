# Embedded native Q2 compiler facts

`dwarf_inventory.py` reads the embedded DWARF4/5 compilation units and full
symbol table of an actual little-endian ELF64 x64 shared image. Its output is
bound to that complete artifact digest. It neither compiles the original game
nor guesses an SDK layout for a different binary.

The source-first gate currently forbids running this reader, its imports, syntax
checks and generation. The intended command after that gate is lifted is:

```sh
python3 tools/native-q2/dwarf_inventory.py \
  --artifact /path/to/game_x86_64.so \
  --type gclient_t --type edict_t --symbol game --symbol level \
  --out /path/to/embedded-compiler-inventory.json
```

The tool retains compiler DIE offsets, tags, exact attribute forms, child trees,
type references, member-location expressions and compilation-unit roots.
Selection uses each actual DIE's declaration flag; specification/abstract-origin
fallback never inherits `DW_AT_declaration` or `DW_AT_sibling`. A completed
definition can refer to its named forward declaration without becoming an
incomplete declaration itself. Name selection retains every matching actual
type definition; repeated definitions
across compilation units are not silently merged or asserted equivalent. A
selected global must have one actual ELF object identity and an embedded
`DW_OP_addr` definition at that exact RVA. Its referenced compiler byte extent
must equal the linked symbol size. The ELF section, unique actual load segment,
write/execute flags and GNU RELRO range qualify the reported mutable span.

Bounded readers reject truncated streams, duplicate abbreviations/attributes,
broken child trees, invalid references and excessive nesting. ELF extended
numbering, compressed sections, debug relocations, indexed string/address forms,
split/type/supplementary units and unsupported attribute forms remain explicit
unsupported cases. Expression bytes are preserved; arbitrary DWARF expressions
are not evaluated to invent object offsets. Array extents without an explicit
compiler byte size, TLS and external type signatures need separate admitted
producers. Output replacement is atomic and cannot replace the input artifact
or its existing path aliases.

The original implementation follows the published
[DWARF5 format](https://dwarfstd.org/doc/DWARF5.pdf), sections 7.4–7.6 and 7.22,
and the [ELF generic ABI](https://refspecs.linuxfoundation.org/elf/gabi4+/contents.html).
The local `/usr/include/dwarf.h` and `/usr/include/elf.h` confirm numeric format
identities. No compiler or game implementation is copied.

Actual available inputs have been inspected using bounded ELF header and
section-name byte reads, without executing a parser:

| Artifact | SHA-256 |
| --- | --- |
| `/home/buzzkill/q2linux/subprojects/rerelease-game/game_x86_64.so` | `e7e20dcae2257d5529fbd989f1a9067885822cf1187c06121796ba4822e30adc` |
| `/home/buzzkill/q2linux/baseq2/gamex86_64.so` | `3bb0485e7fffff2f12139e59f1c52ea9f25f39d3f275a9c64c1c6d8cdb7d4095` |

Both contain `.symtab`, `.debug_info`, `.debug_abbrev`, `.debug_str`,
`.debug_line_str`, `.debug_loclists` and `.debug_rnglists`. This proves available
debug inputs, not that every attribute uses the reader's supported subset or
that its unexecuted implementation handles either artifact successfully.

Complete runtime metadata still needs an original source audit of every mutable
root and byte, callback and allocation. The available rerelease source, for
example, retains `std::unordered_map` save registries in `g_save.cpp`, an RNG in
`g_main.cpp` and scratch vectors in `m_medic.cpp`, `p_client.cpp`, `g_utils.cpp`
and `ctf/g_ctf.cpp`. Actual Init reconstruction or complete allocator/pointer
qualification must establish each root's semantics. Compiler offsets cannot
prove that a heap is tagged, that a cache is reconstructed, or that a field is
pointer-free. Missing stock full declarations and unqualified CRT allocations
remain rejected by the runtime portable qualifier.

Source review is the only verification performed for this packet. After the
full baseline gate is lifted, verification needs actual embedded debug inputs,
malformed unit/reference/tree boundaries, exact symbol/type extents, repeated
compilation-unit definitions and input-output alias protection before using the
facts in a separately audited declaration producer.
