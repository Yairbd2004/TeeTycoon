"""Small reader/writer for Teeworlds DATA v4 maps used by the TeeTycoon map work."""

from __future__ import annotations

import struct
import zlib
from pathlib import Path
from collections import defaultdict


def ints(data: bytes) -> tuple[int, ...]:
    return struct.unpack(f"<{len(data) // 4}i", data)


class MapFile:
    def __init__(self, path: str | Path):
        self.path = Path(path)
        self.content = self.path.read_bytes()
        b = self.content
        if b[:4] != b"DATA":
            raise ValueError(f"{path}: not a DATA map")
        self.version, self.size, self.swaplen, nt, ni, nd, itemsize, datasize = struct.unpack_from("<8i", b, 4)
        if self.version != 4:
            raise ValueError(f"{path}: unsupported DATA version {self.version}")
        off = 36
        self.types = [struct.unpack_from("<3i", b, off + i * 12) for i in range(nt)]
        off += nt * 12
        self.item_offsets = list(struct.unpack_from(f"<{ni}i", b, off))
        off += ni * 4
        self.data_offsets = list(struct.unpack_from(f"<{nd}i", b, off))
        off += nd * 4
        self.data_sizes = list(struct.unpack_from(f"<{nd}i", b, off))
        off += nd * 4
        self.item_start = off
        self.data_start = off + itemsize
        if self.data_start + datasize != len(b):
            raise ValueError(f"{path}: section sizes do not match file size")
        self.items = []
        for i, io in enumerate(self.item_offsets):
            typ_id, length = struct.unpack_from("<Ii", b, self.item_start + io)
            payload = b[self.item_start + io + 8:self.item_start + io + 8 + length]
            self.items.append((typ_id >> 16, typ_id & 0xFFFF, payload))
        self._data_cache = {}

    def data(self, index: int) -> bytes:
        if index < 0 or index >= len(self.data_offsets):
            return b""
        if index not in self._data_cache:
            start = self.data_start + self.data_offsets[index]
            end = self.data_start + (self.data_offsets[index + 1] if index + 1 < len(self.data_offsets) else len(self.content) - self.data_start)
            result = zlib.decompress(self.content[start:end])
            if len(result) != self.data_sizes[index]:
                raise ValueError(f"{self.path}: data {index} decompressed to wrong size")
            self._data_cache[index] = result
        return self._data_cache[index]

    def data_string(self, index: int) -> str:
        return self.data(index).split(b"\0", 1)[0].decode("utf-8", errors="replace")

    def layers(self):
        for typ, item_id, payload in self.items:
            if typ != 5:
                continue
            v = ints(payload)
            if len(v) < 3:
                continue
            if v[1] == 2 and len(v) >= 15:
                layer = dict(id=item_id, type="tiles", version=v[3], width=v[4], height=v[5], flags=v[6], image=v[13], data=v[14], fields=v)
                if len(v) >= 23:
                    layer.update(name=decode_name(v[15:18]), tele=v[18], speedup=v[19], front=v[20], switch=v[21], tune=v[22])
                elif len(v) >= 20:
                    layer.update(tele=v[15], speedup=v[16], front=v[17], switch=v[18], tune=v[19])
                yield layer
            elif v[1] == 3:
                yield dict(id=item_id, type="quads", fields=v)
            else:
                yield dict(id=item_id, type=f"other-{v[1]}", fields=v)

    def tiles(self, layer) -> bytes:
        fields = layer["fields"]
        flags = layer["flags"]
        index = layer["data"]
        if flags & 2:
            index = layer.get("tele", index)
        elif flags & 4:
            index = layer.get("speedup", index)
        elif flags & 8:
            index = layer.get("front", index)
        elif flags & 16:
            index = layer.get("switch", index)
        elif flags & 32:
            index = layer.get("tune", index)
        return self.data(index)


def decode_name(values) -> str:
    encoded = b"".join(struct.pack(">I", v & 0xFFFFFFFF) for v in values)
    return bytes((b - 128) & 0xFF for b in encoded).split(b"\0", 1)[0].decode("utf-8", errors="replace")


def write_png(path: str | Path, width: int, height: int, rgba: bytes) -> None:
    if len(rgba) != width * height * 4:
        raise ValueError("RGBA buffer has the wrong size")

    def chunk(kind: bytes, data: bytes) -> bytes:
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF)

    scanlines = b"".join(b"\x00" + rgba[y * width * 4:(y + 1) * width * 4] for y in range(height))
    output = b"\x89PNG\r\n\x1a\n"
    output += chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0))
    output += chunk(b"IDAT", zlib.compress(scanlines, 9))
    output += chunk(b"IEND", b"")
    Path(path).write_bytes(output)


def encode_name(text: str, words: int = 3) -> tuple[int, ...]:
    raw = text.encode("utf-8")[:words * 4 - 1].ljust(words * 4, b"\0")
    encoded = bytes((b + 128) & 0xFF for b in raw)
    return tuple(struct.unpack(">i", encoded[i:i + 4])[0] for i in range(0, words * 4, 4))


def write_map(path: str | Path, items: list[tuple[int, int, bytes]], data: list[bytes]) -> None:
    """Write a DATA v4 file in the same section order as CDataFileWriter."""
    sorted_items = sorted(items, key=lambda item: item[0])
    types = defaultdict(list)
    for item in sorted_items:
        if len(item[2]) % 4:
            raise ValueError("map item payloads must be aligned to four bytes")
        types[item[0]].append(item)
    item_blob = bytearray()
    item_offsets = []
    for typ, item_id, payload in sorted_items:
        item_offsets.append(len(item_blob))
        item_blob += struct.pack("<Ii", (typ << 16) | item_id, len(payload)) + payload
    compressed = [zlib.compress(blob, 9) for blob in data]
    data_offsets = []
    pos = 0
    for blob in compressed:
        data_offsets.append(pos)
        pos += len(blob)
    types_blob = bytearray()
    start = 0
    for typ, same_type in types.items():
        types_blob += struct.pack("<3i", typ, start, len(same_type))
        start += len(same_type)
    offsets_blob = struct.pack(f"<{len(item_offsets)}i", *item_offsets)
    data_offsets_blob = struct.pack(f"<{len(data_offsets)}i", *data_offsets)
    sizes_blob = struct.pack(f"<{len(data)}i", *(len(blob) for blob in data))
    swap_size = 36 + len(types_blob) + len(offsets_blob) + len(data_offsets_blob) + len(sizes_blob) + len(item_blob)
    file_size = swap_size + pos
    header = b"DATA" + struct.pack("<8i", 4, file_size - 16, swap_size - 16, len(types), len(sorted_items), len(data), len(item_blob), pos)
    Path(path).write_bytes(header + types_blob + offsets_blob + data_offsets_blob + sizes_blob + item_blob + b"".join(compressed))
