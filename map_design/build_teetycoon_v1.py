"""Build TeeTycoonV1.map from original tile geometry and embedded artwork.

Run from the repository root with Pillow available to Python. The output stays
editable in the DDNet map editor.
"""

from __future__ import annotations

import os
import random
import struct
import sys
from pathlib import Path

sys.path.insert(0, os.path.join(os.environ.get("TEMP", ""), "teetycoon_map_deps"))
from PIL import Image, ImageDraw, ImageFont

from ddmap import encode_name, write_map, write_png


WIDTH, HEIGHT = 720, 450
OUT = Path("TeeTycoon/data/maps/TeeTycoonV1.map")
ATLAS = Path("map_design/TeeTycoonV1-atlas.png")
PREVIEW = Path("map_design/previews/TeeTycoonV1-layout.png")
ART_PREVIEW = Path("map_design/previews/TeeTycoonV1-art.png")
RNG = random.Random(20261010)

# In-game tile IDs from mapitems.h.
AIR, SOLID, DEATH, NOHOOK, FREEZE = 0, 1, 2, 3, 9
MONEY, VIP, SURVIVAL, RACE_FINISH = 160, 176, 179, 180
RAINBOW, BLOODY, FAST, HOME = 181, 182, 183, 184
SPAWN, SHOTGUN, GRENADE, NINJA, LASER = 192, 199, 200, 201, 202
TELE_IN, TELE_OUT = 26, 27

game = bytearray(WIDTH * HEIGHT * 4)
front = bytearray(WIDTH * HEIGHT * 4)
tele = bytearray(WIDTH * HEIGHT * 2)
arch = bytearray(WIDTH * HEIGHT * 4)
decor = bytearray(WIDTH * HEIGHT * 4)
background = bytearray(WIDTH * HEIGHT * 4)


def cell(layer: bytearray, x: int, y: int, index: int, flags: int = 0) -> None:
    if 0 <= x < WIDTH and 0 <= y < HEIGHT:
        offset = (y * WIDTH + x) * 4
        layer[offset:offset + 4] = bytes((index, flags, 0, 0))


def get(layer: bytearray, x: int, y: int) -> int:
    return layer[(y * WIDTH + x) * 4]


def rect(layer: bytearray, x0: int, y0: int, x1: int, y1: int, index: int) -> None:
    for y in range(max(0, y0), min(HEIGHT, y1)):
        for x in range(max(0, x0), min(WIDTH, x1)):
            cell(layer, x, y, index)


def line(layer: bytearray, x0: int, y0: int, x1: int, y1: int, index: int) -> None:
    dx, dy = abs(x1 - x0), -abs(y1 - y0)
    sx, sy = (1 if x0 < x1 else -1), (1 if y0 < y1 else -1)
    err = dx + dy
    while True:
        cell(layer, x0, y0, index)
        if x0 == x1 and y0 == y1:
            break
        e2 = 2 * err
        if e2 >= dy:
            err += dy
            x0 += sx
        if e2 <= dx:
            err += dx
            y0 += sy


def tele_tile(x: int, y: int, number: int, typ: int = TELE_OUT) -> None:
    if not (0 <= x < WIDTH and 0 <= y < HEIGHT):
        raise ValueError("teleporter outside map")
    tele[(y * WIDTH + x) * 2:(y * WIDTH + x) * 2 + 2] = bytes((number, typ))


def platform(x: int, y: int, width: int, material: int = 16, nohook: bool = False) -> None:
    rect(game, x, y, x + width, y + 2, NOHOOK if nohook else SOLID)
    rect(arch, x, y, x + width, y + 2, 18 if nohook else material)
    rect(decor, x, y - 1, x + width, y, 17 if not nohook else 18)


def freeze_pocket(x0: int, y0: int, x1: int, y1: int, floor: bool = True) -> None:
    rect(game, x0, y0, x1, y1, FREEZE)
    rect(decor, x0, y0, x1, y1, 19)
    if floor:
        rect(game, x0, y1, x1, y1 + 2, SOLID)
        rect(arch, x0, y1, x1, y1 + 2, 16)


def label(text: str, x: int, y: int, max_width: int | None = None) -> None:
    text = text.upper()
    if max_width is not None and len(text) > max_width:
        raise ValueError(f"label too long: {text}")
    for j, ch in enumerate(text):
        if ch == " ":
            continue
        if "A" <= ch <= "Z":
            index = 32 + ord(ch) - ord("A")
        elif "0" <= ch <= "9":
            index = 58 + ord(ch) - ord("0")
        else:
            continue
        cell(decor, x + j, y, index)


