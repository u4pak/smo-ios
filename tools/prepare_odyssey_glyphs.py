"""Touch pictures for the moped guide: a pedal and an up arrow in the device font.

The message pad pictures ({9:...} DeviceFont tags) draw glyphs of
DeviceFont80.bffnt in every language's LayoutData/FontData.szs. Two of its
glyphs are never shown by this port: ButtonSL (U+E0E8) and ButtonSR (U+E0E9),
used only by the PadStyle labels for a single sideways Joy-Con, a controller
style the host never reports. Their cells in the font's BC4 sheet (an embedded
BNTX texture, Tegra block linear, stored bottom-up) are redrawn in place as a
pedal and an up arrow in a filled circle; the glyph widths, the character map
and every other byte stay as they were. The archive is re-encoded from the
original Yaz0 stream, which keeps every token whose output is unchanged, so the
prepared archive stays about the size of the original.

The message side (prepare_odyssey_balloon.py) points the moped tutorial's
handheld variants at these glyphs; the host draws the handheld variants while
touch is in use (hle_odyssey_ui.c).
"""
from __future__ import annotations

from pathlib import Path
import struct

import prepare_odyssey_aspect as common
import prepare_odyssey_phone as phone

FONT_ARCHIVE = 'LayoutData/FontData.szs'
FONT_MEMBER = 'DeviceFont80.bffnt'
PEDAL, UP = 0xE0E8, 0xE0E9      # ButtonSL, ButtonSR: unused glyphs, redrawn
WIDTHS = (0, 67, 68)            # their (left, glyph, advance) widths, kept
LANGUAGES = 14


# ---- Font structure ----
def _sections(font: bytes) -> dict[bytes, list[int]]:
    if font[:4] != b'FFNT' or font[4:6] != b'\xff\xfe':
        raise ValueError('expected a little-endian FFNT')
    found, pos = {}, struct.unpack_from('<H', font, 6)[0]
    while pos + 8 <= len(font):
        tag, size = struct.unpack_from('<4sI', font, pos)
        found.setdefault(tag, []).append(pos)
        if size < 8:
            break
        pos += size
    return found


def _glyph_index(font: bytes, sections, code: int) -> int:
    for cmap in sections.get(b'CMAP', []):
        lo, hi, method = struct.unpack_from('<IIH', font, cmap + 8)
        if not lo <= code <= hi:
            continue
        if method == 0:
            return struct.unpack_from('<H', font, cmap + 24)[0] + code - lo
        if method == 1:
            return struct.unpack_from('<H', font, cmap + 24 + 2 * (code - lo))[0]
        count = struct.unpack_from('<H', font, cmap + 24)[0]
        for i in range(count):
            entry, index = struct.unpack_from('<IH', font, cmap + 28 + 8 * i)
            if entry == code:
                return index
    raise ValueError(f'{FONT_MEMBER}: no glyph for U+{code:04X}')


def _widths(font: bytes, sections, index: int) -> tuple[int, int, int]:
    for cwdh in sections.get(b'CWDH', []):
        first, last = struct.unpack_from('<HH', font, cwdh + 8)
        if first <= index <= last:
            return struct.unpack_from('<bBB', font, cwdh + 16 + 3 * (index - first))
    raise ValueError(f'{FONT_MEMBER}: no widths for glyph {index}')


