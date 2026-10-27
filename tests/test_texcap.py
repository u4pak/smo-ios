"""Texture caps for the IPA (dm/tools/texcap.py), on synthetic BNTX files only."""
from pathlib import Path
import struct
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'dm/tools'))
import texcap

BC1, R8G8B8A8, UNKNOWN = 0x1a01, 0x0b01, 0x7701


def bntx(textures):
    """A BNTX inside a larger file: header, NX container, one BRTI per texture, names, mip pointer tables, then the data at 0x1000 (level offsets from texcap.levels).
    textures: (name, format, width, height, mips, block height log2, layers)."""
    out = bytearray(b'PAD!' * 16)   # the BNTX does not start the file, as in a BFRES
    b = len(out)
    out += bytearray(0x1000)
    out[b:b + 8] = texcap.BNTX
    out[b + 0x20:b + 0x24] = b'NX  '
    struct.pack_into('<iQ', out, b + 0x24, len(textures), 0x40)
    data, where = 0x1000, []
    for i, (name, fmt, w, h, mips, tile, layers) in enumerate(textures):
        t = 0x100 + 0x100 * i
        struct.pack_into('<Q', out, b + 0x40 + 8 * i, t)
        out[b + t:b + t + 4] = b'BRTI'
        struct.pack_into('<BBHHHI', out, b + t + 0x10, 1, 2, 0, 0, mips, 1)
        struct.pack_into('<IIiiiiii', out, b + t + 0x1c, fmt, 0x20, w, h, 1, layers, tile, 0x10007)
        offsets, size = texcap.levels(w, h, mips, tile, fmt) if fmt != UNKNOWN else ([0, 0x400], 0x800)
        struct.pack_into('<IIIB', out, b + t + 0x50, size, 0x200, 0, 1)
        name_at, mips_at = 0x900 + 0x40 * i, 0xc00 + 0x80 * i
        struct.pack_into('<H', out, b + name_at, len(name)); out[b + name_at + 2:b + name_at + 2 + len(name)] = name.encode()
        struct.pack_into('<QQQ', out, b + t + 0x60, name_at, 0, mips_at)
        struct.pack_into(f'<{mips}Q', out, b + mips_at, *[data + o for o in offsets])
        out += bytes(data + size - (len(out) - b))
        for k, o in enumerate(offsets):   # each level filled with its own number, so a moved level can be recognised
            end = offsets[k + 1] if k + 1 < len(offsets) else size
            out[b + data + o:b + data + end] = bytes([0x10 + k]) * (end - o)
        where.append((t, mips_at, data))
        data = -(-(data + size) // 0x200) * 0x200
    return out, b, where


class TexcapTests(unittest.TestCase):
    def test_layout_matches_the_game(self):
        # Offsets read from the game's KoopaBody_alb (1024 x 1024 BC1, 11 levels, block height 16 GOBs) and KaronMarioEyePupil_alb (256 x 256 BC1, 9 levels, 8 GOBs)
        offsets, size = texcap.levels(1024, 1024, 11, 4, BC1)
        self.assertEqual(offsets[:4], [0, 0x80000, 0xa0000, 0xa8000])
        offsets, size = texcap.levels(256, 256, 9, 3, BC1)
        self.assertEqual(([o + 0xd000 for o in offsets], size), ([0xd000, 0x15000, 0x17000, 0x17800, 0x17a00, 0x17c00, 0x17e00, 0x18000, 0x18200], 0xb400))

    def test_cap_moves_levels_and_rewrites_the_header(self):
        data, b, where = bntx([('Ground_alb', BC1, 1024, 512, 11, 4, 1), ('Small_alb', BC1, 256, 256, 9, 3, 1)])
        before = bytes(data)
        self.assertEqual(texcap.cap_file(data, 256), [('Ground_alb', 1024, 512, 256, 128)])
        self.assertEqual(len(data), len(before))
        t, mips_at, start = where[0]
        mips, = struct.unpack_from('<H', data, b + t + 0x16)
        w, h = struct.unpack_from('<ii', data, b + t + 0x24)
        size, = struct.unpack_from('<I', data, b + t + 0x50)
        self.assertEqual((mips, w, h, data[b + t + 0x34]), (9, 256, 128, 2))   # 128 rows of BC1 = 32 block rows = 4 GOBs: block height 4 GOBs, log2 2
        offsets, total = texcap.levels(256, 128, 9, 2, BC1)
        self.assertEqual(size, total)
        pointers = struct.unpack_from('<11Q', data, b + mips_at)
        self.assertEqual(pointers, tuple(start + o for o in offsets) + (start, start))
        self.assertEqual(data[b + start], 0x12)   # old level 2 is now level 0
        self.assertEqual(data[b + start + offsets[1]], 0x13)
        old_offsets, old_size = texcap.levels(1024, 512, 11, 4, BC1)
        self.assertEqual(bytes(data[b + start + size:b + start + old_size]), bytes(old_size - size))   # the freed bytes are zeros
        self.assertEqual(data[b + where[1][2]:], before[b + where[1][2]:])   # the small texture's data is untouched
        self.assertEqual(data[:b], before[:b])

    def test_textures_it_does_not_understand_stay(self):
        data, b, where = bntx([('Array_alb', BC1, 1024, 1024, 11, 4, 2), ('Odd_alb', UNKNOWN, 1024, 1024, 2, 4, 1), ('NoMips', BC1, 1024, 1024, 1, 4, 1)])
        before = bytes(data)
        self.assertEqual(texcap.cap_file(data, 512), [])
        self.assertEqual(bytes(data), before)
        data, b, where = bntx([('Shifted', BC1, 1024, 1024, 11, 4, 1)])
        struct.pack_into('<Q', data, b + where[0][1] + 8, where[0][2] + 0x80200)   # level 1 not where the layout puts it
        before = bytes(data)
        self.assertEqual(texcap.cap_file(data, 512), [])
        self.assertEqual(bytes(data), before)

    def test_per_texture_limit(self):
        data, b, where = bntx([('SkyBox_color', BC1, 2048, 512, 12, 4, 1), ('Rock_alb', R8G8B8A8, 1024, 1024, 11, 4, 1)])
        changed = texcap.cap_file(data, lambda name: 1024 if name.startswith('Sky') else 512)
        self.assertEqual(changed, [('SkyBox_color', 2048, 512, 1024, 256), ('Rock_alb', 1024, 1024, 512, 512)])


if __name__ == '__main__':
    unittest.main()