def make_geometry() -> None:
    # A dark, sparse sky is repeated with several variations, rather than a
    # single stamped pattern. The artwork is embedded in the map.
    for y in range(HEIGHT):
        for x in range(WIDTH):
            cell(background, x, y, 1 + RNG.randrange(8) if RNG.random() < .12 else 1)

    # Public arrival court. It joins the block area, the race entrance, and
    # the social lounge without sending players through a freeze obstacle.
    rect(game, 24, 132, 100, 135, SOLID)
    rect(arch, 24, 132, 100, 135, 23)
    rect(game, 22, 67, 25, 135, SOLID)
    rect(arch, 22, 67, 25, 135, 23)
    rect(game, 24, 65, 99, 67, SOLID)
    rect(arch, 24, 65, 99, 67, 23)
    rect(game, 97, 67, 100, 88, SOLID)
    rect(arch, 97, 67, 100, 88, 23)
    rect(game, 97, 111, 100, 135, SOLID)
    rect(arch, 97, 111, 100, 135, 23)
    platform(34, 117, 12, 23)
    platform(61, 103, 9, 23)
    cell(game, 52, 128, SPAWN)
    tele_tile(53, 128, 1)
    tele_tile(40, 128, 3, TELE_IN)
    rect(decor, 38, 127, 43, 128, 28)
    label("TEE TYCOON", 32, 73)
    label("LOBBY", 45, 84)
    label("BLOCK", 72, 102)
    label("RACE", 34, 119)
    for x in (28, 92):
        cell(decor, x, 125, 69)
    for x in (33, 58, 80):
        cell(decor, x, 83, 70)
    rect(decor, 45, 130, 66, 131, 76)
    rect(decor, 28, 126, 90, 127, 30)

    # Open block field: platforms are 3-12 tiles wide, with 9-18 tile vertical
    # spacing and adjacent hookable walls. Side and lower freeze pockets provide
    # reachable block goals, while the middle stays open for long hooks.
    rect(game, 99, 36, 467, 38, SOLID)
    rect(arch, 99, 36, 467, 38, 16)
    rect(game, 99, 38, 102, 225, SOLID)
    rect(arch, 99, 38, 102, 225, 16)
    rect(game, 99, 88, 102, 112, AIR)
    rect(arch, 99, 88, 102, 112, AIR)
    rect(game, 464, 38, 467, 225, SOLID)
    rect(arch, 464, 38, 467, 225, 16)
    rect(game, 464, 83, 467, 108, AIR)
    rect(arch, 464, 83, 467, 108, AIR)
    rect(game, 101, 223, 465, 226, SOLID)
    rect(arch, 101, 223, 465, 226, 16)
    label("CROSSFIRE", 247, 42)
    for x, y, w in [
        (117, 201, 10), (137, 184, 6), (156, 168, 8), (181, 151, 5),
        (210, 133, 7), (231, 113, 5), (256, 94, 9), (281, 75, 6),
        (319, 85, 7), (341, 105, 6), (360, 126, 8), (382, 150, 5),
        (415, 174, 8), (438, 194, 7), (298, 145, 5), (271, 170, 6),
        (245, 190, 5), (326, 188, 8), (365, 204, 7), (193, 202, 9),
        (160, 97, 5), (186, 83, 9), (128, 118, 6), (392, 88, 9),
        (420, 67, 6), (443, 116, 8), (351, 60, 6), (226, 65, 8),
    ]:
        platform(x, y, w, 16, nohook=(x, y) in {(181, 151), (326, 188), (392, 88)})
    # Stepped side walls create hook points and clean lines for block throws.
    rect(game, 176, 164, 179, 197, SOLID)
    rect(arch, 176, 164, 179, 197, 16)
    rect(game, 207, 93, 210, 120, SOLID)
    rect(arch, 207, 93, 210, 120, 16)
    rect(game, 305, 119, 308, 173, SOLID)
    rect(arch, 305, 119, 308, 173, 16)
    rect(game, 399, 139, 402, 190, NOHOOK)
    rect(arch, 399, 139, 402, 190, 18)
    for a, b in [(121, 137), (225, 242), (287, 303), (400, 419), (447, 461)]:
        freeze_pocket(a, 216, b, 223, floor=False)
    freeze_pocket(174, 124, 189, 129)
    freeze_pocket(337, 116, 354, 121)
    freeze_pocket(419, 74, 431, 79)
    freeze_pocket(105, 147, 110, 171, floor=False)
    freeze_pocket(454, 150, 463, 174, floor=False)
    # Safe shelves beside pits help a player rescue a frozen friend.
    platform(112, 207, 5, 23)
    platform(139, 207, 7, 23)
    platform(397, 207, 5, 23)
    platform(421, 207, 7, 23)
    rect(decor, 111, 41, 156, 42, 30)
    rect(decor, 401, 41, 454, 42, 30)

    # VIP social gallery connects at the arena's upper right. One tile of VIP
    # barrier is enough because the character's center must cross it.
    rect(game, 467, 48, 697, 51, SOLID)
    rect(arch, 467, 48, 697, 51, 24)
    rect(game, 694, 50, 697, 120, SOLID)
    rect(arch, 694, 50, 697, 120, 24)
    rect(game, 467, 117, 697, 120, SOLID)
    rect(arch, 467, 117, 697, 120, 24)
    rect(game, 467, 50, 470, 83, SOLID)
    rect(arch, 467, 50, 470, 83, 24)
    rect(game, 467, 108, 470, 120, SOLID)
    rect(arch, 467, 108, 470, 120, 24)
    rect(game, 475, 51, 480, 117, VIP)
    rect(decor, 475, 51, 480, 117, 27)
    rect(game, 519, 115, 548, 117, MONEY)
    rect(decor, 519, 115, 548, 117, 20)
    platform(500, 98, 16, 24)
    platform(553, 91, 13, 24)
    platform(600, 98, 16, 24)
    cell(game, 568, 113, SHOTGUN)
    cell(game, 612, 113, LASER)
    cell(game, 641, 114, RAINBOW)
    cell(game, 659, 114, BLOODY)
    label("VIP SKY LOUNGE", 547, 61)
    rect(decor, 501, 108, 677, 109, 27)
    for x in (493, 566, 684):
        cell(decor, x, 108, 78)
    for x in (510, 626):
        cell(decor, x, 72, 73)
    rect(decor, 546, 114, 601, 115, 76)

    make_event_arenas()
    make_race()
    make_houses()


