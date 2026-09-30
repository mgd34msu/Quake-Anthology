"""Resolve source-audited member paths against an artifact's actual compiler types."""

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
from dwarf_inventory import DWARF, DWReader, ELF, MAX_DEPTH
from mutable_inventory import pe_ranges


MAX_PATHS = 4096


def path_parts(path: str) -> list[str]:
    parts = path.split(".")
    need(0 < len(parts) <= 64 and all(part and len(part.encode("utf-8")) <= 1024 for part in parts),
         "member path needs bounded exact compiler member names")
    return parts


def pe_record(types: Types, index: int) -> dict:
    seen = set()
    while True:
        need(index not in seen and len(seen) < 128, "cyclic compiler record qualification")
        seen.add(index)
        row = types.describe(index)
        if row["kind"] == "modifier":
            index = row["target"]
        elif row.get("forward"):
            matches = types.names.get(row["name"], [])
            need(len(matches) == 1, "member path forward record has no unique actual definition")
            index = matches[0]
        else:
            need(row["kind"] == "record" and row.get("byteLength", 0) > 0,
                 "member path traversal requires an actual struct/class record; unions and pointers need a separate audit")
            return row


def pe_member(types: Types, root: int, path: str) -> dict:
    current, offset, chain = root, 0, []
    root_bytes = types.extent(root)
    for part in path_parts(path):
        record = pe_record(types, current)
        matches = [field for field in types.fields(record["fieldList"])
                   if field["kind"] == "member" and field["name"] == part]
        need(len(matches) == 1, "member path has no unique direct compiled instance member")
        field = matches[0]
        leaf = types.describe(field["type"])
        seen = set()
        while leaf["kind"] == "modifier":
            need(leaf["id"] not in seen and len(seen) < 128, "cyclic compiler member storage qualifier")
            seen.add(leaf["id"])
            leaf = types.describe(leaf["target"])
        need(leaf["kind"] != "bitfield", "bitfield member needs actual bit-range storage audit")
        size = types.extent(field["type"])
        need(field["offset"] <= record["byteLength"] and
             size <= record["byteLength"] - field["offset"],
             "compiled member exceeds its actual enclosing record")
        offset += field["offset"]
        chain.append(dict(record=record["id"], member=part, offset=field["offset"],
                          byteLength=size, type=field["type"], attributes=field["attributes"]))
        current = field["type"]
    size = types.extent(current)
    need(offset <= root_bytes and size <= root_bytes - offset,
         "nested compiled member exceeds its root record")
    return dict(path=path, offset=offset, byteLength=size, type=current, chain=chain)


def dwarf_record(dwarf: DWARF, index: int) -> dict:
    seen = set()
    while True:
        need(index not in seen and len(seen) < MAX_DEPTH, "cyclic compiler record qualification")
        seen.add(index)
        row = dwarf.rows[index]
        if row["tag"] in {0x16, 0x26, 0x35, 0x37, 0x47}:
            reference = dwarf.attribute(index, 0x49)
            need(reference is not None and reference["kind"] == "reference",
                 "qualified compiler record has no actual type reference")
            index = reference["value"]
        else:
            need(row["tag"] in {0x02, 0x13} and not (dwarf.attribute(index, 0x3C) or {}).get("value"),
                 "member path traversal requires an actual struct/class definition; unions and pointers need a separate audit")
            dwarf.extent(index)
            return row


def dwarf_member_offset(dwarf: DWARF, index: int) -> int:
    location = dwarf.attribute(index, 0x38)
    need(location is not None, "compiler member lacks an explicit data-member location")
    if location["kind"] == "constant":
        need(location["value"] >= 0, "negative compiler instance-member offset")
        return location["value"]
    need(location["kind"] in {"expression", "block"},
         "dynamic compiler member locations require a separate proven producer")
    expression = DWReader(bytes.fromhex(location["value"]))
    need(expression.integer(1) == 0x23, "only exact DW_OP_plus_uconst member offsets are admitted")
    offset = expression.leb()
    need(expression.at == len(expression.data), "compiler member expression has additional unevaluated operations")
    return offset


