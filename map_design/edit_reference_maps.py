"""Add TeeTycoon homes to the two supplied DDNet map designs.

The original game/front/tele layers are copied without edits. New geometry and
custom tiles are placed only in verified empty areas; the reference map art is
reused to skin the additions.
"""

from __future__ import annotations

import collections
import sys
from pathlib import Path

from ddmap import MapFile, ints, write_map

ROOT = Path(__file__).resolve().parents[1]
HOUSE_TELE_NUMBER = 254
TILE_SOLID = 1
TILE_MONEY = 160
TILE_VIP = 176
TILE_HOME = 184
TELE_OUT = 27


def replace_tile(data: bytearray, width: int, x: int, y: int, index: int, flags: int = 0) -> None:
    off = (y * width + x) * 4
    data[off:off + 4] = bytes((index, flags, 0, 0))


def replace_tele(data: bytearray, width: int, x: int, y: int, number: int, kind: int) -> None:
    off = (y * width + x) * 2
    data[off:off + 2] = bytes((number, kind))


def masks_from_game(data: bytes, width: int, height: int) -> dict[tuple[int, int], int]:
    def solid(x: int, y: int) -> bool:
        if not (0 <= x < width and 0 <= y < height):
            return False
        return data[(y * width + x) * 4] in (TILE_SOLID, 3, TILE_MONEY, TILE_VIP)

    masks = {}
    for y in range(height):
        for x in range(width):
            if not solid(x, y):
                continue
            mask = 0
            for bit, (dx, dy) in enumerate(((-1, -1), (0, -1), (1, -1), (-1, 0), (1, 0), (-1, 1), (0, 1), (1, 1))):
                if solid(x + dx, y + dy):
                    mask |= 1 << bit
            masks[(x, y)] = mask
    return masks


def art_lookup(game: bytes, art: bytes, width: int, height: int) -> dict[int, int]:
    masks = masks_from_game(game, width, height)
    by_mask: dict[int, collections.Counter[int]] = collections.defaultdict(collections.Counter)
    for (x, y), mask in masks.items():
        index = art[(y * width + x) * 4]
        if index:
            by_mask[mask][index] += 1
    if not by_mask:
        return {}
    result = {}
    for mask in range(256):
        exact = by_mask.get(mask)
        if exact:
            result[mask] = exact.most_common(1)[0][0]
        else:
            nearest = min(by_mask, key=lambda candidate: ((candidate ^ mask).bit_count(), -sum(by_mask[candidate].values())))
            result[mask] = by_mask[nearest].most_common(1)[0][0]
    return result


def make_recolorer(atlas: bytes, width: int, height: int):
    """Build a fast lookup of same-silhouette tile cells for palette choices."""
    import numpy as np

    if width != 1024 or height != 1024:
        return lambda tile_id, rgb: tile_id
    image = np.frombuffer(atlas, dtype=np.uint8).reshape(height, width, 4)
    by_shape: dict[bytes, list[tuple[int, tuple[float, float, float]]]] = collections.defaultdict(list)
    for other in range(256):
        oy, ox = divmod(other, 32)
        candidate = image[oy * 32:(oy + 1) * 32, ox * 32:(ox + 1) * 32]
        mask = candidate[:, :, 3]
        if mask.any():
            pixels = candidate[mask > 0, :3]
            by_shape[mask.tobytes()].append((other, tuple(float(v) for v in pixels.mean(axis=0))))

    def recolor(tile_id: int, rgb: tuple[int, int, int]) -> int:
        if tile_id >= 256:
            return tile_id
        y, x = divmod(tile_id, 32)
        tile = image[y * 32:(y + 1) * 32, x * 32:(x + 1) * 32]
        mask = tile[:, :, 3]
        candidates = by_shape.get(mask.tobytes(), [])
        if not candidates:
            return tile_id
        target = np.array(rgb)
        return min(candidates, key=lambda item: float(np.square(np.array(item[1]) - target).sum()))[0]

    return recolor