def make_event_arenas() -> None:
    modes = [
        ("SURVIVAL", 254), ("RACE", 253), ("DEATHMATCH", 252),
        ("FREEZE RACE", 251), ("FNG", 250),
    ]
    for i, (name, number) in enumerate(modes):
        y0 = 129 + i * 32
        y1 = y0 + 29
        rect(game, 476, y0, 697, y0 + 2, SOLID)
        rect(arch, 476, y0, 697, y0 + 2, 23)
        rect(game, 476, y1 - 2, 697, y1, SOLID)
        rect(arch, 476, y1 - 2, 697, y1, 23)
        rect(game, 476, y0, 478, y1, SOLID)
        rect(game, 695, y0, 697, y1, SOLID)
        rect(arch, 476, y0, 478, y1, 23)
        rect(arch, 695, y0, 697, y1, 23)
        rect(game, 478, y0 + 2, 695, y1 - 2, SURVIVAL)
        label(name, 481, y0 + 3)
        for j in range(12):
            tele_tile(488 + 13 * j, y0 + 7, number)
        if i == 0:
            for x in (512, 564, 624, 669):
                platform(x, y0 + 12 + (x % 3) * 2, 9, 23)
            freeze_pocket(540, y1 - 7, 552, y1 - 2, floor=False)
            freeze_pocket(642, y1 - 7, 656, y1 - 2, floor=False)
        elif i in (1, 3):
            for x in (523, 559, 596, 632):
                platform(x, y0 + 15 - (x % 3) * 3, 6, 23)
            cell(game, 680, y1 - 6, RACE_FINISH)
            cell(decor, 680, y1 - 7, 81)
            if i == 3:
                freeze_pocket(550, y1 - 7, 561, y1 - 2, floor=False)
                freeze_pocket(610, y1 - 7, 622, y1 - 2, floor=False)
        else:
            for x in (533, 579, 630):
                platform(x, y0 + 14, 8, 23)
            if i == 4:
                freeze_pocket(550, y1 - 7, 565, y1 - 2, floor=False)
                freeze_pocket(621, y1 - 7, 635, y1 - 2, floor=False)