# ---- Tegra block linear (GOB 64 bytes x 8 rows; blocks of 2^bh GOBs high) ----
def _gob(x: int, y: int) -> int:
    return (x % 64 // 32) * 256 + (y % 8 // 2) * 64 + (x % 32 // 16) * 32 + (y % 2) * 16 + x % 16


def _block_offset(x: int, y: int, row_bytes: int, block_height: int) -> int:
    gobs_x = (row_bytes + 63) // 64
    block = 512 * block_height
    return (y // (8 * block_height)) * gobs_x * block + (x // 64) * block + (y % (8 * block_height)) // 8 * 512 + _gob(x, y)


# ---- BC4 (one channel, 8 bytes per 4x4 block) ----
def _bc4_palette(a0: int, a1: int) -> list[int]:
    if a0 > a1:
        return [a0, a1] + [((7 - i) * a0 + i * a1) // 7 for i in range(1, 7)]
    return [a0, a1] + [((5 - i) * a0 + i * a1) // 5 for i in range(1, 5)] + [0, 255]


def bc4_decode(block: bytes) -> list[int]:
    palette, bits = _bc4_palette(block[0], block[1]), int.from_bytes(block[2:8], 'little')
    return [palette[(bits >> (3 * i)) & 7] for i in range(16)]


def bc4_encode(values: list[int]) -> bytes:
    lo, hi = min(values), max(values)
    if lo == hi:
        return bytes([hi, lo]) + bytes(6)
    palette = _bc4_palette(hi, lo)
    bits = 0
    for i, v in enumerate(values):
        bits |= min(range(8), key=lambda k: abs(palette[k] - v)) << (3 * i)
    return bytes([hi, lo]) + bits.to_bytes(6, 'little')


# ---- The pictures (cell coordinates, y down, coverage 0..255) ----
def _coverage(inside, width: int, height: int, samples: int = 4) -> list[list[int]]:
    rows = []
    for y in range(height):
        row = []
        for x in range(width):
            hits = sum(inside(x + (i + .5) / samples, y + (j + .5) / samples) for i in range(samples) for j in range(samples))
            row.append(round(255 * hits / samples ** 2))
        rows.append(row)
    return rows


def _rounded(px: float, py: float, x0: float, y0: float, x1: float, y1: float, r: float) -> bool:
    cx, cy = min(max(px, x0 + r), x1 - r), min(max(py, y0 + r), y1 - r)
    return x0 <= px <= x1 and y0 <= py <= y1 and (px - cx) ** 2 + (py - cy) ** 2 <= r * r


def _in_polygon(px: float, py: float, points) -> bool:
    inside = False
    for (x0, y0), (x1, y1) in zip(points, points[1:] + points[:1]):
        if (y0 > py) != (y1 > py) and px < x0 + (py - y0) * (x1 - x0) / (y1 - y0):
            inside = not inside
    return inside


# Button glyphs fill x 1..66, y 15..80 of their 80x84 cells; these match.
def pedal(px: float, py: float) -> bool:
    """A gas pedal leaning back: a pad with four grooves on a hinge bar."""
    t = (py - 15) / 58                          # 0 at the top of the pad, 1 at its foot
    half = 15 + 7 * t                           # wider toward the foot
    centre = 36 - 4 * t                         # leaning back
    pad = 15 <= py <= 73 and _rounded(px, py, centre - half, 15, centre + half, 73, 9) if 0 <= t <= 1 else False
    grooves = any(abs(py - y) <= 2.2 for y in (27, 38, 49, 60)) and abs(px - centre) <= half - 7
    hinge = _rounded(px, py, 14, 74, 54, 81, 3.5)
    return (pad and not grooves) or hinge


def up_arrow(px: float, py: float) -> bool:
    """A filled circle (the button glyphs' size) with an up arrow cut out."""
    circle = (px - 33.5) ** 2 + (py - 47.5) ** 2 <= 32.5 ** 2
    head = _in_polygon(px, py, [(33.5, 24), (53, 47), (41, 47), (41, 72), (26, 72), (26, 47), (14, 47)])
    return circle and not head


PICTURES = {PEDAL: pedal, UP: up_arrow}


def patch_font(font: bytes) -> bytes:
    """Redraw the two cells in place; the result has the same size."""
    sections = _sections(font)
    if len(sections.get(b'TGLP', [])) != 1:
        raise ValueError(f'{FONT_MEMBER}: expected one glyph sheet section')
    tglp = sections[b'TGLP'][0]
    cell_w, cell_h, sheets = struct.unpack_from('<BBB', font, tglp + 8)
    sheet_size = struct.unpack_from('<I', font, tglp + 12)[0]
    _, fmt, cols, rows, width, height, data = struct.unpack_from('<HHHHHHI', font, tglp + 16)
    if (sheets, fmt, width, height) != (1, 12, 512, 1024) or cols * (cell_w + 1) > width or rows * (cell_h + 1) > height:
        raise ValueError(f'{FONT_MEMBER}: expected one 512x1024 BC4 sheet')
    brti, brtd = font.find(b'BRTI', data, data + sheet_size), font.find(b'BRTD', data, data + sheet_size)
    if brti < 0 or brtd < 0:
        raise ValueError(f'{FONT_MEMBER}: expected an embedded BNTX texture')
    tile, _, mips, _, tex_fmt, _, tex_w, tex_h, depth, array, block_log2 = struct.unpack_from('<HHHIIIIIIII', font, brti + 0x12)
    pixels, size = brtd + 0x10, struct.unpack_from('<I', font, brtd + 8)[0] - 0x10
    row_bytes = width // 4 * 8
    if (tile, mips, tex_fmt, tex_w, tex_h, depth, array) != (0, 1, 0x1d01, width, height, 1, 1) or size != row_bytes * height // 4 \
            or pixels + size > tglp + struct.unpack_from('<I', font, tglp + 4)[0]:
        raise ValueError(f'{FONT_MEMBER}: unexpected texture layout')
    block_height = 1 << block_log2
    out = bytearray(font)
    # Both pictures in sheet-image pixels (top down) first: their cells share blocks.
    target, blocks = {}, set()
    for code, picture in PICTURES.items():
        index = _glyph_index(font, sections, code)
        if _widths(font, sections, index) != WIDTHS or index >= cols * rows:
            raise ValueError(f'{FONT_MEMBER}: U+{code:04X} is not the expected glyph')
        art = _coverage(picture, cell_w, cell_h)
        left, top = (index % cols) * (cell_w + 1) + 1, (index // cols) * (cell_h + 1) + 1
        for y in range(cell_h):
            for x in range(cell_w):
                target[left + x, top + y] = art[y][x]
        # The texture is stored bottom-up: image row y is texture row height - 1 - y.
        blocks |= {(bx, by) for bx in range(left // 4, (left + cell_w - 1) // 4 + 1)
                   for by in range((height - top - cell_h) // 4, (height - 1 - top) // 4 + 1)}
    for bx, by in sorted(blocks):
        at = pixels + _block_offset(bx * 8, by, row_bytes, block_height)
        was = bc4_decode(out[at:at + 8])
        values, free = list(was), set()
        for i in range(16):
            x, y = bx * 4 + i % 4, height - 1 - (by * 4 + i // 4)
            if (x, y) in target:
                values[i] = target[x, y]
                free.add(i)
            elif x % (cell_w + 1) == 0 or y % (cell_h + 1) == 0:   # the grid between cells: part of no glyph, cleared
                values[i] = 0
                free.add(i)
        block = bc4_encode(values)
        now = bc4_decode(block)
        if any(now[i] != was[i] for i in range(16) if i not in free):
            raise ValueError(f'{FONT_MEMBER}: redrawing the touch pictures would change a neighbouring glyph')
        if now != was:
            out[at:at + 8] = block
    return bytes(out)


# ---- Yaz0, reusing the original stream ----
def _tokens(stream: bytes):
    """Yield (output position, length, raw token bytes or None for a literal) from a Yaz0 stream."""
    size, pos, out = struct.unpack_from('>I', stream, 4)[0], 16, 0
    while out < size:
        control = stream[pos]
        pos += 1
        for bit in range(7, -1, -1):
            if out >= size:
                return
            if control & (1 << bit):
                yield out, 1, None
                pos += 1
                out += 1
            else:
                a = stream[pos]
                length = (a >> 4) + 2 if a >> 4 else stream[pos + 2] + 0x12
                raw = stream[pos:pos + (2 if a >> 4 else 3)]
                pos += len(raw)
                yield out, length, raw
                out += length


def yaz0_reuse(original: bytes, data: bytes) -> bytes:
    """Encode data as Yaz0, keeping each back-reference of the original stream that still produces the same bytes."""
    if original[:4] != b'Yaz0' or struct.unpack_from('>I', original, 4)[0] != len(data):
        raise ValueError('the source Yaz0 stream does not match the data size')
    out = bytearray(original[:16])
    control_at, count = len(out), 0

    def emit(literal: bool, raw: bytes):
        nonlocal control_at, count
        if count == 0:
            control_at = len(out)
            out.append(0)
        if literal:
            out[control_at] |= 0x80 >> count
        out.extend(raw)
        count = (count + 1) % 8

    def reference(distance: int, length: int) -> bytes:
        d = distance - 1
        return bytes([(length - 2) << 4 | d >> 8, d & 0xff]) if length < 0x12 else bytes([d >> 8, d & 0xff, length - 0x12])

    for at, length, raw in _tokens(original):
        if raw is None:
            emit(True, data[at:at + 1])
            continue
        distance = ((raw[0] & 15) << 8 | raw[1]) + 1
        # The output so far is data, so the copy's byte at q is data[q - distance].
        if data[at - distance:at - distance + length] == data[at:at + length] or \
                all(data[q - distance] == data[q] for q in range(at, at + length)):
            emit(False, raw)
            continue
        # Changed bytes: keep runs that still match as shorter references, the rest as literals.
        q, end = at, at + length
        while q < end:
            run = q
            while run < end and data[run - distance] == data[run]:
                run += 1
            if run - q >= 3:
                emit(False, reference(distance, run - q))
                q = run
            else:
                emit(True, data[q:q + 1])
                q += 1
    return bytes(out)


def prepare(romfs: Path, overlay: Path) -> list[dict]:
    """Write every language's font archive with the touch pictures; return records."""
    archives = sorted(romfs.glob('LocalizedData/*/' + FONT_ARCHIVE))
    if len(archives) != LANGUAGES:
        raise ValueError(f'expected {LANGUAGES} font archives, found {len(archives)}')
    records = []
    for path in archives:
        before = path.read_bytes()
        if before[:4] != b'Yaz0':
            raise ValueError(f'{path.name}: expected Yaz0')
        sarc = common.yaz0_decode(before)
        found = [(lo, hi) for name, lo, hi in phone.members(sarc, path.name) if name == FONT_MEMBER]
        if len(found) != 1:
            raise ValueError(f'{path.name}: expected one {FONT_MEMBER}')
        lo, hi = found[0]
        patched = sarc[:lo] + patch_font(sarc[lo:hi]) + sarc[hi:]
        after = yaz0_reuse(before, patched)
        if common.yaz0_decode(after) != patched:
            raise ValueError(f'{path.name}: Yaz0 round-trip failed')
        relative = path.relative_to(romfs)
        target = overlay / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(after)
        records.append({'relative_path': str(relative), 'member': FONT_MEMBER,
                        'source_sha256': common.sha(before), 'output_sha256': common.sha(after)})
    return records
