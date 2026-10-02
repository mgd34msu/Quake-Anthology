# Native Q3 item catalog

An original native GAME may provide `native-q3-items.json` in its own mounted
content. The role retains the real VFS resource and acquisition receipt. Missing
metadata supplies no catalog. QVM layouts and product defaults are not used.

The document requires `version: 1`, `profile: "q3-vmMain"`, `artifactPath`,
`artifactDigest`, `pointerBytes`, and `abi`. The path and digest name the exact
loaded GAME artifact. Pointer width and ABI must match its inspected native
target. `abi` uses `qa_native_abi` values from `qa/native.h`.

`items` requires these fields:

- `source: "live"`.
- `address: {"rva": N}` for an image-resident table, or
  `address: {"globalRva": N}` for a native pointer stored in the image.
- `count: N`, or `count: {"globalRva": N, "maximum": N}` for an int32 count.
- `stride` in bytes and `maximumStringBytes`, a positive terminator bound.
- `fields` containing byte offsets `className`, `pickupName`, `type`, and `tag`.
  Name fields contain native pointers; type and tag contain int32 values.
- Distinct nonnegative int32 `weaponType` and `ammoType` source values.

All RVAs and row fields come from the authored artifact interface. Reads use the
actual native instance's RVA resolution, readable-range proof, pointer width,
and memory/string operations after successful GAME Init. Empty classname
pointers skip empty rows. Actual weapon/ammo rows require a source pickup name.
Other item rows may genuinely have no pickup name.

Weapons preserve source order and the first row for each selection tag. Ammo
comes from an actual declared ammo-type row with the same source tag. Canonical
SDK classname identities are matched by classname; other identities use the
actual native artifact digest and classname. Source tag values are not remapped
to stock weapon numbers. This catalog declares no model handles or capacity.

The catalog owns only immutable layout and derived records. Its declaration
resource stays with the artifact; mutable table bytes remain owned by the native
process. Construction performs no source initialization or table read. A cold
native parent must import that real resource and process before catalog reads.
The original-provider codec records an immutable native module cache and a
separate owned process/HOST capsule and external capability recipe for every
committed role. Import stages CPU/RAM first, decodes the HOST bridge against the
restored world, and validates this table before source publication. An actual
uncommitted role retains only its constructor services and capability recipe.
Neither route calls GAME Init or replays native loader constructors. Direct and
runner executors have no owned-process capsule and remain outside this codec.
