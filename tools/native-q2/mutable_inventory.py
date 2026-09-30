"""Inventory actual mutable image roots and unresolved compiler coverage for source audit."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import struct
import tempfile
import uuid

from pdb_inventory import InventoryError, MAX_BYTES, MSF, PE, Types, data_symbols, need, read_file
from dwarf_inventory import DWARF, ELF, cstring


MAX_OBJECTS = 262144


def extent(start: int, size: int) -> tuple[int, int]:
    need(0 <= start < (1 << 64) and 0 < size <= (1 << 64) - start,
         "mutable compiler span has no bounded positive extent")
    return start, start + size


def overlap(a: tuple[int, int], b: tuple[int, int]) -> bool:
    return a[0] < b[1] and b[0] < a[1]


def subtract(area: tuple[int, int], exclusions: list[tuple[int, int]]) -> list[tuple[int, int]]:
    result, current = [], area[0]
    for start, end in sorted(exclusions):
        if end <= current or start >= area[1]:
            continue
        if start > current:
            result.append((current, start))
        current = max(current, min(end, area[1]))
    if current < area[1]:
        result.append((current, area[1]))
    return result


def wire_span(area: tuple[int, int]) -> dict:
    return dict(rva=area[0], byteLength=area[1] - area[0])


def contained(area: tuple[int, int], ranges: list[dict]) -> bool:
    return any(row["rva"] <= area[0] and area[1] <= row["rva"] + row["byteLength"]
               for row in ranges)


def gaps(ranges: list[dict], objects: list[dict]) -> list[dict]:
    spans = [extent(row["rva"], row["byteLength"]) for row in objects
             if row.get("compilerExtentQualified")]
    return [wire_span(part) for row in ranges
            for part in subtract(extent(row["rva"], row["byteLength"]), spans)]


def pe_ranges(pe: PE) -> dict:
    offset = int.from_bytes(pe.data[60:64], "little")
    optional_size = int.from_bytes(pe.data[offset + 20:offset + 22], "little")
    optional = pe.data[offset + 24:offset + 24 + optional_size]
    need(len(optional) >= 60, "PE optional header omits actual image extent")
    image_bytes = int.from_bytes(optional[56:60], "little")
    need(image_bytes > 0, "PE image has no actual positive mapped extent")
    sections, mutable, excluded = [], [], []
    for index, row in enumerate(pe.sections):
        raw = pe.section_bytes[index * 40:(index + 1) * 40]
        name = raw[:8].split(b"\0", 1)[0].decode("ascii")
        size = max(row["virtualSize"], row["rawSize"])
        need(not row["rawSize"] or (row["raw"] <= len(pe.data) and
             row["rawSize"] <= len(pe.data) - row["raw"]),
             "PE linked section raw extent exceeds actual artifact bytes")
        section = dict(index=index + 1, name=name, rva=row["rva"], byteLength=size,
                       virtualBytes=row["virtualSize"], rawBytes=row["rawSize"], flags=row["flags"])
        sections.append(section)
        if size:
            area = extent(row["rva"], size)
            need(area[1] <= image_bytes, "PE linked section exceeds actual SizeOfImage")
            if row["flags"] & 0x80000000 and not row["flags"] & 0x20000000:
                mutable.append(area)
            else:
                excluded.append(area)
    directory = 96 if pe.pointer_bytes == 4 else 112
    count = int.from_bytes(optional[directory - 4:directory], "little")
    need(count <= (len(optional) - directory) // 8, "PE data directories exceed actual optional header")
    preferred = int.from_bytes(optional[28:32] if pe.pointer_bytes == 4 else optional[24:32], "little")
    tls = None
    if count > 9:
        address, size = struct.unpack_from("<II", optional, directory + 9 * 8)
        need(bool(address) == bool(size), "PE TLS directory has an incomplete actual extent")
        if size:
            need(address <= image_bytes and size <= image_bytes - address,
                 "PE TLS directory declared extent exceeds actual SizeOfImage")
            width = pe.pointer_bytes
            need(size >= width * 4 + 8, "PE TLS directory omits actual ABI fields")
            record = pe.rva(address, width * 4 + 8)
            values = [int.from_bytes(record[at:at + width], "little") for at in range(0, width * 4, width)]
            start, end, index, callbacks = values
            need(start <= end and ((not start and not end) or start >= preferred),
                 "PE TLS template VAs do not belong to the preferred image")
            tls = dict(directoryRva=address, directoryBytes=size, preferredImageBase=preferred,
                       templateStartVA=start, templateEndVA=end, indexVA=index, callbacksVA=callbacks,
                       zeroFillBytes=int.from_bytes(record[width * 4:width * 4 + 4], "little"),
                       characteristics=int.from_bytes(record[width * 4 + 4:width * 4 + 8], "little"))
            if end > start:
                template = extent(start - preferred, end - start)
                need(any(row["rva"] <= template[0] and template[1] <= row["rva"] + row["byteLength"]
                         for row in sections), "PE TLS template exceeds actual linked sections")
                tls["template"] = wire_span(template)
                excluded.append(template)
    ranges = [wire_span(part) for area in mutable for part in subtract(area, excluded)]
    return dict(imageBytes=image_bytes, sections=sections, mutableRanges=ranges, tls=tls,
                excludedRanges=[wire_span(area) for area in excluded])


def pe_inventory(artifact: bytes, debug: bytes) -> dict:
    pe, pdb = PE(artifact), MSF(debug)
    info = pdb.stream(1)
    need(len(info) >= 28 and int.from_bytes(info[:4], "little") in
         {20000404, 20030901, 20091201, 20140508} and info[12:28] == pe.guid and
         int.from_bytes(info[8:12], "little") == pe.age,
         "PDB GUID/age differs from artifact RSDS")
    types = Types(pdb.stream(2))
    symbols = data_symbols(pdb, pe, pe.age)
    image = pe_ranges(pe)
    objects, closure = [], {}
    cache = {}
    for symbol in symbols:
        if not symbol["writableNonExecutable"]:
            continue
        need(len(objects) < MAX_OBJECTS, "excessive mutable compiler objects")
        row = dict(symbol, compilerExtentQualified=False)
        target = symbol["type"]
        if target not in cache:
            try:
                cache[target] = (types.extent(target), types.closure([target]), None)
            except InventoryError as error:
                cache[target] = (None, [], str(error))
        size, facts, failure = cache[target]
        if failure:
            row["unresolved"] = failure
        else:
            row["byteLength"] = size
            area = extent(row["rva"], size)
            if size > row["sectionBytesRemaining"]:
                row["unresolved"] = "compiler object extent exceeds its linked virtual section"
            elif not contained(area, image["mutableRanges"]):
                row["unresolved"] = "compiler object overlaps excluded or unqualified image storage"
            else:
                row["compilerExtentQualified"] = True
                for fact in facts:
                    closure[fact["id"]] = fact
        objects.append(row)
    return dict(format="pe-pdb", machine=pe.machine, pointerBytes=pe.pointer_bytes,
                pdbSha256=hashlib.sha256(debug).hexdigest(), guid=str(uuid.UUID(bytes_le=pe.guid)),
                age=pe.age, embeddedPdbPath=pe.pdb_name, image=image, objects=objects,
                types=[closure[index] for index in sorted(closure)],
                rangesWithoutQualifiedCompilerObjects=gaps(image["mutableRanges"], objects),
                unresolvedSymbolKinds=["CodeView TLS", "public-only", "unsupported data records"])


def elf_ranges(elf: ELF) -> dict:
    mutable, excluded, tls = [], [], []
    for index, row in enumerate(elf.segments):
        if not row["bytes"]:
            continue
        area = extent(row["address"], row["bytes"])
        if row["kind"] == 1:
            if row["flags"] & 2 and not row["flags"] & 1:
                mutable.append(area)
            else:
                excluded.append(area)
        elif row["kind"] == 0x6474E552:
            excluded.append(area)
        elif row["kind"] == 7:
            tls.append(dict(index=index, **wire_span(area), flags=row["flags"]))
            excluded.append(area)
    sections = []
    for index, row in enumerate(elf.sections):
        if not row["flags"] & 2 or not row["bytes"]:
            continue
        area = extent(row["address"], row["bytes"])
        sections.append(dict(index=index, name=row["name"], **wire_span(area),
                             flags=row["flags"], kind=row["kind"]))
        if row["flags"] & 0x400:
            excluded.append(area)
    ranges = [wire_span(part) for area in mutable for part in subtract(area, excluded)]
    return dict(segments=elf.segments, sections=sections, mutableRanges=ranges, tls=tls,
                excludedRanges=[wire_span(area) for area in excluded])


def elf_objects(elf: ELF) -> list[dict]:
    table = elf.named[".symtab"]
    need(table["kind"] == 2 and table["entryBytes"] == 24 and table["bytes"] % 24 == 0 and
         table["link"] < len(elf.sections), "unsupported ELF object symbol table")
    strings = elf.sections[table["link"]]
    need(strings["kind"] == 3, "ELF symbols have no linked string table")
    strings = elf.section_data(strings)
    result = []
    for symbol_index, raw in enumerate(elf.span(table["offset"], table["bytes"], 24)):
        name, info, visibility, section, address, size = struct.unpack("<IBBHQQ", raw)
        kind = info & 15
        if kind not in {1, 6} or not section:
            continue
        need(len(result) < MAX_OBJECTS and section < len(elf.sections),
             "excessive or absolute/extended ELF object symbols")
        owner = elf.sections[section]
        if not owner["flags"] & 2:
            continue
        need(address >= 0, "ELF object address is invalid")
        row = dict(name=cstring(strings, name), symbolIndex=symbol_index, symbolKind=kind,
                   binding=info >> 4, visibility=visibility, section=owner["name"],
                   sectionIndex=section, byteLength=size, compilerExtentQualified=False)
        if kind == 6 or owner["flags"] & 0x400:
            row.update(tlsOffset=address, unresolved="TLS object needs actual per-thread ownership and relocation")
        else:
            need(owner["address"] <= address and size <= owner["bytes"] - (address - owner["address"]),
                 "ELF object symbol exceeds actual allocated section")
            row["rva"] = address
            if not size:
                row["unresolved"] = "ELF object has no positive compiler extent"
        result.append(row)
    return result


def elf_inventory(artifact: bytes) -> dict:
    elf = ELF(artifact)
    dwarf = DWARF(elf)
    image = elf_ranges(elf)
    objects = elf_objects(elf)
    by_address, definitions, roots = {}, [], []
    for index, row in dwarf.rows.items():
        if row["tag"] != 0x34:
            continue
        location = dwarf.attribute(index, 0x02)
        if not location or location["kind"] not in {"expression", "block"}:
            continue
        expression = bytes.fromhex(location["value"])
        if len(expression) != 9 or expression[0] != 3:
            continue
        address = int.from_bytes(expression[1:], "little")
        if not any(area["rva"] <= address < area["rva"] + area["byteLength"]
                   for area in image["mutableRanges"]):
            continue
        need(len(definitions) < MAX_OBJECTS, "excessive mutable compiler definitions")
        field = dwarf.attribute(index, 0x6E) or dwarf.attribute(index, 0x03)
        definition = dict(die=index, unit=row["unit"], rva=address,
                          name=field["value"] if field and field["kind"] == "string" else None,
                          compilerExtentQualified=False)
        target = dwarf.attribute(index, 0x49)
        try:
            need(target is not None and target["kind"] == "reference",
                 "mutable compiler variable has no actual referenced type")
            size = dwarf.extent(target["value"])
            definition.update(type=target["value"], byteLength=size)
            need(contained(extent(address, size), image["mutableRanges"]),
                 "mutable compiler variable overlaps excluded or unqualified image storage")
            definition["compilerExtentQualified"] = True
            roots.append(index)
            by_address.setdefault((address, size), []).append(index)
        except InventoryError as error:
            definition["unresolved"] = str(error)
        definitions.append(definition)
    for row in objects:
        if "rva" not in row or not row["byteLength"]:
            continue
        area = extent(row["rva"], row["byteLength"])
        if not contained(area, image["mutableRanges"]):
            row["unresolved"] = "ELF object is outside ordinary mutable image storage"
        elif (row["rva"], row["byteLength"]) in by_address:
            row["definitions"] = by_address[(row["rva"], row["byteLength"])]
            row["compilerExtentQualified"] = True
        else:
            row["unresolved"] = "ELF object has no exact embedded compiler type/address extent"
    return dict(format="elf-dwarf", machine=62, pointerBytes=8, image=image, objects=objects,
                definitions=definitions, units=dwarf.units, dies=dwarf.closure(roots),
                rangesWithoutQualifiedCompilerObjects=gaps(image["mutableRanges"], definitions),
                unresolvedSymbolKinds=["TLS", "zero-sized", "public-only", "unsupported location expressions"])


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--artifact", type=Path, required=True)
    parser.add_argument("--pdb", type=Path)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    try:
        for source in (args.artifact, args.pdb):
            if source is not None:
                need(args.out.resolve() != source.resolve() and
                     (not args.out.exists() or not os.path.samefile(args.out, source)),
                     "mutable inventory output must not replace artifact/debug input")
        artifact = read_file(args.artifact)
        if artifact[:2] == b"MZ":
            need(args.pdb is not None, "PE mutable inventory needs its exact PDB partner")
            result = pe_inventory(artifact, read_file(args.pdb))
        else:
            need(artifact[:4] == b"\x7fELF" and args.pdb is None,
                 "ELF mutable inventory uses actual embedded compiler facts")
            result = elf_inventory(artifact)
        result.update(schema=1, kind="artifact-mutable-root-inventory",
                      artifactSha256=hashlib.sha256(artifact).hexdigest(),
                      semanticCoverage="requires-original-source-audit")
        text = json.dumps(result, indent=2, sort_keys=True) + "\n"
        need(len(text.encode("utf-8")) <= MAX_BYTES, "mutable compiler facts exceed bounded output owner")
        temporary = None
        try:
            with tempfile.NamedTemporaryFile(mode="w", encoding="utf-8", dir=args.out.parent,
                                             prefix=args.out.name + ".", delete=False) as output:
                temporary = Path(output.name)
                output.write(text)
            os.replace(temporary, args.out)
            temporary = None
        finally:
            if temporary is not None:
                temporary.unlink(missing_ok=True)
    except (InventoryError, OSError, UnicodeError, struct.error) as error:
        parser.exit(2, f"native Q2 mutable compiler inventory: {error}\n")


if __name__ == "__main__":
    main()
