"""Touch pictures in the device font, on synthetic files only."""
from pathlib import Path
import struct
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
sys.path.insert(0, str(ROOT / 'tests'))
import prepare_odyssey_aspect as common
import prepare_odyssey_glyphs as glyphs
import prepare_odyssey_phone as phone
from test_balloon_world import sarc

CELL_W, CELL_H, COLS, ROWS, W, H = 80, 84, 6, 12, 512, 1024
CODES = [0xE0E0, 0xE0E1, 0xE0E8, 0xE0E9]        # A, B, SL, SR → glyphs 0..3


def font() -> bytes:
    """A minimal FFNT: TGLP with an embedded BNTX-like texture (BRTI, BRTD), CWDH and a scan CMAP."""
    texture = bytearray(W // 4 * H // 4 * 8)   # empty cells, as the sheet's unused area
    brti = bytearray(b'BRTI' + bytes(0x3c))
    struct.pack_into('<HHHIIIIIIII', brti, 0x12, 0, 0, 1, 0, 0x1d01, 0, W, H, 1, 1, 4)
    sheet = bytearray(0x200) + brti
    sheet += bytes(-len(sheet) % 0x100)
    sheet += b'BRTD' + struct.pack('<II', 0, len(texture) + 0x10) + bytes(4) + texture
    out = bytearray(b'FFNT\xff\xfe' + struct.pack('<HIIHH', 0x14, 0x04010000, 0, 3, 0))
    tglp = len(out)
    data = 0x1000
    header = bytearray(b'TGLP' + bytes(4) + struct.pack('<BBBBI', CELL_W, CELL_H, 1, 80, len(sheet))
                       + struct.pack('<HHHHHHI', 74, 12, COLS, ROWS, W, H, data))
    out += header + bytes(data - tglp - len(header)) + sheet
    struct.pack_into('<I', out, tglp + 4, len(out) - tglp)
    widths = [(1, 65, 68), (1, 65, 68), glyphs.WIDTHS, glyphs.WIDTHS]
    body = b''.join(struct.pack('<bBB', *w) for w in widths)
    out += b'CWDH' + struct.pack('<IHHI', 16 + len(body) + 1, 0, 3, 0) + body + bytes(1)
    entries = b''.join(struct.pack('<IHH', c, i, 0) for i, c in enumerate(CODES))
    out += b'CMAP' + struct.pack('<IIIHHI', 28 + len(entries), 0, 0xffff, 2, 0, 0) + struct.pack('<HH', len(CODES), 0) + entries
    struct.pack_into('<I', out, 0x0c, len(out))
    return bytes(out)


def yaz0(data: bytes) -> bytes:
    """A small greedy Yaz0 encoder (back-references within 4 KiB), so the reuse path sees real references."""
    out, pos, last = bytearray(b'Yaz0' + struct.pack('>I', len(data)) + bytes(8)), 0, {}
    while pos < len(data):
        control_at = len(out); out.append(0)
        for bit in range(8):
            if pos >= len(data):
                break
            key, best, best_at = data[pos:pos + 3], 0, 0
            for start in last.get(key, [])[-8:]:
                if pos - start > 0x1000:
                    continue
                n = 0
                while n < 0x111 and pos + n < len(data) and data[start + n] == data[pos + n]:
                    n += 1
                if n > best:
                    best, best_at = n, start
            if best >= 3:
                d = pos - best_at - 1
                out += bytes([((best - 2) << 4 | d >> 8) if best < 0x12 else d >> 8, d & 0xff]) + (bytes([best - 0x12]) if best >= 0x12 else b'')
                for p in range(pos, pos + best):
                    last.setdefault(data[p:p + 3], []).append(p)
                pos += best
            else:
                out[control_at] |= 0x80 >> bit
                out.append(data[pos])
                last.setdefault(key, []).append(pos)
                pos += 1
    return bytes(out)


class TouchGlyphTests(unittest.TestCase):
    def test_bc4_round_trip(self):
        levels = glyphs._bc4_palette(255, 0)
        for values in ([0] * 16, [255] * 16, [0, 255] * 8, [levels[i % 8] for i in range(16)]):
            self.assertEqual(glyphs.bc4_decode(glyphs.bc4_encode(values)), values)
        smooth = [i * 3 for i in range(16)]
        self.assertTrue(all(abs(a - b) <= 4 for a, b in zip(glyphs.bc4_decode(glyphs.bc4_encode(smooth)), smooth)))

    def test_pictures_fill_the_button_area(self):
        for picture in glyphs.PICTURES.values():
            art = glyphs._coverage(picture, CELL_W, CELL_H, samples=1)
            ys = [y for y, row in enumerate(art) if any(row)]
            xs = [x for row in art for x, v in enumerate(row) if v]
            self.assertTrue(12 <= min(ys) and max(ys) <= 82 and min(xs) >= 1 and max(xs) <= glyphs.WIDTHS[1])
        self.assertFalse(glyphs.up_arrow(33.5, 40) or not glyphs.up_arrow(33.5, 18))   # arrow cut out, circle around
        self.assertTrue(glyphs.pedal(36, 20) and not glyphs.pedal(36, 27) and glyphs.pedal(30, 78))

    def test_patch_redraws_only_the_two_cells(self):
        before = font()
        after = glyphs.patch_font(before)
        self.assertEqual(len(after), len(before))
        changed = [i for i, (a, b) in enumerate(zip(before, after)) if a != b]
        self.assertTrue(changed)
        pixels = before.find(b'BRTD') + 0x10
        row_bytes = W // 4 * 8
        block_at = {glyphs._block_offset(bx * 8, by, row_bytes, 16): (bx, by) for by in range(H // 4) for bx in range(W // 4)}
        cells = set()
        for i in changed:
            self.assertGreaterEqual(i, pixels)   # texture only: widths, map and headers stay
            at = (i - pixels) // 8 * 8
            bx, by = block_at[at]
            was, now = glyphs.bc4_decode(before[pixels + at:pixels + at + 8]), glyphs.bc4_decode(after[pixels + at:pixels + at + 8])
            for k in range(16):
                if was[k] != now[k]:   # image pixel (the texture is bottom-up)
                    x, y = bx * 4 + k % 4, H - 1 - (by * 4 + k // 4)
                    cells.add((x // (CELL_W + 1), y // (CELL_H + 1)))
        self.assertEqual(cells, {(2, 0), (3, 0)})   # glyphs 2 and 3: SL, SR
        pixels_at = before.find(b'BRTD') + 0x10 + glyphs._block_offset(40 * 8, H // 4 - 1, row_bytes, 16)
        busy = bytearray(before); busy[pixels_at:pixels_at + 8] = bytes([200, 100]) + sum((i % 2) << (3 * i) for i in range(16)).to_bytes(6, 'little')   # glyph 1 reaches x = 161
        with self.assertRaisesRegex(ValueError, 'neighbouring glyph'):
            glyphs.patch_font(bytes(busy))
        with self.assertRaisesRegex(ValueError, 'not the expected glyph'):
            glyphs.patch_font(before.replace(struct.pack('<bBB', *glyphs.WIDTHS), struct.pack('<bBB', 0, 60, 68), 1))

    def test_yaz0_reuse_keeps_references(self):
        data = (b'abcdefgh' * 600 + bytes(range(256)) * 8) * 3
        original = yaz0(data)
        self.assertEqual(common.yaz0_decode(original), data)
        changed = bytearray(data)
        changed[5000:5010] = b'0123456789'
        reused = glyphs.yaz0_reuse(original, bytes(changed))
        self.assertEqual(common.yaz0_decode(reused), bytes(changed))
        self.assertLess(len(reused), len(original) + 200)   # far from a literal stream
        self.assertEqual(glyphs.yaz0_reuse(original, data), original)

    def test_prepare_writes_every_language(self):
        with tempfile.TemporaryDirectory() as temp:
            romfs, overlay = Path(temp) / 'RomFS', Path(temp) / 'overlay'
            archive = yaz0(sarc([('MessageFont38.bffnt', b'M' * 0x300), (glyphs.FONT_MEMBER, font())], alignment=0x2000))
            for i in range(glyphs.LANGUAGES):
                path = romfs / 'LocalizedData' / f'L{i:02d}' / glyphs.FONT_ARCHIVE
                path.parent.mkdir(parents=True)
                path.write_bytes(archive)
            sources = {p: p.read_bytes() for p in romfs.rglob('*.szs')}
            records = glyphs.prepare(romfs, overlay)
            self.assertEqual(len(records), glyphs.LANGUAGES)
            self.assertEqual({p: p.read_bytes() for p in romfs.rglob('*.szs')}, sources)
            for record in records:
                out = (overlay / record['relative_path']).read_bytes()
                self.assertEqual(common.sha(out), record['output_sha256'])
                members = {name: common.yaz0_decode(out)[lo:hi] for name, lo, hi in phone.members(common.yaz0_decode(out), 'out')}
                self.assertEqual(members['MessageFont38.bffnt'], b'M' * 0x300)
                self.assertEqual(members[glyphs.FONT_MEMBER], glyphs.patch_font(font()))
            next(romfs.rglob('FontData.szs')).unlink()
            with self.assertRaisesRegex(ValueError, 'font archives'):
                glyphs.prepare(romfs, Path(temp) / 'overlay2')


if __name__ == '__main__':
    unittest.main()