def build(path: Path, destination: Path, *, layout: str) -> None:
    original = MapFile(path)
    items = list(original.items)
    data = [original.data(i) for i in range(len(original.data_offsets))]
    layers = {layer["id"]: layer for layer in original.layers() if layer["type"] == "tiles"}
    by_key = {(layer.get("name", ""), layer["flags"]): layer for layer in layers.values()}

    game = next(layer for layer in layers.values() if layer.get("name") == "Game")
    front = next((layer for layer in layers.values() if layer.get("name") == "Front"), None)
    tele = next(layer for layer in layers.values() if layer.get("name") == "Tele")
    game_raw = bytearray(original.tiles(game))
    front_raw = bytearray(original.tiles(front)) if front is not None else None
    tele_raw = bytearray(original.tiles(tele))
    width, height = game["width"], game["height"]
    if (front is not None and (front["width"] != width or front["height"] != height)) or tele["width"] != width or tele["height"] != height:
        raise ValueError(f"{path.name}: Game, Front, and Tele dimensions do not match")

    if layout == "royal":
        module_width, room_height, gap, start_x, top = 70, 45, 4, 592, 516
        art_layer = layers[47]  # the original map's hookable stonework layer
        art_raw = bytearray(original.tiles(art_layer))
        art_mask_lookup = art_lookup(original.tiles(game), original.tiles(art_layer), width, height)
        image_item = next(payload for typ, item_id, payload in items if typ == 2 and item_id == art_layer["image"])
        image = ints(image_item)
        atlas = original.data(image[5])
        atlas_width, atlas_height = image[1], image[2]
        recolor = make_recolorer(atlas, atlas_width, atlas_height)
        colors = [(134, 138, 146), (142, 143, 148), (154, 134, 99), (153, 117, 82), (179, 151, 93), (193, 157, 82)]
        art_data_index = art_layer["data"]
        room_fill_tile = 66
    elif layout == "copy":
        module_width, room_height, gap, start_x, top = 44, 36, 1, 68, 176
        art_layer = layers[14]  # the original map's grass/earth tile art
        art_raw = bytearray(original.tiles(art_layer))
        art_data_index = art_layer["data"]
        atlas = b""
        atlas_width = atlas_height = 0
        colors = []
        art_mask_lookup = {}
        recolor = lambda tile_id, rgb: tile_id
        room_fill_tile = 86
    else:
        raise ValueError(layout)

    # Tier rooms grow richer from left to right; the last room is the VIP lounge.
    room_specs = [("home", level, start_x + level * (module_width + gap)) for level in range(5)]
    room_specs.append(("vip", 5, start_x + 5 * (module_width + gap)))
    if room_specs[-1][2] + module_width > width or top + room_height >= height:
        raise ValueError(f"{path.name}: planned home wing is outside the existing map bounds")

    added_solid: dict[tuple[int, int], int] = {}
    accent_cells: dict[tuple[int, int], int] = {}
    home_tiles: list[tuple[int, int]] = []
    vip_tiles: set[tuple[int, int]] = set()
    front_edits: set[tuple[int, int]] = set()
    home_outs: list[tuple[int, int]] = []

    for kind, level, left in room_specs:
        widths = (38, 46, 54, 62, 70, 70) if layout == "royal" else (26, 30, 34, 38, 42, 42)
        heights = (25, 29, 34, 39, 45, 45) if layout == "royal" else (20, 24, 28, 32, 36, 36)
        room_width = widths[level]
        room_depth = heights[level]
        anchor = left
        left += (module_width - room_width) // 2
        right = left + room_width - 1
        floor = top + room_height - 1
        room_top = floor - room_depth + 1
        # A clean, walkable stone/earth shell with a side entry; the old map is untouched.
        for x in range(left, right + 1):
            added_solid[(x, room_top)] = TILE_SOLID
            added_solid[(x, floor)] = TILE_SOLID
        for y in range(room_top + 1, floor):
            added_solid[(left, y)] = TILE_SOLID
            added_solid[(right, y)] = TILE_SOLID
        added_solid.pop((left, floor - 1), None)
        if kind == "home" and level == 4:
            # The palace opens directly into the adjacent VIP sanctuary.
            added_solid.pop((right, floor - 1), None)

        # Progressive architecture: thicker cornices, towers, and inner balconies.
        center = (left + right) // 2
        crown_width = (0, 14, 26, 40, 54, 58)[level]
        for x in range(center - crown_width // 2, center + (crown_width + 1) // 2):
            added_solid[(x, room_top - 1)] = TILE_SOLID
        tower_height = (0, 0, 1, 2, 3, 4)[level]
        tower_width = (0, 0, 0, 4, 6, 8)[level]
        if tower_height:
            for tower_x in (left + 2, right - 2):
                for rise in range(tower_height):
                    added_solid[(tower_x, room_top - 1 - rise)] = TILE_SOLID
                    if tower_width:
                        for dx in range(1, tower_width // 2 + 1):
                            added_solid[(tower_x - dx, room_top - 1 - rise)] = TILE_SOLID
                            added_solid[(tower_x + dx, room_top - 1 - rise)] = TILE_SOLID

        if level >= 1:
            balcony_y = room_top + (room_depth * 2) // 3
            span = (0, 12, 26, 42, 54, 28)[level]
            for x in range(center - span // 2, center + (span + 1) // 2):
                added_solid[(x, balcony_y)] = TILE_SOLID
            if level >= 3:
                for x in (left + 5, right - 5):
                    for y in range(balcony_y + 1, floor):
                        added_solid[(x, y)] = TILE_SOLID
                upper_y = room_top + room_depth // 3
                upper_span = (0, 0, 0, 24, 38, 30)[level]
                for x in range(center - upper_span // 2, center + (upper_span + 1) // 2):
                    added_solid[(x, upper_y)] = TILE_SOLID
        if level >= 4:
            # A central second gallery in the highest houses; leave a broad passage under it.
            gallery_y = room_top + room_depth // 3
            for x in range(center - module_width // 3, center + module_width // 3):
                added_solid[(x, gallery_y)] = TILE_SOLID
            for x in (left + 7, right - 7):
                for y in range(gallery_y + 1, floor - 2):
                    added_solid[(x, y)] = TILE_SOLID

        if kind == "home":
            # Farm tiles form a continuous, tier-scaled strip above the stone floor.
            count = (8, 13, 19, 25, 37)[level]
            farm_y = floor - 1
            start = left + 2
            for x in range(start, start + count):
                added_solid[(x, farm_y)] = TILE_MONEY
            spawn = (left + 4, floor - 2)
            home_outs.append(spawn)
            if front_raw is not None:
                replace_tile(front_raw, width, spawn[0], spawn[1], TILE_HOME)
                front_edits.add(spawn)
            else:
                home_tiles.append(spawn)
        else:
            # The VIP zone is at the entrance to its own decorated, raised sanctuary.
            for y in range(top + 4, top + 7):
                for x in range(center - 2, center + 3):
                    replace_tile(game_raw, width, x, y, TILE_VIP)
                    vip_tiles.add((x, y))
            # Small, symmetric inner platforms make the lounge feel like its own room.
            for x in range(left + 5, left + 11):
                added_solid[(x, room_top + 11)] = TILE_SOLID
            for x in range(right - 10, right - 4):
                added_solid[(x, room_top + 11)] = TILE_SOLID

        if layout == "royal":
            # Two-color cornices and columns make each upgrade read clearly at a glance.
            for x in range(left + 2, right - 1):
                accent_cells[(x, room_top)] = level
            for y in range(room_top + 2, floor - 2):
                accent_cells[(left + 1, y)] = level
                accent_cells[(right - 1, y)] = level
            if level >= 2:
                for x in range(left + 2, right - 1):
                    accent_cells[(x, floor)] = level
            if kind == "vip":
                for y in range(room_top + 7, floor - 1):
                    accent_cells[(center - 2, y)] = level
                    accent_cells[(center + 2, y)] = level

        # Richly tile the room interiors so each home reads as a finished place,
        # rather than an empty shell. The map's original atlas supplies every texture.
        for y in range(room_top + 1, floor):
            for x in range(left + 1, right):
                if (x, y) not in added_solid:
                    fill_tile = room_fill_tile
                    if layout == "royal":
                        fill_tile = recolor(fill_tile, colors[level])
                    else:
                        # Broad panels of slightly different native earth tiles keep
                        # the interior alive without introducing foreign map art.
                        panel = ((x - left) // 6 + (y - top) // 4 + level) % 4
                        fill_tile = (86, 84, 90, 82)[panel]
                    replace_tile(art_raw, art_layer["width"], x, y, fill_tile)
        # A clear entrance threshold and a polished upper trim make the tiers readable.
        for x in range(left + 1, right):
            replace_tile(art_raw, art_layer["width"], x, room_top, room_fill_tile)
        if layout == "copy":
            # Small grassy ledges frame the evolving earth homes in their own atlas.
            trim = (64, 65, 70, 67, 70, 65)[level]
            for x in range(left + 1, right):
                replace_tile(art_raw, art_layer["width"], x, room_top - 1, trim)
            for x, y in added_solid:
                if left <= x <= right and room_top <= y <= floor:
                    replace_tile(art_raw, art_layer["width"], x, y, room_fill_tile)

    # Modify only cells belonging to the new home wing.
    for (x, y), tile in added_solid.items():
        replace_tile(game_raw, width, x, y, tile)
    for x, y in home_tiles:
        replace_tile(game_raw, width, x, y, TILE_HOME)
    game_edits = set(added_solid) | set(home_tiles) | vip_tiles

    # House/VIP tele-out number is reserved by TeeTycoon and does not collide with
    # any of the supplied maps' original race or event teleports.
    for x, y in home_outs:
        replace_tele(tele_raw, width, x, y, HOUSE_TELE_NUMBER, TELE_OUT)

    if layout == "royal":
        masks = masks_from_game(bytes(game_raw), width, height)
        for (x, y), mask in masks.items():
            if (x, y) not in added_solid:
                continue
            tile_id = art_mask_lookup[mask]
            room_level = min(5, max(0, (x - start_x) // (module_width + gap)))
            if (x, y) in accent_cells:
                tile_id = recolor(tile_id, colors[room_level])
            replace_tile(art_raw, width, x, y, tile_id)
    else:
        # Match the existing solid-rock art and add the grassy interior backdrop above it.
        ground_layer = layers[19]
        ground_raw = bytearray(original.tiles(ground_layer))
        for (x, y), tile in added_solid.items():
            if tile == TILE_SOLID:
                replace_tile(ground_raw, ground_layer["width"], x, y, 16)

    def put_data(layer, raw: bytes) -> None:
        flags = layer["flags"]
        if flags & 2:
            index = layer["tele"]
        elif flags & 4:
            index = layer["speedup"]
        elif flags & 8:
            index = layer["front"]
        elif flags & 16:
            index = layer["switch"]
        elif flags & 32:
            index = layer["tune"]
        else:
            index = layer["data"]
        data[index] = raw

    put_data(game, bytes(game_raw))
    if front is not None:
        put_data(front, bytes(front_raw))
    put_data(tele, bytes(tele_raw))
    put_data(art_layer, bytes(art_raw))
    if layout == "copy":
        put_data(ground_layer, bytes(ground_raw))
    if not any(tele_raw[i] == HOUSE_TELE_NUMBER and tele_raw[i + 1] == TELE_OUT for i in range(0, len(tele_raw), 2)):
        raise ValueError("Reserved tele-outs were not written to the Tele layer buffer")
    destination.parent.mkdir(parents=True, exist_ok=True)
    write_map(destination, items, data)

    # Parse the result again and verify the freeze layer and old physics are unchanged.
    checked = MapFile(destination)
    checked_layers = {layer["id"]: layer for layer in checked.layers() if layer["type"] == "tiles"}
    if layout == "royal":
        if checked.tiles(checked_layers[41]) != original.tiles(layers[41]):
            raise ValueError("The Royal freeze artwork/layer was modified")
    elif checked.tiles(checked_layers[18]) != original.tiles(layers[18]):
        raise ValueError("The Copy Love Box freeze artwork/layer was modified")
    if len(home_outs) != 5:
        raise ValueError("Expected five house teleports")
    out_data = checked.tiles(checked_layers[tele["id"]])
    tele_destinations = [(x, y) for y in range(height) for x in range(width)
                         if out_data[(y * width + x) * 2:(y * width + x) * 2 + 2] == bytes((HOUSE_TELE_NUMBER, TELE_OUT))]
    if len(tele_destinations) != 5:
        raise ValueError(f"Expected five reserved house tele-outs, found {len(tele_destinations)}")
    source_game = original.tiles(layers[game["id"]])
    result_game = checked.tiles(checked_layers[game["id"]])
    for y in range(height):
        for x in range(width):
            if (x, y) in game_edits:
                continue
            off = (y * width + x) * 4
            if source_game[off:off + 4] != result_game[off:off + 4]:
                raise ValueError(f"Existing game geometry changed at tile {x},{y}")
    if front is not None:
        source_front = original.tiles(layers[front["id"]])
        result_front = checked.tiles(checked_layers[front["id"]])
        for y in range(height):
            for x in range(width):
                if (x, y) in front_edits:
                    continue
                off = (y * width + x) * 4
                if source_front[off:off + 4] != result_front[off:off + 4]:
                    raise ValueError(f"Existing front geometry changed at tile {x},{y}")
    source_tele = original.tiles(layers[tele["id"]])
    for y in range(height):
        for x in range(width):
            if (x, y) in home_outs:
                continue
            off = (y * width + x) * 2
            if source_tele[off:off + 2] != out_data[off:off + 2]:
                raise ValueError(f"Existing teleporter geometry changed at tile {x},{y}")
    print(f"{destination}: 5 homes, 1 VIP room, {len(added_solid)} new structural/farm cells")


if __name__ == "__main__":
    sys.stdout.reconfigure(encoding="utf-8")
    build(ROOT / "map_examples" / "blmapV3ROYAL.map", ROOT / "TeeTycoon" / "data" / "maps" / "blmapV3ROYAL-TT.map", layout="royal")
    build(ROOT / "map_examples" / "Copy Love Box.map", ROOT / "TeeTycoon" / "data" / "maps" / "Copy Love Box-TT.map", layout="copy")