def make_race() -> None:
    # A shared race runs under the block field. Most falls return players to a
    # lower shelf; the final section adds a few meaningful freeze hazards.
    rect(game, 30, 296, 700, 299, SOLID)
    rect(arch, 30, 296, 700, 299, 23)
    rect(game, 30, 345, 700, 348, SOLID)
    rect(arch, 30, 345, 700, 348, 23)
    rect(game, 30, 299, 33, 348, SOLID)
    rect(game, 697, 299, 700, 348, SOLID)
    rect(arch, 30, 299, 33, 348, 23)
    rect(arch, 697, 299, 700, 348, 23)
    # Lobby shaft to the starting deck.
    rect(game, 34, 135, 37, 298, SOLID)
    rect(game, 83, 135, 86, 298, SOLID)
    rect(arch, 34, 135, 37, 298, 23)
    rect(arch, 83, 135, 86, 298, 23)
    # A physical route down the shaft remains available alongside the lobby
    # teleporter, so the race is part of the world rather than an isolated box.
    rect(game, 37, 132, 48, 135, AIR)
    rect(arch, 37, 132, 48, 135, AIR)
    rect(game, 46, 296, 68, 299, AIR)
    rect(arch, 46, 296, 68, 299, AIR)
    for y in range(151, 292, 16):
        platform(38 if (y // 16) % 2 else 68, y, 10, 23)
    rect(game, 44, 342, 66, 345, SOLID)
    rect(arch, 44, 342, 66, 345, 23)
    cell(game, 52, 339, 33)  # race start
    tele_tile(56, 339, 3)
    label("RACE", 46, 304)
    for x, y, w in [
        (77, 330, 10), (99, 316, 9), (124, 325, 8), (145, 307, 9),
        (169, 328, 10), (195, 316, 7), (217, 304, 8), (243, 321, 11),
        (272, 308, 7), (296, 326, 11), (325, 312, 8), (350, 302, 6),
        (373, 324, 10), (404, 309, 9), (432, 328, 7), (456, 311, 9),
        (485, 302, 8), (513, 324, 10), (545, 310, 9), (575, 330, 10),
        (606, 312, 8), (634, 303, 10), (665, 326, 11),
    ]:
        platform(x, y, w, 23, nohook=x in (195, 373, 575))
    # Floor pockets force players to time jumps, while short safe floors make
    # retries and helper play possible.
    for x0, x1 in [(113, 128), (255, 271), (386, 400), (535, 551), (619, 632)]:
        freeze_pocket(x0, 340, x1, 345, floor=False)
    freeze_pocket(341, 298, 349, 303, floor=False)
    freeze_pocket(472, 298, 481, 303, floor=False)
    cell(game, 681, 339, 34)  # race finish
    tele_tile(690, 339, 1, TELE_IN)
    label("FINISH", 652, 304)
    rect(decor, 41, 343, 90, 344, 30)
    rect(decor, 648, 343, 690, 344, 27)


def make_houses() -> None:
    # A death separator makes the five rooms reachable only through /home.
    rect(game, 18, 354, 701, 357, DEATH)
    rect(decor, 18, 354, 701, 357, 19)
    houses = [
        ("SHACK", 24, 95, 403, 21, 8),
        ("LOFT", 104, 192, 394, 22, 13),
        ("SUITE", 201, 302, 386, 25, 19),
        ("COURT", 311, 445, 376, 24, 25),
        ("PALACE", 454, 697, 365, 26, 37),
    ]
    for level, (name, x0, x1, top, material, farm_width) in enumerate(houses):
        floor = 442
        rect(game, x0, top, x1, top + 3, SOLID)
        rect(game, x0, top, x0 + 3, floor, SOLID)
        rect(game, x1 - 3, top, x1, floor, SOLID)
        rect(game, x0, floor - 3, x1, floor, SOLID)
        for x in range(x0, x1):
            for y in list(range(top, top + 3)) + list(range(floor - 3, floor)):
                cell(arch, x, y, material)
        rect(arch, x0, top, x0 + 3, floor, material)
        rect(arch, x1 - 3, top, x1, floor, material)
        rect(front, x0 + 3, top + 3, x1 - 3, floor - 3, HOME)
        farm_x = (x0 + x1 - farm_width) // 2
        rect(game, farm_x, floor - 4, farm_x + farm_width, floor - 3, MONEY)
        rect(decor, farm_x, floor - 4, farm_x + farm_width, floor - 3, 20)
        tele_tile((x0 + x1) // 2, 415, 2)
        label(name, x0 + 7, top + 5)
        label(str(level), x1 - 10, top + 5)
        # The entrance sits on the same row in every house, so the engine's
        # row-major TeleOuts(1) list exactly matches levels 0..4.
        if level == 0:
            rect(decor, x0 + 4, floor - 9, x0 + 20, floor - 8, 21)
            platform(x0 + 7, 426, 8, 21)
            for x, y in [(x0 + 11, 428), (x1 - 18, 434), (x1 - 12, 434)]:
                cell(decor, x, y, 68)
            cell(decor, x0 + 6, floor - 10, 69)
            cell(decor, x1 - 8, floor - 10, 69)
        elif level == 1:
            platform(x0 + 10, 422, 15, material)
            platform(x1 - 26, 408, 12, material)
            cell(game, x1 - 14, floor - 7, SHOTGUN)
            rect(decor, x0 + 6, top + 12, x0 + 9, top + 20, 30)
            for x in (x0 + 18, x1 - 18):
                cell(decor, x, top + 9, 70)
            cell(decor, x0 + 8, floor - 6, 72)
            cell(decor, x1 - 8, floor - 6, 71)
            rect(decor, x0 + 28, floor - 3, x1 - 28, floor - 2, 76)
        elif level == 2:
            platform(x0 + 12, 417, 19, material)
            platform(x1 - 34, 401, 20, material)
            cell(game, x0 + 19, floor - 7, SHOTGUN)
            cell(game, x1 - 18, floor - 7, GRENADE)
            cell(game, x1 - 25, floor - 7, RAINBOW)
            rect(decor, x0 + 7, top + 10, x0 + 10, top + 24, 30)
            for x in (x0 + 18, (x0 + x1) // 2, x1 - 18):
                cell(decor, x, top + 9, 70)
            for x in (x0 + 9, x1 - 10):
                cell(decor, x, floor - 6, 71)
            cell(decor, x0 + 27, floor - 6, 72)
            cell(decor, x1 - 29, floor - 6, 72)
            rect(decor, x0 + 31, floor - 3, x1 - 31, floor - 2, 76)
            cell(decor, (x0 + x1) // 2, top + 14, 73)
        elif level == 3:
            platform(x0 + 15, 416, 24, material)
            platform(x1 - 39, 399, 27, material)
            platform((x0 + x1) // 2 - 12, 390, 24, 27)
            for x, entity in [(x0 + 20, SHOTGUN), (x0 + 35, GRENADE), (x1 - 19, LASER)]:
                cell(game, x, floor - 7, entity)
            cell(game, x1 - 29, floor - 7, BLOODY)
            cell(decor, (x0 + x1) // 2, top + 10, 81)
            for x in (x0 + 10, x0 + 40, x1 - 42, x1 - 12):
                cell(decor, x, top + 12, 70)
                cell(decor, x, top + 8, 77)
            for x in (x0 + 16, x1 - 17):
                cell(decor, x, floor - 6, 78)
            for x in (x0 + 57, x1 - 58):
                cell(decor, x, top + 19, 73)
            cell(decor, (x0 + x1) // 2, floor - 7, 74)
            rect(decor, x0 + 28, floor - 3, x1 - 28, floor - 2, 76)
        else:
            # A large, multi-level palace with columns, floating balconies,
            # a central crown, and separate farming and social corners.
            for x in range(x0 + 22, x1 - 22, 33):
                rect(decor, x, top + 9, x + 2, floor - 11, 27)
            for x, y, w in [(x0 + 20, 424, 29), (x0 + 64, 406, 27), (x0 + 110, 390, 32), (x0 + 160, 406, 27), (x1 - 44, 424, 27)]:
                platform(x, y, w, 26)
            for x, entity in [(x0 + 30, SHOTGUN), (x0 + 48, GRENADE), (x0 + 70, LASER), (x1 - 30, NINJA)]:
                cell(game, x, floor - 7, entity)
            for x, pickup in [(x1 - 64, RAINBOW), (x1 - 56, BLOODY), (x1 - 48, FAST)]:
                cell(game, x, floor - 7, pickup)
            cell(decor, (x0 + x1) // 2, top + 8, 81)
            rect(decor, x0 + 8, top + 9, x0 + 10, floor - 9, 27)
            rect(decor, x1 - 10, top + 9, x1 - 8, floor - 9, 27)
            for x in range(x0 + 17, x1 - 15, 31):
                cell(decor, x, top + 8, 77)
                cell(decor, x + 8, top + 14, 70)
            for x in (x0 + 27, x0 + 83, x0 + 139, x1 - 28):
                cell(decor, x, top + 21, 73)
            for x in (x0 + 12, x1 - 13):
                cell(decor, x, floor - 6, 78)
            for x in (x0 + 61, x1 - 62):
                cell(decor, x, floor - 6, 71)
            cell(decor, (x0 + x1) // 2, floor - 8, 74)
            rect(decor, x0 + 34, floor - 3, x1 - 34, floor - 2, 76)


def make_atlas() -> bytes:
    atlas = Image.new("RGBA", (512, 512), (0, 0, 0, 0))
    draw = ImageDraw.Draw(atlas)

    def box(tile: int) -> tuple[int, int, int, int]:
        x, y = tile % 16 * 32, tile // 16 * 32
        return x, y, x + 31, y + 31

    # Starfield, with eight subtle variations.
    for tile in range(1, 9):
        x0, y0, x1, y1 = box(tile)
        draw.rectangle((x0, y0, x1, y1), fill=(14, 25, 42, 255))
        if tile != 1:
            r = random.Random(tile * 121)
            for _ in range(tile % 3 + 1):
                x, y = x0 + r.randrange(2, 31), y0 + r.randrange(2, 31)
                draw.point((x, y), fill=(77, 111, 145, 110))

    palettes = {
        16: ((78, 93, 111), (119, 135, 150), (43, 56, 71)),
        17: ((136, 151, 165), (172, 190, 202), (72, 90, 108)),
        18: ((40, 47, 60), (62, 69, 83), (23, 28, 40)),
        19: ((58, 147, 181), (115, 228, 240), (26, 81, 116)),
        20: ((149, 104, 34), (252, 216, 100), (89, 54, 29)),
        21: ((92, 62, 49), (132, 96, 69), (49, 39, 36)),
        22: ((92, 104, 127), (149, 166, 193), (49, 60, 77)),
        23: ((55, 87, 107), (83, 145, 167), (31, 55, 72)),
        24: ((187, 195, 198), (224, 228, 224), (102, 135, 154)),
        25: ((76, 62, 119), (138, 99, 169), (42, 37, 76)),
        26: ((189, 202, 205), (246, 247, 225), (97, 143, 159)),
        27: ((159, 111, 42), (248, 216, 118), (89, 59, 35)),
        28: ((59, 123, 150), (120, 216, 231), (37, 74, 106)),
        29: ((109, 70, 63), (208, 144, 103), (60, 43, 47)),
        30: ((19, 80, 91), (79, 229, 206), (16, 47, 63)),
        31: ((45, 96, 67), (127, 178, 100), (25, 58, 50)),
    }
    for tile, (base, light, shadow) in palettes.items():
        x0, y0, x1, y1 = box(tile)
        draw.rectangle((x0, y0, x1, y1), fill=base + (255,))
        draw.line((x0, y1, x1, y1), fill=shadow + (255,), width=3)
        draw.line((x1, y0, x1, y1), fill=shadow + (255,), width=3)
        draw.line((x0, y0, x1, y0), fill=light + (255,), width=2)
        draw.line((x0, y0, x0, y1), fill=light + (255,), width=2)
        if tile in (16, 17, 22, 23, 24, 25, 26):
            draw.line((x0 + 3, y0 + 15, x1 - 3, y0 + 15), fill=shadow + (180,), width=1)
            draw.line((x0 + 16, y0 + 3, x0 + 16, y0 + 14), fill=light + (120,), width=1)
            draw.line((x0 + 8, y0 + 17, x0 + 8, y1 - 3), fill=light + (120,), width=1)
        if tile == 19:
            draw.line((x0 + 2, y0 + 27, x0 + 16, y0 + 4, x0 + 25, y0 + 19), fill=(198, 251, 255, 255), width=2)
            draw.line((x0 + 8, y0 + 27, x0 + 28, y0 + 4), fill=(178, 239, 255, 255), width=1)
        if tile in (20, 27):
            draw.rectangle((x0 + 8, y0 + 8, x0 + 23, y0 + 23), outline=light + (255,), width=2)
            draw.ellipse((x0 + 14, y0 + 14, x0 + 18, y0 + 18), fill=light + (255,))

    try:
        font = ImageFont.truetype("C:/Windows/Fonts/arialbd.ttf", 24)
    except OSError:
        font = ImageFont.load_default()
    for tile, char in [(32 + i, chr(65 + i)) for i in range(26)] + [(58 + i, str(i)) for i in range(10)]:
        x0, y0, x1, y1 = box(tile)
        bbox = draw.textbbox((0, 0), char, font=font, stroke_width=0)
        tw, th = bbox[2] - bbox[0], bbox[3] - bbox[1]
        x, y = x0 + (32 - tw) / 2 - bbox[0], y0 + (32 - th) / 2 - bbox[1] - 1
        draw.text((x + 1, y + 2), char, font=font, fill=(9, 21, 35, 220))
        draw.text((x, y), char, font=font, fill=(239, 229, 187, 255))
    # Furniture and lighting are painted into the map, never into collision.
    for tile in range(68, 80):
        x0, y0, x1, y1 = box(tile)
        if tile == 68:  # patched crate
            draw.rectangle((x0 + 3, y0 + 6, x1 - 3, y1 - 2), fill=(94, 59, 43, 255), outline=(156, 103, 65, 255), width=2)
            draw.line((x0 + 5, y0 + 8, x1 - 5, y1 - 4), fill=(183, 126, 79, 255), width=2)
            draw.line((x1 - 5, y0 + 8, x0 + 5, y1 - 4), fill=(183, 126, 79, 255), width=2)
        elif tile == 69:  # iron lantern
            draw.line((x0 + 16, y0, x0 + 16, y0 + 8), fill=(132, 126, 104, 255), width=2)
            draw.rectangle((x0 + 9, y0 + 8, x0 + 23, y0 + 27), fill=(80, 81, 88, 255), outline=(214, 175, 87, 255), width=2)
            draw.ellipse((x0 + 12, y0 + 12, x0 + 20, y0 + 23), fill=(255, 216, 111, 255))
        elif tile == 70:  # stained window
            draw.rounded_rectangle((x0 + 3, y0 + 1, x1 - 3, y1), radius=9, fill=(36, 91, 131, 255), outline=(174, 183, 170, 255), width=3)
            draw.line((x0 + 16, y0 + 5, x0 + 16, y1 - 3), fill=(212, 222, 198, 255), width=2)
            draw.line((x0 + 5, y0 + 17, x1 - 5, y0 + 17), fill=(212, 222, 198, 255), width=2)
        elif tile == 71:  # potted greenery
            draw.rectangle((x0 + 9, y0 + 19, x1 - 9, y1), fill=(164, 97, 68, 255), outline=(214, 152, 94, 255), width=2)
            for dx, dy in ((-8, 14), (0, 8), (7, 14), (-4, 5), (5, 4)):
                draw.ellipse((x0 + 13 + dx, y0 + dy, x0 + 22 + dx, y0 + dy + 10), fill=(75, 156, 101, 255))
        elif tile == 72:  # bookcase
            draw.rectangle((x0 + 3, y0 + 2, x1 - 3, y1), fill=(89, 60, 53, 255), outline=(169, 116, 77, 255), width=3)
            for dx, color in ((6, (190, 97, 86)), (11, (92, 147, 180)), (16, (181, 155, 83)), (21, (112, 147, 114))):
                draw.rectangle((x0 + dx, y0 + 6, x0 + dx + 3, y0 + 24), fill=color + (255,))
        elif tile == 73:  # chandelier
            draw.line((x0 + 16, y0, x0 + 16, y0 + 15), fill=(243, 213, 117, 255), width=2)
            draw.arc((x0 + 3, y0 + 10, x1 - 3, y0 + 28), 0, 180, fill=(243, 213, 117, 255), width=3)
            for dx in (6, 16, 26):
                draw.ellipse((x0 + dx - 3, y0 + 18, x0 + dx + 3, y0 + 25), fill=(255, 239, 156, 255))
        elif tile == 74:  # ceremonial seat
            draw.rounded_rectangle((x0 + 5, y0 + 2, x1 - 5, y1 - 1), radius=5, fill=(111, 68, 132, 255), outline=(242, 202, 96, 255), width=3)
            draw.rectangle((x0 + 3, y0 + 20, x1 - 3, y0 + 28), fill=(165, 107, 156, 255), outline=(242, 202, 96, 255), width=2)
        elif tile == 75:  # carved arch
            draw.arc((x0 + 2, y0 + 1, x1 - 2, y1 + 17), 180, 360, fill=(219, 222, 211, 255), width=4)
            draw.line((x0 + 3, y0 + 15, x0 + 3, y1), fill=(183, 192, 195, 255), width=4)
            draw.line((x1 - 3, y0 + 15, x1 - 3, y1), fill=(183, 192, 195, 255), width=4)
        elif tile == 76:  # runner
            draw.rectangle((x0, y0 + 5, x1, y1 - 4), fill=(100, 42, 82, 255), outline=(211, 164, 78, 255), width=3)
            draw.line((x0 + 4, y0 + 16, x1 - 4, y0 + 16), fill=(174, 103, 125, 255), width=2)
        elif tile == 77:  # hanging royal banner
            draw.line((x0 + 16, y0, x0 + 16, y0 + 7), fill=(223, 198, 111, 255), width=2)
            draw.polygon(((x0 + 6, y0 + 7), (x1 - 6, y0 + 7), (x1 - 6, y1 - 1), (x0 + 16, y1 - 8), (x0 + 6, y1 - 1)), fill=(91, 66, 147, 255), outline=(228, 190, 88, 255))
        elif tile == 78:  # standing brazier
            draw.line((x0 + 16, y0 + 17, x0 + 16, y1), fill=(175, 144, 99, 255), width=3)
            draw.ellipse((x0 + 5, y0 + 14, x1 - 5, y0 + 23), fill=(133, 111, 92, 255), outline=(243, 197, 98, 255), width=2)
            draw.polygon(((x0 + 10, y0 + 15), (x0 + 15, y0 + 1), (x0 + 20, y0 + 10), (x0 + 23, y0 + 2), (x0 + 25, y0 + 17)), fill=(255, 173, 70, 255))
        else:  # patterned tapestry
            draw.rectangle((x0 + 1, y0 + 2, x1 - 1, y1 - 2), fill=(65, 116, 130, 255), outline=(240, 207, 107, 255), width=2)
            draw.polygon(((x0 + 16, y0 + 6), (x0 + 26, y0 + 16), (x0 + 16, y0 + 26), (x0 + 6, y0 + 16)), fill=(169, 117, 151, 255), outline=(249, 222, 130, 255))
    for tile, shape in [(80, "coin"), (81, "crown"), (82, "heart")]:
        x0, y0, x1, y1 = box(tile)
        if shape == "coin":
            draw.ellipse((x0 + 5, y0 + 4, x1 - 5, y1 - 4), fill=(239, 178, 53, 255), outline=(255, 233, 129, 255), width=2)
            draw.ellipse((x0 + 11, y0 + 10, x1 - 11, y1 - 10), fill=(255, 222, 117, 255))
        elif shape == "crown":
            draw.polygon([(x0 + 4, y0 + 11), (x0 + 10, y0 + 17), (x0 + 16, y0 + 5), (x0 + 22, y0 + 17), (x0 + 28, y0 + 11), (x0 + 25, y0 + 25), (x0 + 7, y0 + 25)], fill=(251, 211, 92, 255))
            draw.line((x0 + 7, y0 + 26, x0 + 25, y0 + 26), fill=(255, 238, 165, 255), width=2)
        else:
            draw.polygon([(x0 + 16, y0 + 27), (x0 + 3, y0 + 14), (x0 + 7, y0 + 7), (x0 + 16, y0 + 12), (x0 + 25, y0 + 7), (x0 + 29, y0 + 14)], fill=(239, 90, 124, 255))
    ATLAS.parent.mkdir(parents=True, exist_ok=True)
    atlas.save(ATLAS)
    return atlas.tobytes()


def build_file(atlas_rgba: bytes) -> None:
    data = []

    def add_data(blob: bytes) -> int:
        data.append(blob)
        return len(data) - 1

    items = []
    items.append((0, 0, struct.pack("<i", 1)))
    author = add_data(b"Yair\0")
    version = add_data(b"TeeTycoonV1\0")
    credits = add_data(b"Original TeeTycoon map by Yair. Built for the TeeTycoon DDNet mod.\0")
    license_name = add_data(b"TeeTycoon original map artwork\0")
    items.append((1, 0, struct.pack("<6i", 1, author, version, credits, license_name, -1)))
    image_name = add_data(b"teetycoon_v1_atlas\0")
    image_data = add_data(atlas_rgba)
    items.append((2, 0, struct.pack("<6i", 1, 512, 512, 0, image_name, image_data)))

    layer_id = 0

    def layer(name: str, tiles: bytearray, flag: int, image: int = -1, special: bytearray | None = None) -> None:
        nonlocal layer_id
        data_id = add_data(bytes(tiles) if special is None else bytes(WIDTH * HEIGHT * 4))
        named = list(encode_name(name))
        fields = [0, 2, 0, 3, WIDTH, HEIGHT, flag, 255, 255, 255, 255, -1, 0, image, data_id, *named, -1, -1, -1, -1, -1]
        if special is not None:
            special_data_id = add_data(bytes(special))
            if flag == 2:
                fields[18] = special_data_id
            elif flag == 8:
                fields[20] = special_data_id
        items.append((5, layer_id, struct.pack("<23i", *fields)))
        layer_id += 1

    # Both groups have 100% parallax so one tile coordinate maps to one world
    # tile. The game group begins with the architecture layer.
    group0 = [3, 0, 0, 100, 100, 0, 1, 0, 0, 0, 0, 0, *encode_name("Sky")]
    items.append((4, 0, struct.pack("<15i", *group0)))
    layer("Sky", background, 0, 0)
    group1 = [3, 0, 0, 100, 100, 1, 5, 0, 0, 0, 0, 0, *encode_name("TeeTycoon")]
    items.append((4, 1, struct.pack("<15i", *group1)))
    layer("Stone", arch, 0, 0)
    layer("Game", game, 1)
    layer("Front", front, 8, special=front)
    layer("Tele", tele, 2, special=tele)
    layer("Ornament", decor, 0, 0)
    OUT.parent.mkdir(parents=True, exist_ok=True)
    write_map(OUT, items, data)


def physics_preview() -> None:
    colors = {AIR: (14, 25, 42), SOLID: (142, 158, 176), NOHOOK: (58, 65, 78), DEATH: (211, 70, 88), FREEZE: (96, 209, 237), MONEY: (255, 209, 89), VIP: (248, 151, 41), SURVIVAL: (49, 76, 96), HOME: (136, 94, 173), RACE_FINISH: (129, 223, 122)}
    pixels = bytearray(WIDTH * HEIGHT * 4)
    for i in range(WIDTH * HEIGHT):
        tile = game[i * 4]
        r, g, b = colors.get(tile, (225, 231, 180) if tile >= 192 else (30, 45, 64))
        if tele[i * 2 + 1] == TELE_OUT:
            r, g, b = (231, 74, 202)
        elif tele[i * 2 + 1] == TELE_IN:
            r, g, b = (69, 231, 222)
        pixels[i * 4:i * 4 + 4] = bytes((r, g, b, 255))
    PREVIEW.parent.mkdir(parents=True, exist_ok=True)
    write_png(PREVIEW, WIDTH, HEIGHT, pixels)


def art_preview() -> None:
    """Render the embedded artwork at four pixels per tile for design review."""
    atlas = Image.open(ATLAS).convert("RGBA")
    tiles = []
    for index in range(256):
        x, y = (index % 16) * 32, (index // 16) * 32
        tiles.append(atlas.crop((x, y, x + 32, y + 32)).resize((4, 4), Image.Resampling.LANCZOS))
    canvas = Image.new("RGBA", (WIDTH * 4, HEIGHT * 4), (14, 25, 42, 255))
    for layer_bytes in (background, arch, decor):
        for y in range(HEIGHT):
            for x in range(WIDTH):
                index = layer_bytes[(y * WIDTH + x) * 4]
                if index:
                    tile = tiles[index]
                    canvas.alpha_composite(tile, (x * 4, y * 4))
    canvas.convert("RGB").save(ART_PREVIEW)


if __name__ == "__main__":
    make_geometry()
    build_file(make_atlas())
    physics_preview()
    art_preview()
    print(f"wrote {OUT} ({WIDTH}x{HEIGHT})")
