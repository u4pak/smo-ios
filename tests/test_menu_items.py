"""Title/pause menu without Playing in VR and Action Guide; title logo edge."""
import ast
from pathlib import Path
import struct
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
sys.path.insert(0, str(ROOT / 'tests'))
import prepare_odyssey_aspect as common
import prepare_odyssey_phone as phone
from test_phone_layout import layout

ODYSSEY = 'b424be150a8e7d78701cbe7a439d9ebf'
VR, HELP, SAVE, OPTIONS = 6, 3, 4, 5


def cuts():
    """Read patch_nso.py's CUTS literal without running the rewriter."""
    tree = ast.parse((ROOT / 'dm/tools/patch_nso.py').read_text())
    node = next(n for n in tree.body if isinstance(n, ast.Assign) and getattr(n.targets[0], 'id', '') == 'CUTS')
    return {offset: (bytes.fromhex(old), bytes.fromhex(new)) for offset, old, new, _ in ast.literal_eval(node.value)[ODYSSEY]}


def table(raw):
    return list(struct.unpack('<6i', raw))


def with_texcoords(data: bytes, name: str) -> bytes:
    """The last pane (a pic1) gains one full-texture coordinate set, as real pictures carry."""
    order, index = phone.panes(data, 'x')
    pos = index[name][0]
    if pos + 0x54 != len(data):
        raise AssertionError('the picture must be the last pane')
    out = bytearray(data) + bytes(20) + struct.pack('<8f', 0., 0., 1., 0., 0., 1., 1., 1.)
    out[pos + 0x54 + 18] = 1
    struct.pack_into('<I', out, pos + 4, len(out) - pos)
    struct.pack_into('<I', out, 12, len(out))
    return bytes(out)


