"""Read embedded ELF/DWARF compiler facts without asserting private-state completeness."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import struct
import tempfile

from pdb_inventory import InventoryError, MAX_BYTES, Reader, need, read_file


MAX_DIES = 1048576
MAX_DEPTH = 256


def cstring(data: bytes, offset: int) -> str:
    need(0 <= offset < len(data), "compiler string offset exceeds actual section")
    return Reader(data[offset:]).text()


class ELF:
    def __init__(self, data: bytes):
        self.data = data
        need(len(data) >= 64 and data[:7] == b"\x7fELF\x02\x01\x01",
             "only actual little-endian ELF64 compiler inventories are supported")
        kind, machine, version = struct.unpack_from("<HHI", data, 16)
        need(kind == 3 and machine == 62 and version == 1, "artifact is not an x64 shared image")
        program_at, section_at = struct.unpack_from("<QQ", data, 32)
        header_size, program_size, programs, section_size, sections, names = struct.unpack_from("<HHHHHH", data, 52)
        need(header_size == 64 and program_size == 56 and section_size == 64 and
             0 < programs <= 256 and 0 < sections <= 65535 and 0 < names < sections,
             "unsupported ELF header or extended section numbering")
        self.segments = []
        for row in self.span(program_at, programs * 56, 56):
            segment, flags, offset, address, _, files, memory, _ = struct.unpack("<IIQQQQQQ", row)
            need(files <= memory, "ELF segment file extent exceeds memory")
            self.bytes(offset, files)
            self.segments.append(dict(kind=segment, flags=flags, address=address, bytes=memory))
        loads = [row for row in self.segments if row["kind"] == 1]
        need(loads and min(row["address"] for row in loads) == 0,
             "ELF image RVAs require a zero-based actual load span")
        self.sections = []
        for row in self.span(section_at, sections * 64, 64):
            name, kind, flags, address, offset, size, link, info, align, entry = struct.unpack("<IIQQQQIIQQ", row)
            need(not flags & 0x800, "compressed compiler sections require a separate bounded producer")
            if kind != 8:
                self.bytes(offset, size)
            self.sections.append(dict(nameOffset=name, kind=kind, flags=flags, address=address,
                offset=offset, bytes=size, link=link, info=info, alignment=align, entryBytes=entry))
        need(self.sections[names]["kind"] == 3, "ELF section names have no string table")
        name_bytes = self.section_data(self.sections[names])
        self.named: dict[str, dict] = {}
        for row in self.sections:
            row["name"] = cstring(name_bytes, row["nameOffset"])
            if row["name"]:
                need(row["name"] not in self.named, "ambiguous ELF named sections")
                self.named[row["name"]] = row
        for name in (".debug_info", ".debug_abbrev", ".debug_str", ".symtab"):
            need(name in self.named, f"artifact lacks embedded {name}")
        for row in self.sections:
            if row["kind"] in {4, 9}:
                need(row["info"] < len(self.sections) and
                     not self.sections[row["info"]]["name"].startswith(".debug"),
                     "embedded debug relocations require a separate proven producer")

    def bytes(self, offset: int, size: int) -> bytes:
        need(0 <= offset <= len(self.data) and 0 <= size <= len(self.data) - offset,
             "ELF file span exceeds actual artifact")
        return self.data[offset:offset + size]

    def span(self, offset: int, size: int, stride: int):
        data = self.bytes(offset, size)
        return (data[at:at + stride] for at in range(0, size, stride))

    def section_data(self, row: dict) -> bytes:
        need(row["kind"] != 8, "NOBITS section has no compiler bytes")
        return self.bytes(row["offset"], row["bytes"])

    def debug(self, name: str) -> bytes:
        need(name in self.named, f"artifact lacks embedded {name}")
        return self.section_data(self.named[name])

    def writable(self, address: int, size: int) -> bool:
        fits = [row for row in self.segments if row["kind"] == 1 and
                row["address"] <= address and size <= row["bytes"] - (address - row["address"])]
        need(len(fits) == 1, "typed global span has no unique actual ELF load segment")
        relro = any(row["kind"] == 0x6474E552 and address < row["address"] + row["bytes"] and
                    row["address"] < address + size for row in self.segments)
        return bool(fits[0]["flags"] & 2) and not bool(fits[0]["flags"] & 1) and not relro

    def symbols(self) -> list[dict]:
        area = self.named[".symtab"]
        need(area["kind"] == 2 and area["entryBytes"] == 24 and area["bytes"] % 24 == 0 and
             area["link"] < len(self.sections), "unsupported actual ELF symbol table")
        strings = self.sections[area["link"]]
        need(strings["kind"] == 3, "ELF symbols have no actual string table")
        strings = self.section_data(strings)
        result = []
        for row in self.span(area["offset"], area["bytes"], 24):
            name, info, _, section, address, size = struct.unpack("<IBBHQQ", row)
            if info & 15 != 1 or not section or not size:
                continue
            need(section < len(self.sections), "absolute/extended object symbols require qualification")
            owner = self.sections[section]
            need(owner["flags"] & 2 and owner["address"] <= address and
                 size <= owner["bytes"] - (address - owner["address"]),
                 "typed object symbol exceeds actual allocated section")
            result.append(dict(name=cstring(strings, name), rva=address, byteLength=size,
                writableNonExecutable=self.writable(address, size), section=owner["name"]))
        return result


class DWReader(Reader):
    def leb(self, signed: bool = False) -> int:
        value = 0
        for index in range(10):
            byte = self.integer(1)
            value |= (byte & 127) << (index * 7)
            if not byte & 128:
                if signed and byte & 64:
                    value -= 1 << ((index + 1) * 7)
                need((-(1 << 63) <= value < (1 << 63)) if signed else (0 <= value < (1 << 64)),
                     "DWARF integer exceeds bounded 64-bit value")
                return value
        raise InventoryError("excessive DWARF LEB128 integer")


class DWARF:
    def __init__(self, elf: ELF):
        self.elf = elf
        self.rows: dict[int, dict] = {}
        self.units: list[dict] = []
        self.abbreviations: dict[int, dict] = {}
        info = DWReader(elf.debug(".debug_info"))
        while info.at < len(info.data):
            start = info.at
            length = info.integer(4)
            width = 4
            if length == 0xFFFFFFFF:
                length, width = info.integer(8), 8
            else:
                need(length < 0xFFFFFFF0, "reserved DWARF unit length")
            payload_start = info.at
            unit = DWReader(info.take(length))
            version = unit.integer(2)
            need(version in {4, 5}, "only embedded DWARF4/5 compilation units are admitted")
            if version == 5:
                need(unit.integer(1) == 1, "split/type/skeleton units require a separate producer")
                address_bytes, abbreviation = unit.integer(1), unit.integer(width)
            else:
                abbreviation, address_bytes = unit.integer(width), unit.integer(1)
            need(address_bytes == 8, "compiler address width differs from actual ELF64 image")
            table = self.abbrev(abbreviation)
            stack: list[int] = []
            root = None
            while unit.at < len(unit.data):
                offset = payload_start + unit.at
                code = unit.leb()
                if not code:
                    need(stack, "DWARF child terminator has no enclosing DIE")
                    stack.pop()
                    continue
                need(code in table and len(self.rows) < MAX_DIES and len(stack) < MAX_DEPTH,
                     "invalid or excessive compiler DIE tree")
                tag, children, attributes = table[code]
                fields = {}
                for attribute, form, implicit in attributes:
                    need(attribute not in fields, "duplicate compiler DIE attribute")
                    fields[attribute] = self.form(unit, form, implicit, start, width, address_bytes)
                row = dict(id=offset, tag=tag, unit=start, attributes=fields, children=[])
                if stack:
                    self.rows[stack[-1]]["children"].append(offset)
                else:
                    need(root is None and tag == 0x11, "unit must contain one actual compilation root")
                    root = offset
                self.rows[offset] = row
                if children:
                    stack.append(offset)
            need(root is not None and not stack, "compiler DIE tree is incomplete")
            self.units.append(dict(offset=start, root=root, version=version, offsetBytes=width,
                                   attributes=self.rows[root]["attributes"]))
        for row in self.rows.values():
            for field in row["attributes"].values():
                if field["kind"] == "reference":
                    need(field["value"] in self.rows, "compiler reference has no actual DIE")
                    if field.get("unitRelative"):
                        need(self.rows[field["value"]]["unit"] == row["unit"],
                             "unit-relative compiler reference leaves its actual compilation unit")

    def abbrev(self, offset: int) -> dict:
        if offset in self.abbreviations:
            return self.abbreviations[offset]
        data = self.elf.debug(".debug_abbrev")
        need(offset < len(data), "abbreviation offset exceeds actual compiler section")
        reader = DWReader(data[offset:])
        table = {}
        while True:
            code = reader.leb()
            if not code:
                break
            need(code not in table and len(table) < 65536, "duplicate or excessive compiler abbreviations")
            tag, children = reader.leb(), reader.integer(1)
            need(children in {0, 1}, "invalid compiler child flag")
            attributes = []
            while True:
                attribute, form = reader.leb(), reader.leb()
                if not attribute and not form:
                    break
                need(attribute and form and len(attributes) < 256, "invalid compiler attribute roster")
                implicit = reader.leb(signed=True) if form == 0x21 else None
                attributes.append((attribute, form, implicit))
            table[code] = tag, children, attributes
        self.abbreviations[offset] = table
        return table

    def form(self, reader: DWReader, form: int, implicit: int | None,
             start: int, width: int, address_bytes: int, depth: int = 0) -> dict:
        need(depth < 8, "excessive indirect DWARF attribute")
        fixed = {0x05: 2, 0x06: 4, 0x07: 8, 0x0B: 1, 0x0C: 1}
        refs = {0x11: 1, 0x12: 2, 0x13: 4, 0x14: 8}
        kind = "constant"
        if form in fixed:
            value = reader.integer(fixed[form])
        elif form == 0x01:
            kind, value = "address", reader.integer(address_bytes)
        elif form == 0x08:
            kind, value = "string", reader.text()
        elif form in {0x0E, 0x1F}:
            kind = "string"
            value = cstring(self.elf.debug(".debug_str" if form == 0x0E else ".debug_line_str"), reader.integer(width))
        elif form in {0x0D, 0x0F}:
            value = reader.leb(signed=form == 0x0D)
        elif form in refs or form == 0x15:
            kind = "reference"
            value = start + (reader.integer(refs[form]) if form in refs else reader.leb())
        elif form == 0x10:
            kind, value = "reference", reader.integer(width)
        elif form == 0x17:
            kind, value = "section-offset", reader.integer(width)
        elif form in {0x03, 0x04, 0x09, 0x0A, 0x18}:
            length = reader.integer({0x03: 2, 0x04: 4, 0x0A: 1}[form]) if form in {0x03, 0x04, 0x0A} else reader.leb()
            kind, value = "expression" if form == 0x18 else "block", reader.take(length).hex()
        elif form == 0x19:
            value = 1
        elif form == 0x21:
            need(implicit is not None, "implicit compiler constant is missing from abbreviation")
            value = implicit
        elif form == 0x16:
            actual = reader.leb()
            need(actual != 0x21, "indirect implicit constant has no abbreviation payload")
            return self.form(reader, actual, None, start, width, address_bytes, depth + 1)
        elif form == 0x1E:
            kind, value = "data16", reader.take(16).hex()
        elif form == 0x20:
            kind, value = "external-type-signature", reader.integer(8)
        elif form in {0x22, 0x23}:
            kind, value = "location-index" if form == 0x22 else "range-index", reader.leb()
        else:
            raise InventoryError(f"unsupported compiler attribute form {form:#x}; no fact inferred")
        result = dict(form=form, kind=kind, value=value)
        if kind == "reference":
            result["unitRelative"] = form in refs or form == 0x15
        return result

    def attribute(self, index: int, attribute: int, seen: frozenset[int] = frozenset()) -> dict | None:
        need(index not in seen and len(seen) < MAX_DEPTH, "cyclic compiler declaration inheritance")
        row = self.rows[index]
        fields = row["attributes"]
        if attribute in fields:
            return fields[attribute]
        # These facts belong to this DIE, never its declaration/abstract origin.
        if attribute in {0x01, 0x3C}:
            return None
        for owner in (0x47, 0x31):
            reference = fields.get(owner)
            if reference:
                need(reference["kind"] == "reference", "compiler declaration has no typed DIE reference")
                found = self.attribute(reference["value"], attribute, seen | {index})
                if found is not None:
                    return found
        return None

    def extent(self, index: int, seen: frozenset[int] = frozenset()) -> int:
        need(index not in seen and len(seen) < MAX_DEPTH, "cyclic compiler object extent")
        field = self.attribute(index, 0x0B)
        if field:
            need(field["kind"] == "constant" and field["value"] > 0,
                 "compiler object has no constant positive byte extent")
            return field["value"]
        row = self.rows[index]
        need(row["tag"] in {0x16, 0x26, 0x35, 0x37, 0x47},
             "compiler type has no admitted byte extent")
        reference = self.attribute(index, 0x49)
        need(reference is not None and reference["kind"] == "reference",
             "compiler type qualifier lacks actual referenced extent")
        return self.extent(reference["value"], seen | {index})

    def closure(self, roots: list[int]) -> list[dict]:
        pending, selected = list(roots), {}
        for unit in self.units:
            pending.extend(field["value"] for field in unit["attributes"].values()
                           if field["kind"] == "reference")
        while pending:
            index = pending.pop()
            if index in selected:
                continue
            row = self.rows[index]
            selected[index] = row
            pending.extend(row["children"])
            pending.extend(field["value"] for field in row["attributes"].values()
                           if field["kind"] == "reference")
        return [selected[index] for index in sorted(selected)]


def inventory(data: bytes, type_names: list[str], symbol_names: list[str]) -> dict:
    elf = ELF(data)
    dwarf = DWARF(elf)
    symbols = elf.symbols()
    roots, selected = [], []
    for name in type_names:
        matches = [index for index, row in dwarf.rows.items()
                   if row["tag"] in {0x02, 0x13, 0x17, 0x16, 0x24} and
                   (dwarf.attribute(index, 0x03) or {}).get("value") == name and
                   not (dwarf.attribute(index, 0x3C) or {}).get("value")]
        need(matches, f"no actual compiler type definitions for {name}")
        roots.extend(matches)
    for name in symbol_names:
        matches = [row for row in symbols if row["name"] == name]
        need(len({(row["rva"], row["byteLength"]) for row in matches}) == 1,
             f"no unique actual ELF object symbol {name}")
        symbol = matches[0]
        definitions = []
        for index, row in dwarf.rows.items():
            if row["tag"] != 0x34:
                continue
            field = dwarf.attribute(index, 0x6E) or dwarf.attribute(index, 0x03)
            if not field or field["value"] != name:
                continue
            location = dwarf.attribute(index, 0x02)
            if not location or location["kind"] not in {"expression", "block"}:
                continue
            expression = bytes.fromhex(location["value"])
            if len(expression) != 9 or expression[0] != 3 or int.from_bytes(expression[1:], "little") != symbol["rva"]:
                continue
            target = dwarf.attribute(index, 0x49)
            need(target is not None and target["kind"] == "reference" and
                 dwarf.extent(target["value"]) == symbol["byteLength"],
                 "compiler global type/location and actual ELF object extent differ")
            definitions.append(index)
        need(definitions, f"ELF object {name} has no exact embedded compiler type/address definition")
        roots.extend(definitions)
        selected.append(dict(symbol, definitions=definitions))
    return dict(schema=1, kind="artifact-compiler-inventory", format="elf-dwarf",
        artifactSha256=hashlib.sha256(data).hexdigest(), machine=62, pointerBytes=8,
        roots=sorted(set(roots)), units=dwarf.units, symbols=selected, dataSymbols=symbols,
        dies=dwarf.closure(roots))


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--artifact", type=Path, required=True)
    parser.add_argument("--type", action="append", default=[], dest="types")
    parser.add_argument("--symbol", action="append", default=[], dest="symbols")
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    try:
        need(args.types or args.symbols, "select actual compiler types or typed globals")
        need(args.out.resolve() != args.artifact.resolve() and
             (not args.out.exists() or not os.path.samefile(args.out, args.artifact)),
             "inventory output must not replace its artifact input")
        value = inventory(read_file(args.artifact), args.types, args.symbols)
        text = json.dumps(value, indent=2, sort_keys=True) + "\n"
        need(len(text.encode("utf-8")) <= MAX_BYTES, "compiler inventory exceeds bounded output owner")
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