def dwarf_member(dwarf: DWARF, root: int, path: str) -> dict:
    current, offset, chain = root, 0, []
    root_bytes = dwarf.extent(root)
    for part in path_parts(path):
        record = dwarf_record(dwarf, current)
        matches = [index for index in record["children"] if dwarf.rows[index]["tag"] == 0x0D and
                   (dwarf.attribute(index, 0x03) or {}).get("value") == part]
        need(len(matches) == 1, "member path has no unique direct compiled instance member")
        index = matches[0]
        need(dwarf.attribute(index, 0x0D) is None and dwarf.attribute(index, 0x0C) is None and
             dwarf.attribute(index, 0x6B) is None,
             "bitfield member needs actual bit-range storage audit")
        reference = dwarf.attribute(index, 0x49)
        need(reference is not None and reference["kind"] == "reference",
             "compiler instance member lacks an actual type reference")
        target = reference["value"]
        size, local = dwarf.extent(target), dwarf_member_offset(dwarf, index)
        enclosing = dwarf.extent(record["id"])
        need(local <= enclosing and size <= enclosing - local,
             "compiled member exceeds its actual enclosing record")
        offset += local
        chain.append(dict(record=record["id"], member=part, memberDie=index,
                          offset=local, byteLength=size, type=target))
        current = target
    size = dwarf.extent(current)
    need(offset <= root_bytes and size <= root_bytes - offset,
         "nested compiled member exceeds its root record")
    return dict(path=path, offset=offset, byteLength=size, type=current, chain=chain)


def inventory(artifact: bytes, debug: bytes | None, name: str, paths: list[str], definition: int | None = None) -> dict:
    need(0 < len(paths) <= MAX_PATHS and len(set(paths)) == len(paths),
         "member audit requires unique bounded source paths")
    for path in paths:
        path_parts(path)
    if artifact[:2] == b"MZ":
        need(debug is not None, "PE member inventory requires its exact PDB partner")
        pe, pdb = PE(artifact), MSF(debug)
        info = pdb.stream(1)
        need(len(info) >= 28 and int.from_bytes(info[:4], "little") in
             {20000404, 20030901, 20091201, 20140508} and info[12:28] == pe.guid and
             int.from_bytes(info[8:12], "little") == pe.age, "PDB GUID/age differs from actual artifact")
        data_symbols(pdb, pe, pe.age)
        pe_ranges(pe)
        types = Types(pdb.stream(2))
        matches = types.names.get(name, [])
        if definition is not None:
            need(definition in matches, "selected PDB definition is not an actual named root")
            matches = [definition]
        need(len(matches) == 1, "named member root lacks a unique actual PDB definition")
        members = [pe_member(types, matches[0], path) for path in paths]
        result = dict(format="pe-pdb", machine=pe.machine, pointerBytes=pe.pointer_bytes,
                      pdbSha256=hashlib.sha256(debug).hexdigest(), guid=str(uuid.UUID(bytes_le=pe.guid)), age=pe.age,
                      records=[dict(type=matches[0], byteLength=types.extent(matches[0]), members=members)],
                      types=types.closure([matches[0]]))
    else:
        need(artifact[:4] == b"\x7fELF" and debug is None, "ELF member inventory needs actual embedded compiler facts")
        dwarf = DWARF(ELF(artifact))
        matches = [index for index, row in dwarf.rows.items() if row["tag"] in {0x02, 0x13, 0x16} and
                   (dwarf.attribute(index, 0x03) or {}).get("value") == name and
                   not (dwarf.attribute(index, 0x3C) or {}).get("value")]
        need(matches, "named member root has no actual embedded definition")
        if definition is not None:
            need(definition in matches, "selected DWARF definition is not an actual named root")
            matches = [definition]
        need(len(matches) <= MAX_PATHS // len(paths), "excessive compiled definition/member query product")
        records = [dict(type=index, unit=dwarf.rows[index]["unit"], byteLength=dwarf.extent(index),
                        members=[dwarf_member(dwarf, index, path) for path in paths]) for index in matches]
        result = dict(format="elf-dwarf", machine=62, pointerBytes=8, records=records,
                      units=dwarf.units, dies=dwarf.closure(matches))
    result.update(schema=1, kind="artifact-member-path-inventory", typeName=name,
                  artifactSha256=hashlib.sha256(artifact).hexdigest(),
                  semanticCoverage="requires-original-source-audit")
    return result


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--artifact", type=Path, required=True)
    parser.add_argument("--pdb", type=Path)
    parser.add_argument("--type", required=True, dest="type_name")
    parser.add_argument("--path", action="append", required=True, dest="paths")
    parser.add_argument("--definition", type=lambda text: int(text, 0))
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    try:
        for source in (args.artifact, args.pdb):
            if source is not None:
                need(args.out.resolve() != source.resolve() and
                     (not args.out.exists() or not os.path.samefile(args.out, source)),
                     "member inventory output must not replace an artifact/debug input")
        result = inventory(read_file(args.artifact), read_file(args.pdb) if args.pdb else None,
                           args.type_name, args.paths, args.definition)
        text = json.dumps(result, indent=2, sort_keys=True) + "\n"
        need(len(text.encode("utf-8")) <= MAX_BYTES, "member facts exceed bounded output owner")
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
        parser.exit(2, f"native Q2 member compiler inventory: {error}\n")


if __name__ == "__main__":
    main()