class MenuItemTests(unittest.TestCase):
    def test_cuts_keep_shape_and_drop_vr_and_help(self):
        found = cuts()
        for offset, (old, new) in found.items():
            self.assertEqual(len(old), len(new), hex(offset))
        title_old, title_new = map(table, found[0xec8270])
        pause_old, pause_new = map(table, found[0xec8288])
        self.assertEqual(title_old[:6], [0, 1, 2, VR, HELP, OPTIONS])
        self.assertEqual(title_new[:4], [0, 1, 2, OPTIONS])
        self.assertEqual(pause_old, [0, 1, VR, HELP, SAVE, OPTIONS])
        self.assertEqual(pause_new[:4], [0, 1, SAVE, OPTIONS])
        # Special mode deactivates logical 2/4/5 through the pause table: same parts.
        self.assertEqual({pause_new[i] for i in (2, 4, 5)}, {pause_old[i] for i in (2, 4, 5)})
        # Both item counts become four (mov w1, #6 -> mov w1, #4).
        for offset in (0x49723c, 0x49609c):
            self.assertEqual(found[offset], (bytes.fromhex('e1071f32'), bytes.fromhex('81008052')))

    def test_menu_files_are_hash_guarded(self):
        records = []
        with self.assertRaisesRegex(ValueError, 'menu layout'):
            common.patch_menu_member('blyt/Menu.bflyt', layout(), 'Menu.bflyt', records)
        with self.assertRaisesRegex(ValueError, 'menu animation'):
            common.patch_menu_member('anim/Menu_SelectTitle.bflan', b'FLAN', 'Menu_SelectTitle.bflan', records)
        self.assertEqual(common.patch_menu_member('blyt/MenuGuide.bflyt', b'unchanged', 'x', records), b'unchanged')
        self.assertEqual(records, [])

    def test_boot_logo_follows_the_menu_logo_edge(self):
        # Menu's ParLogo uses the same CHILD_EDGES rule; BootLoading's must match it.
        self.assertEqual(phone.CHILD_EDGES['BootLoading']['ParLogo'], phone.CHILD_EDGES['Menu']['ParLogo'])
        before = with_texcoords(layout([('ParLogo', b'prt1', -510., 270., 192., 120.), ('PicFade', b'pic1', 0., 0., 1280., 720.),
                                        ('ParBG', b'prt1', 0., 0., 1280., 720.), ('PicStrap', b'pic1', 640., 0., 340., 720.),
                                        ('PicOtherCon', b'pic1', -154., -360., 1280., 328.), ('PicJoyCon', b'pic1', -154., 380., 1280., 556.)]),
                                'PicJoyCon')
        after = phone.patch_layout(before, 'BootLoading.bflyt', [], [])
        order, index = phone.panes(after, 'BootLoading')
        scale = float(phone.SCALE_X)
        expected = -510. - 640 * (1 - scale) / scale
        self.assertAlmostEqual(struct.unpack_from(order + 'f', after, index['ParLogo'][0] + 0x2c)[0], expected, places=3)
        # The tooltip strap ends against the panel's right edge (640 / scale in layout units).
        self.assertAlmostEqual(struct.unpack_from(order + 'f', after, index['PicStrap'][0] + 0x2c)[0] * scale, 640., places=3)
        self.assertEqual(len(before), len(after))

    def test_boot_picture_mode_background_and_wardrobe_fill_the_panel(self):
        scale = float(phone.SCALE_X)
        panes = [('PicOtherCon', b'pic1', -154., -360., 1280., 328.), ('ParLogo', b'prt1', -510., 270., 192., 120.),
                 ('PicFade', b'pic1', 0., 0., 1280., 720.), ('ParBG', b'prt1', 0., 0., 1280., 720.),
                 ('PicStrap', b'pic1', 640., 0., 340., 720.), ('PicJoyCon', b'pic1', -154., 380., 1280., 556.)]
        boot = with_texcoords(layout(panes), 'PicJoyCon')
        order, index = phone.panes(boot, 'BootLoading')
        joy = index['PicJoyCon'][0]
        after = phone.patch_layout(boot, 'BootLoading.bflyt', [], [])
        width = struct.unpack_from('<f', after, joy + 0x4c)[0]
        coords = struct.unpack_from('<8f', after, joy + 0x54 + 20)
        self.assertAlmostEqual(width * scale, 1280., places=3)
        self.assertEqual(struct.unpack_from('<f', after, joy + 0x50)[0], 556.)   # height (and children) unchanged
        self.assertAlmostEqual(coords[5] - coords[1], scale, places=5)          # visible texture height shrinks as width grows
        self.assertAlmostEqual(coords[1], 1 - coords[5], places=5)              # cropped evenly
        self.assertAlmostEqual(struct.unpack_from('<f', after, index['PicOtherCon'][0] + 0x4c)[0] * scale, 1280., places=3)
        with self.assertRaisesRegex(ValueError, 'texture coordinates'):
            cropped = bytearray(boot)
            struct.pack_into('<f', cropped, joy + 0x54 + 24, .5)   # already cropped: not the measured picture
            phone.patch_layout(bytes(cropped), 'BootLoading.bflyt', [], [])
        point8 = struct.unpack('<f', struct.pack('<f', .8))[0]
        mode = bytearray(layout([('PicBase', b'pic1', 0., 160., 1600., 1600.), ('PicFade', b'pic1', 0., 0., 1280., 720.)]))
        order, index = phone.panes(bytes(mode), 'ModeSelect')
        struct.pack_into('<2f', mode, index['PicBase'][0] + 0x44, point8, point8)
        after = phone.patch_layout(bytes(mode), 'ModeSelect.bflyt', [], [])
        sx, sy = struct.unpack_from('<2f', after, index['PicBase'][0] + 0x44)
        self.assertAlmostEqual(sx * 1600 * scale, 1280., places=2)   # covers the width; uniform
        self.assertAlmostEqual(sx, sy, places=6)
        head = phone.patch_layout(layout([('PicBG', b'pic1', 0., 0., 1280., 720.), ('PicBGLightT', b'pic1', 0., 10., 1280., 350.),
                                          ('PicBGLightB', b'pic1', 0., 10., 1280., 370.), ('PicBase', b'pic1', 0., 360., 1280., 148.)]),
                                  'ClosetHead.bflyt', [], [])
        order, index = phone.panes(head, 'ClosetHead')
        for pane in ('PicBG', 'PicBGLightT', 'PicBGLightB', 'PicBase'):
            self.assertAlmostEqual(struct.unpack_from('<f', head, index[pane][0] + 0x4c)[0] * scale, 1280., places=3)
        closet = phone.patch_layout(layout([('ParCounterT', b'prt1', 574., 313., 128., 102.), ('ParCounterB', b'prt1', 574., -214., 128., 102.)]),
                                    'Closet.bflyt', [], [])
        order, index = phone.panes(closet, 'Closet')
        x = struct.unpack_from('<f', closet, index['ParCounterT'][0] + 0x2c)[0]
        self.assertAlmostEqual(640 / scale - x, 640 - 574, places=3)   # same distance (in its root) from the panel edge

    def test_boot_strap_slides_in_from_beyond_the_panel_edge(self):
        def flan(tracks):   # one pai1 section: targets with one FLPA tag of Hermite keys each
            body = bytearray()
            entries = []
            for target, channel, keys in tracks:
                content = bytearray(target.encode().ljust(28, b'\0') + bytes([1, 0, 0, 0]) + struct.pack('<I', 36))
                tag = bytearray(b'FLPA' + bytes([1, 0, 0, 0]) + struct.pack('<I', 12))
                entry = bytearray(bytes([0, channel, 2, 0]) + struct.pack('<HHI', len(keys), 0, 12))
                for k in keys:
                    entry += struct.pack('<3f', *k)
                entries.append(content + tag + entry)
            table_at = 20
            offsets, at = [], table_at + 4 * len(entries)
            for e in entries:
                offsets.append(at); at += len(e)
            pai1 = bytearray(b'pai1' + struct.pack('<I', 0) + bytes(6) + struct.pack('<HI', len(entries), table_at))
            pai1 += b''.join(struct.pack('<I', o) for o in offsets) + b''.join(entries)
            struct.pack_into('<I', pai1, 4, len(pai1))
            head = bytearray(b'FLAN\xff\xfe' + struct.pack('<HIIH', 20, 0, 20 + len(pai1), 1) + bytes(2))
            return bytes(head + pai1)
        before = flan([('PicStrap', 0, [(200., 980., 0.), (200., 980., -8.5), (240., 640., 0.)]),
                       ('PicJoyCon', 0, [(200., 0., 0.), (240., -154., 0.)])])
        records = []
        after = phone.patch_strap_animation(before, 'BootLoading_Appear.bflan', records)
        shift = 640 * (1 - float(phone.SCALE_X)) / float(phone.SCALE_X)
        tracks = {t['target']: t['offset'] for t in common.animation_tracks(after, 'x')}
        keys = [struct.unpack_from('<3f', after, tracks['PicStrap'] + 12 + 12 * i) for i in range(3)]
        self.assertEqual([k[0] for k in keys], [200., 200., 240.])
        self.assertEqual([k[2] for k in keys], [0., -8.5, 0.])   # same slide, same speed
        self.assertAlmostEqual(keys[2][1] * float(phone.SCALE_X), 640., places=3)
        self.assertGreaterEqual((keys[0][1] - 340) * float(phone.SCALE_X), 640. - 1e-3)   # starts wholly beyond the edge
        joycon = tracks['PicJoyCon'] + 12
        self.assertEqual(after[joycon:joycon + 24], before[joycon:joycon + 24])
        self.assertAlmostEqual(records[0]['changes'][0]['after'] - 980., shift, places=3)
        with self.assertRaisesRegex(ValueError, 'strap position'):
            phone.patch_strap_animation(flan([('PicStrap', 0, [(0., 700., 0.)])]), 'BootLoading_Wait.bflan', [])


if __name__ == '__main__':
    unittest.main()
