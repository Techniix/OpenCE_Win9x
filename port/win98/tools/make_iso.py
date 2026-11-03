#!/usr/bin/env python3
"""Puts files and folders on a CD image (ISO 9660 with Joliet's long names,
which Windows 95 and 98 read), for an emulator's CD drive: 86Box's, say,
since a floppy holds 1.44 MB and halo.exe is larger.

    python3 port/win98/tools/make_iso.py opence.iso build/win98/halo.exe \\
        build/win98/probe/import_check.exe port/win98/tools/config.toml \\
        /mnt/c/Games/OpenCE/maps

Each argument after the image is a file or a folder, put at the image's top
(a folder with what is in it). Windows paths (C:\\Games\\...) are read as
WSL's /mnt/c/... ones.

Files copied from a CD to a disk on Windows 9x keep the CD's read-only
attribute: clear it (the file's Properties, or attrib -r) on files the
program writes, as config.toml.
"""

import argparse
import os
import re
import struct
import sys
import time

SECTOR = 2048


def wsl_path(path: str) -> str:
    match = re.match(r"^([A-Za-z]):[\\/](.*)$", path)
    if match and not os.path.exists(path):
        return f"/mnt/{match.group(1).lower()}/" + match.group(2).replace("\\", "/")
    return path


class Node:
    def __init__(self, name, source=None, children=None):
        self.name = name
        self.source = source
        self.children = children  # None for a file
        self.size = os.path.getsize(source) if source and children is None else 0
        self.extent = 0
        # per namespace: the directory's extent and size, and its children
        # in the namespace's order
        self.directory = {}
        self.sorted = {}


def tree(path: str, name: str) -> Node:
    if os.path.isdir(path):
        children = [tree(os.path.join(path, entry), entry) for entry in sorted(os.listdir(path))]
        return Node(name, path, children)
    return Node(name, path)


# ---------- the two namespaces' names

def primary_name(node: Node) -> bytes:
    """ISO 9660 level 2: upper case, d-characters, at most 30 of them"""
    name = re.sub(r"[^A-Z0-9_.]", "_", node.name.upper())
    if node.children is not None:
        return name.replace(".", "_")[:31].encode("ascii")
    stem, dot, extension = name.rpartition(".")
    if not dot:
        stem, extension = name, ""
    stem = stem.replace(".", "_")
    extension = extension[:8]
    stem = stem[:30 - len(extension) - 1]
    return f"{stem}.{extension};1".encode("ascii")


def joliet_name(node: Node) -> bytes:
    name = node.name[:64]
    if node.children is None:
        name += ";1"
    return name.encode("utf-16-be")


NAMESPACES = {"primary": primary_name, "joliet": joliet_name}


def unique_names(directory: Node, namer):
    """each child's name, made unique within the folder (~1, ~2...)"""
    names = {}
    used = set()
    for child in directory.children:
        name = namer(child)
        if name in used:
            for number in range(1, 1000):
                suffix = f"~{number}"
                if namer is joliet_name:
                    candidate = (child.name[:60] + suffix + (";1" if child.children is None else "")).encode(
                        "utf-16-be")
                else:
                    base = name.decode().split(";")[0]
                    stem, dot, extension = base.rpartition(".")
                    if not dot:
                        stem, extension = base, ""
                    stem = stem[:30 - len(extension) - 1 - len(suffix)] + suffix
                    candidate = (f"{stem}.{extension};1" if child.children is None else stem).encode("ascii")
                if candidate not in used:
                    name = candidate
                    break
        used.add(name)
        names[id(child)] = name
    return names


# ---------- records

def both16(value):
    return struct.pack("<H", value) + struct.pack(">H", value)


def both32(value):
    return struct.pack("<I", value) + struct.pack(">I", value)


def record_date(moment):
    t = time.gmtime(moment)
    return bytes([t.tm_year - 1900, t.tm_mon, t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec, 0])


def volume_date(moment):
    return time.strftime("%Y%m%d%H%M%S", time.gmtime(moment)).encode() + b"00\x00"


def directory_record(name: bytes, extent: int, size: int, is_directory: bool, moment) -> bytes:
    length = 33 + len(name) + (1 if len(name) % 2 == 0 else 0)
    record = bytes([length, 0]) + both32(extent) + both32(size) + record_date(moment)
    record += bytes([2 if is_directory else 0, 0, 0]) + both16(1) + bytes([len(name)]) + name
    if len(name) % 2 == 0:
        record += b"\0"
    return record


def record_length(name: bytes) -> int:
    return 33 + len(name) + (1 if len(name) % 2 == 0 else 0)


def directory_size(directory: Node, names, space) -> int:
    """sectors' worth: no record crosses a sector"""
    used = 0
    lengths = [34, 34] + [record_length(names[id(child)]) for child in directory.sorted[space]]
    for length in lengths:
        if used % SECTOR + length > SECTOR:
            used += SECTOR - used % SECTOR
        used += length
    return (used + SECTOR - 1) // SECTOR * SECTOR


