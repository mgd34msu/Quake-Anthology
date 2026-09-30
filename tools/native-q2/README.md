# Native Q2 artifact compiler inventory

`pdb_inventory.py` reads a supplied PE image and its exact MSF 7 PDB partner.
It reports selected compiler types, member offsets, typed data symbols and
linked RVAs. It also records all supported `S_LDATA32`/`S_GDATA32` symbols for
the subsequent source audit. It does not create runtime compatibility metadata.

The source-first baseline gate currently forbids executing this tool, including
syntax checks and sample generation. No output or executable validation is
claimed for this packet.

After that gate is lifted, the intended invocation is:

```sh
python3 tools/native-q2/pdb_inventory.py \
  --artifact /path/to/game_x64.dll --pdb /path/to/game_Shipping_x64.pdb \
  --type gclient_t --type edict_t --symbol game --symbol level \
  --out /path/to/compiler-inventory.json
```

Names must exist uniquely in the actual compiler metadata. These example names
do not assert that a particular retail PDB exposes them.

## Qualification

The tool matches the artifact's RSDS GUID and age to the PDB information stream,
then checks the DBI age/machine and exact linked section headers. OMAP remapping,
stripped debug files, unsupported type field encodings, ambiguous definitions
and missing streams are rejected. Global object spans require an actual compiler
type extent contained in the corresponding linked PE section. Output replacement
is atomic and rejects the artifact/debug input paths and their existing aliases.

The parser supports modern TPI records, field-list continuations, class/struct/
union members, arrays, pointers, modifiers, enums and bitfields. Unknown standalone
type records remain explicitly uninterpreted. Non-instance declarations and
virtual-base facts retain their compiler meaning; they are not assigned invented
object offsets. TLS, function/public-only symbols, external type servers, member
pointer semantics and ELF/DWARF require separate producers before use. Seeing a
PDB member or writable symbol does not prove complete runtime state coverage.

The original reader uses the format facts described by LLVM's
[raw PDB headers](https://github.com/llvm/llvm-project/blob/main/llvm/include/llvm/DebugInfo/PDB/Native/RawTypes.h),
[DBI stream ordering](https://github.com/llvm/llvm-project/blob/main/llvm/lib/DebugInfo/PDB/Native/DbiStream.cpp),
[CodeView type mapping](https://github.com/llvm/llvm-project/blob/main/llvm/lib/DebugInfo/CodeView/TypeRecordMapping.cpp)
and [primitive type identities](https://github.com/llvm/llvm-project/blob/main/llvm/include/llvm/DebugInfo/CodeView/TypeIndex.h).
Microsoft's [CodeView definitions](https://github.com/microsoft/microsoft-pdb/blob/master/include/cvinfo.h)
specify integer leaves and the extra nested-type record. No engine or LLVM parser
implementation is copied into the tool.

## Required next producer

Runtime `primary.continuation.private` admission still requires a source audit
that maps every mutable root and byte, including globals omitted by the original
serializer, live client/edict rows, tagged allocations, callbacks and all pointer
destinations. That audit must supply exact tags, allocation record counts,
reconstructed original-Init state and source ownership. Compiler facts cannot
infer these. Uninterpreted types, unqualified CRT/process allocations, TLS and
missing mutable roots prevent a full declaration. The existing runtime qualifier
continues to reject stock artifacts without that complete declaration.

The available Steam and `/home/buzzkill/q2rets` rerelease x64 images both have
SHA-256 `045d49c53722d9b922caf14f168dd28a97d4c514a6e443a3140560f8668baccd`.
Their embedded RSDS partner is GUID `1746e881-2142-4600-ac82-f5fdafdeecc1`, age 1,
with the original path
`C:\Jenkins\workspace\Quake_I_II_Quake_II_master\kex3_ptah\bin\x64\game_Shipping_x64.pdb`.
The available artifact trees contain no matching PDB. The TypeScript retail
client profile is explicitly an observed partial field profile; modern SDK
headers cannot substitute for the missing retail compiler layout.

Source review must precede any execution. After the full baseline gate is lifted,
verification must include the actual matched PE/PDB pair, malformed/truncated
streams, forward and continued types, symbol extents and input-output alias
protection. Until then the tool is an unexecuted source producer.
