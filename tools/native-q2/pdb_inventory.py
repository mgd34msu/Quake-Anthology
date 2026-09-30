"""Read exact PE/PDB compiler facts; never infer a complete private-state audit."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import struct
import tempfile
import uuid


MAX_BYTES = 512 * 1024 * 1024
MAX_RECORDS = 262144
MSF_MAGIC = b"Microsoft C/C++ MSF 7.00\r\n\x1aDS\0\0\0"


class InventoryError(ValueError):
    pass


def need(condition: bool, message: str) -> None:
    if not condition:
        raise InventoryError(message)


class Reader:
    def __init__(self, data: bytes):
        self.data = data
        self.at = 0

    def take(self, size: int) -> bytes:
        need(0 <= size <= len(self.data) - self.at, "truncated compiler metadata")
        result = self.data[self.at:self.at + size]
        self.at += size
        return result

    def integer(self, width: int, signed: bool = False) -> int:
        return int.from_bytes(self.take(width), "little", signed=signed)

    def text(self) -> str:
        end = self.data.find(b"\0", self.at)
        need(end >= self.at, "unterminated compiler name")
        text = self.data[self.at:end].decode("utf-8")
        self.at = end + 1
        return text

    def numeric(self, nonnegative: bool = True) -> int:
        leaf = self.integer(2)
        if leaf < 0x8000:
            return leaf
        kinds = {0x8000: (1, True), 0x8001: (2, True), 0x8002: (2, False),
                 0x8003: (4, True), 0x8004: (4, False), 0x8009: (8, True),
                 0x800A: (8, False), 0x8017: (16, True), 0x8018: (16, False)}
        need(leaf in kinds, f"unsupported CodeView integer leaf {leaf:#x}")
        width, signed = kinds[leaf]
        value = self.integer(width, signed)
        need(not nonnegative or value >= 0, "negative compiler object offset or extent")
        return value


def read_file(path: Path) -> bytes:
    need(0 < path.stat().st_size <= MAX_BYTES, "input file exceeds bounded metadata owner")
    data = path.read_bytes()
    need(len(data) <= MAX_BYTES, "input file grew beyond bounded metadata owner")
    return data


def records(data: bytes):
    reader = Reader(data)
    count = 0
    while reader.at < len(data):
        size = reader.integer(2)
        need(size >= 2 and count < MAX_RECORDS, "invalid or excessive CodeView records")
        body = Reader(reader.take(size))
        yield body.integer(2), body.take(size - 2)
        count += 1


class MSF:
    def __init__(self, data: bytes):
        need(data.startswith(MSF_MAGIC), "PDB is not MSF 7.00")
        header = Reader(data[32:56])
        block, free_map, blocks, directory_size, reserved, block_map = (
            header.integer(4) for _ in range(6))
        need(512 <= block <= 65536 and not block & (block - 1), "invalid MSF block size")
        need(blocks * block == len(data) and free_map < blocks and reserved == 0,
             "MSF block extent or header differs")
        need(0 < directory_size <= MAX_BYTES, "invalid MSF directory extent")
        self.data, self.block, self.blocks = data, block, blocks
        pages = (directory_size + block - 1) // block
        need(pages * 4 <= block, "multi-block MSF directory maps are unsupported")
        mapping = Reader(self.page(block_map))
        directory = b"".join(self.page(mapping.integer(4)) for _ in range(pages))
        directory = Reader(directory[:directory_size])
        count = directory.integer(4)
        need(4 <= count <= 65536, "invalid MSF stream count")
        sizes = [directory.integer(4) for _ in range(count)]
        self.streams: list[tuple[int, list[int]]] = []
        for size in sizes:
            if size == 0xFFFFFFFF:
                self.streams.append((0xFFFFFFFF, []))
                continue
            need(size <= MAX_BYTES, "MSF stream exceeds bounded input")
            ids = [directory.integer(4) for _ in range((size + block - 1) // block)]
            need(all(page < blocks for page in ids), "MSF stream page is outside the file")
            self.streams.append((size, ids))
        need(directory.at == len(directory.data), "MSF directory has trailing data")

    def page(self, index: int) -> bytes:
        need(index < self.blocks, "MSF page is outside the file")
        return self.data[index * self.block:(index + 1) * self.block]

    def stream(self, index: int) -> bytes:
        need(0 <= index < len(self.streams), "PDB stream index is outside the directory")
        size, pages = self.streams[index]
        need(size != 0xFFFFFFFF, "required PDB stream is absent")
        return b"".join(self.page(page) for page in pages)[:size]


class PE:
    def __init__(self, data: bytes):
        self.data = data
        need(data[:2] == b"MZ", "artifact is not a PE image")
        need(len(data) >= 64, "truncated DOS header")
        offset = int.from_bytes(data[60:64], "little")
        reader = Reader(data[offset:])
        need(reader.take(4) == b"PE\0\0", "invalid PE header")
        self.machine, count = reader.integer(2), reader.integer(2)
        reader.take(12)
        optional_size = reader.integer(2)
        reader.take(2)
        optional = reader.take(optional_size)
        magic = int.from_bytes(optional[:2], "little")
        need((self.machine, magic) in {(0x14C, 0x10B), (0x8664, 0x20B)},
             "only actual i386/x64 PE compiler inventories are supported")
        self.pointer_bytes = 4 if magic == 0x10B else 8
        directory = 96 if self.pointer_bytes == 4 else 112
        need(len(optional) >= directory + 56 and
             int.from_bytes(optional[directory - 4:directory], "little") >= 7,
             "artifact lacks a CodeView debug directory")
        need(0 < count <= 96, "invalid PE section count")
        self.section_bytes = reader.take(count * 40)
        self.sections = []
        for at in range(0, len(self.section_bytes), 40):
            section = self.section_bytes[at:at + 40]
            self.sections.append({"virtualSize": int.from_bytes(section[8:12], "little"),
                "rva": int.from_bytes(section[12:16], "little"),
                "rawSize": int.from_bytes(section[16:20], "little"),
                "raw": int.from_bytes(section[20:24], "little"),
                "flags": int.from_bytes(section[36:40], "little")})
        debug_rva, debug_size = struct.unpack_from("<II", optional, directory + 48)
        need(debug_size and debug_size % 28 == 0, "invalid PE debug directory extent")
        debug = self.rva(debug_rva, debug_size)
        partners = []
        for at in range(0, len(debug), 28):
            _, _, _, _, kind, size, rva, raw = struct.unpack_from("<IIHHIIII", debug, at)
            if kind != 2:
                continue
            need(size >= 25 and raw <= len(data) and size <= len(data) - raw,
                 "truncated artifact CodeView record")
            record = data[raw:raw + size]
            need(record == self.rva(rva, size), "CodeView file/RVA views differ")
            need(record[:4] == b"RSDS", "only GUID/age CodeView records are supported")
            name = Reader(record[24:]).text()
            partners.append((record[4:20], int.from_bytes(record[20:24], "little"), name))
        need(len(partners) == 1, "artifact requires one actual RSDS compiler partner")
        self.guid, self.age, self.pdb_name = partners[0]

    def rva(self, address: int, size: int) -> bytes:
        matches = []
        for section in self.sections:
            offset = address - section["rva"]
            if 0 <= offset <= section["rawSize"] and size <= section["rawSize"] - offset:
                raw = section["raw"] + offset
                need(raw <= len(self.data) and size <= len(self.data) - raw,
                     "PE mapped debug extent exceeds artifact bytes")
                matches.append(self.data[raw:raw + size])
        need(len(matches) == 1, "debug RVA has no unique actual file section")
        return matches[0]


class Types:
    def __init__(self, data: bytes):
        header = Reader(data)
        version, size, begin, end, byte_count = (header.integer(4) for _ in range(5))
        need(version == 20040203 and size >= 56 and begin == 0x1000 and
             begin <= end <= begin + MAX_RECORDS and size + byte_count == len(data),
             "unsupported or incomplete compiler TPI stream")
        self.rows = dict(enumerate(records(data[size:]), begin))
        need(len(self.rows) == end - begin, "compiler type count differs from its records")
        self.names: dict[str, list[int]] = {}
        for index, (kind, body) in self.rows.items():
            if kind in {0x1504, 0x1505, 0x1506}:
                row = self.describe(index)
                if not row["forward"]:
                    self.names.setdefault(row["name"], []).append(index)

    def fields(self, index: int, seen: frozenset[int] = frozenset()) -> list[dict]:
        need(index not in seen and len(seen) < 128, "cyclic or excessive compiler field lists")
        need(index in self.rows and self.rows[index][0] == 0x1203,
             "object has no actual compiler field list")
        reader = Reader(self.rows[index][1])
        result = []
        while reader.at < len(reader.data):
            if reader.data[reader.at] >= 0xF0:
                padding = reader.integer(1) & 15
                need(padding > 0, "invalid CodeView field padding")
                reader.take(padding - 1)
                continue
            leaf = reader.integer(2)
            if leaf in {0x150D, 0x1400, 0x151A}:
                attributes, target = reader.integer(2), reader.integer(4)
                offset = reader.numeric()
                name = reader.text() if leaf == 0x150D else "<base>"
                result.append({"kind": "member" if leaf == 0x150D else "base",
                    "name": name, "type": target, "offset": offset, "attributes": attributes})
            elif leaf == 0x1404:
                reader.take(2)
                result.extend(self.fields(reader.integer(4), seen | {index}))
            elif leaf in {0x1401, 0x1402}:
                attributes, target, pointer = reader.integer(2), reader.integer(4), reader.integer(4)
                result.append({"kind": "virtual-base", "type": target, "pointerType": pointer,
                    "attributes": attributes, "pointerOffset": reader.numeric(),
                    "tableOffset": reader.numeric()})
            elif leaf == 0x1409:
                reader.take(2)
                result.append({"kind": "vtable", "type": reader.integer(4)})
            elif leaf in {0x150E, 0x1512}:
                attributes, target = reader.integer(2), reader.integer(4)
                result.append({"kind": "static-member" if leaf == 0x150E else "nested-type",
                    "type": target,
                    "attributes": attributes, "name": reader.text()})
            elif leaf == 0x150F:
                count, target = reader.integer(2), reader.integer(4)
                result.append({"kind": "overloaded-method", "type": target,
                    "methodList": target, "overloadCount": count, "name": reader.text()})
            elif leaf == 0x1510:
                need(reader.integer(2) == 0, "nested compiler type padding differs")
                result.append({"kind": "nested-type", "type": reader.integer(4),
                    "name": reader.text()})
            elif leaf == 0x1511:
                attributes, target = reader.integer(2), reader.integer(4)
                if ((attributes >> 2) & 7) in {4, 6}:
                    reader.take(4)
                result.append({"kind": "method", "type": target, "name": reader.text()})
            elif leaf == 0x1502:
                reader.take(2)
                reader.numeric(nonnegative=False)
                reader.text()
            else:
                raise InventoryError(f"unsupported compiler field leaf {leaf:#x}; no layout inferred")
        return result

    def describe(self, index: int) -> dict:
        if index < 0x1000:
            return {"id": index, "kind": "primitive", "code": index & 255,
                    "pointerMode": (index >> 8) & 15}
        need(index in self.rows, "compiler field references an absent type")
        kind, body = self.rows[index]
        reader = Reader(body)
        row: dict = {"id": index, "leaf": kind}
        if kind in {0x1504, 0x1505, 0x1506}:
            members, flags, fields = reader.integer(2), reader.integer(2), reader.integer(4)
            if kind != 0x1506:
                reader.take(8)
            row.update(kind="union" if kind == 0x1506 else "record", memberCount=members,
                forward=bool(flags & 0x80), byteLength=reader.numeric(), name=reader.text(),
                fieldList=fields, attributes=flags)
            if flags & 0x200:
                row["uniqueName"] = reader.text()
        elif kind == 0x1002:
            target, attributes = reader.integer(4), reader.integer(4)
            pointer_kind, pointer_mode = attributes & 31, (attributes >> 5) & 7
            need(pointer_mode in {0, 1, 4} and pointer_kind in {10, 12},
                 "member/based/segmented compiler pointers require a separate proven producer")
            width = (attributes >> 13) & 63
            need(width == (4 if pointer_kind == 10 else 8),
                 "compiler pointer kind and exact extent differ")
            row.update(kind="pointer", target=target, byteLength=(attributes >> 13) & 63,
                       attributes=attributes, pointerKind=pointer_kind, pointerMode=pointer_mode)
        elif kind == 0x1001:
            row.update(kind="modifier", target=reader.integer(4), attributes=reader.integer(2))
        elif kind == 0x1503:
            row.update(kind="array", target=reader.integer(4), indexType=reader.integer(4),
                       byteLength=reader.numeric(), name=reader.text())
        elif kind == 0x1507:
            members, flags, target, fields = (reader.integer(2), reader.integer(2),
                                             reader.integer(4), reader.integer(4))
            row.update(kind="enum", target=target, memberCount=members, attributes=flags,
                       fieldList=fields, name=reader.text())
            if flags & 0x200:
                row["uniqueName"] = reader.text()
        elif kind == 0x1205:
            row.update(kind="bitfield", target=reader.integer(4), bits=reader.integer(1),
                       bitOffset=reader.integer(1))
        else:
            row.update(kind="uninterpreted", recordHex=body.hex())
        return row

    def extent(self, index: int, seen: frozenset[int] = frozenset()) -> int:
        need(index not in seen and len(seen) < 128, "cyclic or excessive compiler extent chain")
        row = self.describe(index)
        if row["kind"] == "primitive":
            modes = {4: 4, 6: 8, 7: 16}
            widths = {0x08: 4, 0x10: 1, 0x20: 1, 0x70: 1, 0x71: 2,
                0x7A: 2, 0x7B: 4, 0x7C: 1, 0x68: 1, 0x69: 1,
                0x11: 2, 0x21: 2, 0x72: 2, 0x73: 2, 0x12: 4, 0x22: 4,
                0x74: 4, 0x75: 4, 0x13: 8, 0x23: 8, 0x76: 8, 0x77: 8,
                0x14: 16, 0x24: 16, 0x78: 16, 0x79: 16,
                0x46: 2, 0x40: 4, 0x45: 4, 0x44: 6, 0x41: 8, 0x42: 10, 0x43: 16,
                0x56: 4, 0x50: 8, 0x55: 8, 0x54: 12, 0x51: 16, 0x52: 20, 0x53: 32,
                0x30: 1, 0x31: 2, 0x32: 4, 0x33: 8, 0x34: 16}
            mode = row["pointerMode"]
            need(mode in modes if mode else row["code"] in widths,
                 "compiler primitive has no admitted object extent")
            return modes[mode] if mode else widths[row["code"]]
        if row["kind"] in {"modifier", "enum", "bitfield"}:
            return self.extent(row["target"], seen | {index})
        if row.get("forward"):
            choices = self.names.get(row["name"], [])
            need(len(choices) == 1, "forward type has no unique actual compiler extent")
            return self.extent(choices[0], seen | {index})
        need(row.get("byteLength", 0) > 0, "compiler type has no complete object extent")
        return row["byteLength"]

    def closure(self, roots: list[int]) -> list[dict]:
        pending = list(roots)
        rows: dict[int, dict] = {}
        while pending:
            index = pending.pop()
            if index in rows:
                continue
            row = self.describe(index)
            rows[index] = row
            if row["kind"] in {"record", "union"}:
                if row["forward"]:
                    alternatives = self.names.get(row["name"], [])
                    need(len(alternatives) == 1, "forward type has no unique actual compiler definition")
                    row["definition"] = alternatives[0]
                    pending.append(alternatives[0])
                elif row["fieldList"]:
                    row["fields"] = self.fields(row["fieldList"])
                    for field in row["fields"]:
                        pending.append(field["type"])
                        if "pointerType" in field:
                            pending.append(field["pointerType"])
            if "target" in row:
                pending.append(row["target"])
        return [rows[index] for index in sorted(rows)]


def data_symbols(pdb: MSF, pe: PE, age: int) -> list[dict]:
    dbi = pdb.stream(3)
    need(len(dbi) >= 64, "truncated DBI header")
    need(int.from_bytes(dbi[:4], "little") == 0xFFFFFFFF and
         int.from_bytes(dbi[4:8], "little") == 19990903 and
         int.from_bytes(dbi[8:12], "little") == age and
         int.from_bytes(dbi[58:60], "little") == pe.machine,
         "DBI age/machine does not match actual artifact")
    need(not int.from_bytes(dbi[56:58], "little") & 2, "stripped PDB cannot supply private layout facts")
    sizes = [int.from_bytes(dbi[at:at + 4], "little", signed=True)
             for at in (24, 28, 32, 36, 40, 52)]
    optional_size = int.from_bytes(dbi[48:52], "little", signed=True)
    need(all(size >= 0 for size in sizes) and optional_size >= 12 and optional_size % 2 == 0,
         "invalid DBI substream extents")
    optional_at = 64 + sum(sizes)
    need(optional_at + optional_size == len(dbi), "DBI substreams differ from exact stream extent")
    optional = Reader(dbi[optional_at:])
    indexes = [optional.integer(2) for _ in range(optional_size // 2)]
    need(indexes[3] == 0xFFFF and indexes[4] == 0xFFFF,
         "OMAP-remapped symbols require a separate proven mapping producer")
    need(pdb.stream(indexes[5]) == pe.section_bytes, "PDB linked sections differ from actual artifact")
    streams = [(int.from_bytes(dbi[20:22], "little"), "<global>", None)]
    modules = Reader(dbi[64:64 + sizes[0]])
    while modules.at < len(modules.data):
        header = modules.take(64)
        module, object_file = modules.text(), modules.text()
        modules.take((-modules.at) % 4)
        index = int.from_bytes(header[34:36], "little")
        symbol_size = int.from_bytes(header[36:40], "little")
        if index != 0xFFFF and symbol_size:
            streams.append((index, f"{module}|{object_file}", symbol_size))
    rows = []
    for index, origin, size in streams:
        if index == 0xFFFF:
            continue
        data = pdb.stream(index)
        if size is not None:
            need(4 <= size <= len(data) and data[:4] == b"\x04\0\0\0",
                 "unsupported module symbol stream")
            data = data[4:size]
        for kind, body in records(data):
            if kind not in {0x110C, 0x110D}:
                continue
            reader = Reader(body)
            target, offset, section, name = (reader.integer(4), reader.integer(4),
                                            reader.integer(2), reader.text())
            need(1 <= section <= len(pe.sections), "typed data symbol has no linked image section")
            area = pe.sections[section - 1]
            need(offset < area["virtualSize"], "typed symbol exceeds actual linked section")
            rows.append({"name": name, "type": target, "rva": area["rva"] + offset,
                "sectionBytesRemaining": area["virtualSize"] - offset,
                "writableNonExecutable": bool(area["flags"] & 0x80000000) and
                    not bool(area["flags"] & 0x20000000), "origin": origin})
    return rows


def inventory(artifact: bytes, debug: bytes, type_names: list[str], symbol_names: list[str]) -> dict:
    pe, pdb = PE(artifact), MSF(debug)
    info = pdb.stream(1)
    need(len(info) >= 28 and int.from_bytes(info[:4], "little") in
         {20000404, 20030901, 20091201, 20140508} and info[12:28] == pe.guid and
         int.from_bytes(info[8:12], "little") == pe.age, "PDB GUID/age differs from artifact RSDS")
    types = Types(pdb.stream(2))
    symbols = data_symbols(pdb, pe, pe.age)
    roots = []
    selected_symbols = []
    for name in type_names:
        choices = types.names.get(name, [])
        need(len(choices) == 1, f"no unique compiler definition for type {name}")
        roots.append(choices[0])
    for name in symbol_names:
        matches = [row for row in symbols if row["name"] == name]
        identities = {(row["type"], row["rva"]) for row in matches}
        need(len(identities) == 1, f"no unique linked typed symbol for {name}")
        roots.append(matches[0]["type"])
        for match in matches:
            extent = types.extent(match["type"])
            need(extent <= match["sectionBytesRemaining"],
                 "typed object extent exceeds its actual linked image section")
            selected_symbols.append(dict(match, byteLength=extent))
    return {"schema": 1, "kind": "artifact-compiler-inventory",
        "artifactSha256": hashlib.sha256(artifact).hexdigest(),
        "pdbSha256": hashlib.sha256(debug).hexdigest(),
        "guid": str(uuid.UUID(bytes_le=pe.guid)), "age": pe.age,
        "machine": pe.machine, "pointerBytes": pe.pointer_bytes,
        "embeddedPdbPath": pe.pdb_name, "roots": sorted(set(roots)),
        "symbols": selected_symbols, "dataSymbols": symbols, "types": types.closure(roots)}


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--artifact", type=Path, required=True)
    parser.add_argument("--pdb", type=Path, required=True)
    parser.add_argument("--type", action="append", default=[], dest="types")
    parser.add_argument("--symbol", action="append", default=[], dest="symbols")
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    try:
        need(args.types or args.symbols, "select actual compiler types or typed globals")
        for source in (args.artifact, args.pdb):
            need(args.out.resolve() != source.resolve() and
                 (not args.out.exists() or not os.path.samefile(args.out, source)),
                 "inventory output must not replace an artifact or compiler input")
        value = inventory(read_file(args.artifact), read_file(args.pdb), args.types, args.symbols)
        text = json.dumps(value, indent=2, sort_keys=True) + "\n"
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
        parser.exit(2, f"native Q2 compiler inventory: {error}\n")


if __name__ == "__main__":
    main()