def directories_breadth_first(root: Node, space=None):
    order = [(root, None)]
    index = 0
    while index < len(order):
        directory, _ = order[index]
        for child in (directory.sorted[space] if space else directory.children):
            if child.children is not None:
                order.append((child, index + 1))
        index += 1
    return order


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("image")
    parser.add_argument("paths", nargs="+")
    parser.add_argument("--label", default="OPENCE")
    args = parser.parse_args()

    children = []
    for path in args.paths:
        path = wsl_path(path)
        if not os.path.exists(path):
            sys.exit(f"{path}: not found")
        children.append(tree(path, os.path.basename(os.path.normpath(path))))
    root = Node("", None, children)
    moment = time.time()

    names = {}
    orders = {}
    for space, namer in NAMESPACES.items():
        names[space] = {}
        orders[space] = directories_breadth_first(root)
        for directory, _ in orders[space]:
            names[space].update(unique_names(directory, namer))
            # (sorted by name, as ISO 9660 has a directory's records)
            directory.sorted[space] = sorted(directory.children, key=lambda child, s=space: names[s][id(child)])
        # (the order breadth first, with each folder's children sorted)
        orders[space] = directories_breadth_first(root, space)

    # path tables
    def path_table(space, little):
        table = b""
        for directory, parent in orders[space]:
            name = b"\0" if directory is root else names[space][id(directory)]
            extent = directory.directory[space][0]
            table += bytes([len(name), 0]) + (struct.pack("<IH", extent, parent or 1) if little else
                                              struct.pack(">IH", extent, parent or 1)) + name
            if len(name) % 2:
                table += b"\0"
        return table

    # layout: system area, the two descriptors and the terminator, four path
    # tables a namespace, the directories, the files
    def path_table_size(space):
        size = 0
        for directory, _ in orders[space]:
            name_length = 1 if directory is root else len(names[space][id(directory)])
            size += 8 + name_length + name_length % 2
        return size

    sector = 19
    table_sectors = {}
    for space in NAMESPACES:
        size = path_table_size(space)
        count = (size + SECTOR - 1) // SECTOR
        table_sectors[space] = (sector, sector + count, size)
        sector += 2 * count
    for space in NAMESPACES:
        for directory, _ in orders[space]:
            size = directory_size(directory, names[space], space)
            directory.directory[space] = (sector, size)
            sector += size // SECTOR
    files = []

    def collect(node):
        for child in node.children:
            if child.children is None:
                files.append(child)
            else:
                collect(child)
    collect(root)
    for node in files:
        node.extent = sector if node.size else 0
        sector += (node.size + SECTOR - 1) // SECTOR
    total = sector

    with open(args.image, "wb") as image:
        def at(lba):
            image.seek(lba * SECTOR)

        # descriptors
        for number, space in ((16, "primary"), (17, "joliet")):
            joliet = space == "joliet"

            def text(value, length, j=joliet):
                if j:
                    encoded = value.encode("utf-16-be")
                    encoded += b"\0 " * ((length - len(encoded)) // 2)
                    return encoded.ljust(length, b" ")[:length]
                return value.encode("ascii").ljust(length, b" ")[:length]

            root_extent, root_size = root.directory[space]
            table_l, table_m, table_size = table_sectors[space]
            descriptor = bytearray(SECTOR)
            descriptor[0] = 2 if joliet else 1
            descriptor[1:6] = b"CD001"
            descriptor[6] = 1
            descriptor[8:40] = text("", 32)
            descriptor[40:72] = text(args.label[:16] if joliet else args.label.upper()[:32], 32)
            descriptor[80:88] = both32(total)
            if joliet:
                descriptor[88:91] = b"%/E"
            descriptor[120:124] = both16(1)
            descriptor[124:128] = both16(1)
            descriptor[128:132] = both16(SECTOR)
            descriptor[132:140] = both32(table_size)
            descriptor[140:144] = struct.pack("<I", table_l)
            descriptor[148:152] = struct.pack(">I", table_m)
            descriptor[156:190] = directory_record(b"\0", root_extent, root_size, True, moment)
            descriptor[190:318] = text("", 128)
            descriptor[318:446] = text("", 128)
            descriptor[446:574] = text("", 128)
            descriptor[574:702] = text("OPENCE MAKE_ISO", 128)
            descriptor[702:813] = text("", 111)
            descriptor[813:830] = volume_date(moment)
            descriptor[830:847] = volume_date(moment)
            descriptor[847:864] = b"0" * 16 + b"\0"
            descriptor[864:881] = b"0" * 16 + b"\0"
            descriptor[881] = 1
            at(number)
            image.write(descriptor)
        terminator = bytearray(SECTOR)
        terminator[0] = 255
        terminator[1:6] = b"CD001"
        terminator[6] = 1
        at(18)
        image.write(terminator)

        for space in NAMESPACES:
            table_l, table_m, _ = table_sectors[space]
            at(table_l)
            image.write(path_table(space, True))
            at(table_m)
            image.write(path_table(space, False))

        for space in NAMESPACES:
            for directory, parent in orders[space]:
                extent, size = directory.directory[space]
                parent_node = root if parent is None else orders[space][parent - 1][0]
                parent_extent, parent_size = parent_node.directory[space]
                records = [directory_record(b"\0", extent, size, True, moment),
                           directory_record(b"\1", parent_extent, parent_size, True, moment)]
                for child in directory.sorted[space]:
                    if child.children is None:
                        records.append(directory_record(names[space][id(child)], child.extent, child.size, False,
                                                        moment))
                    else:
                        child_extent, child_size = child.directory[space]
                        records.append(directory_record(names[space][id(child)], child_extent, child_size, True,
                                                        moment))
                data = bytearray()
                for record in records:
                    if len(data) % SECTOR + len(record) > SECTOR:
                        data += b"\0" * (SECTOR - len(data) % SECTOR)
                    data += record
                at(extent)
                image.write(bytes(data).ljust(size, b"\0"))

        for node in files:
            if not node.size:
                continue
            at(node.extent)
            with open(node.source, "rb") as source:
                while True:
                    chunk = source.read(1 << 20)
                    if not chunk:
                        break
                    image.write(chunk)
        image.truncate(total * SECTOR)

    print(f"{args.image}: {len(files)} files, {total * SECTOR / 1048576:.1f} MB")


if __name__ == "__main__":
    main()
