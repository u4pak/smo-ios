"""The IPA's packed RomFS mirror (dm/tools/dmz_tree.py with dm/tools/dmz.c), on synthetic files only."""
from pathlib import Path
import os
import platform
import struct
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tests'))
from test_touch_glyphs import yaz0


def literal_view(header: bytes, data: bytes) -> bytes:
    """The Yaz0 view written out from its definition: the header, then 0xff before every 8 bytes."""
    return header + b''.join(b'\xff' + data[i:i + 8] for i in range(0, len(data), 8))


@unittest.skipUnless(platform.system() == 'Darwin', 'LZFSE (libcompression) is Apple-only')
class DmzTreeTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        cls.tool = Path(cls.temp.name) / 'dmz'
        subprocess.run(['xcrun', 'clang', '-O2', '-o', cls.tool, ROOT / 'dm/tools/dmz.c', ROOT / 'dm/host/dmz.c', '-lcompression'], check=True)

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def tree(self, src, dst):
        out = subprocess.run([sys.executable, ROOT / 'dm/tools/dmz_tree.py', self.tool, src, dst, '--suffix', '.szs:.bfstm'],
                             capture_output=True, text=True, check=True).stdout
        return out

    def cat(self, path):
        return subprocess.run([self.tool, 'cat', path], capture_output=True, check=True).stdout

    def test_mirror_packs_links_and_follows_the_source(self):
        with tempfile.TemporaryDirectory() as temp:
            src, dst = Path(temp) / 'RomFS', Path(temp) / 'out' / 'romfs'
            (src / 'ObjectData').mkdir(parents=True)
            archive = (b'SARC' + bytes(60) + b'texture ' * 9000)[:70001]
            original = yaz0(archive)
            original = original[:8] + struct.pack('>I', 0x2000) + original[12:]   # alignment, which the view must keep
            (src / 'ObjectData/Stage.szs').write_bytes(original)
            stream = bytes(range(256)) * 400 + bytes(50000)
            (src / 'music.bfstm').write_bytes(stream)
            noise = os.urandom(40000)
            (src / 'noise.bfstm').write_bytes(noise)
            (src / 'movie.mp4').write_bytes(b'movie')
            first = self.tree(src, dst)
            self.assertIn('2 of 3 files', first)   # the random stream does not shrink
            self.assertEqual(self.cat(dst / 'ObjectData/Stage.szs'), literal_view(original[:16], archive))
            self.assertLess((dst / 'ObjectData/Stage.szs').stat().st_size, len(original))
            self.assertEqual(self.cat(dst / 'music.bfstm'), stream)
            for name in ('noise.bfstm', 'movie.mp4'):   # left as they are: the same file, not a copy
                self.assertTrue(os.path.samestat((src / name).stat(), (dst / name).stat()))
            self.assertFalse((dst.parent / 'romfs' / '.dmz-tree.json').exists())
            self.assertTrue(Path(str(dst) + '.json').is_file())
            packed = (dst / 'music.bfstm').stat().st_mtime_ns
            again = self.tree(src, dst)
            self.assertIn('(0 this run)', again)
            self.assertEqual((dst / 'music.bfstm').stat().st_mtime_ns, packed)
            (src / 'music.bfstm').write_bytes(stream[::-1])
            (src / 'movie.mp4').unlink()
            self.assertIn('(1 this run)', self.tree(src, dst))
            self.assertEqual(self.cat(dst / 'music.bfstm'), stream[::-1])
            self.assertFalse((dst / 'movie.mp4').exists())

    def test_a_damaged_archive_stops_the_mirror(self):
        with tempfile.TemporaryDirectory() as temp:
            src = Path(temp) / 'RomFS'
            src.mkdir()
            broken = yaz0(b'abc' * 5000)
            (src / 'Bad.szs').write_bytes(broken[:len(broken) // 2])
            result = subprocess.run([sys.executable, ROOT / 'dm/tools/dmz_tree.py', self.tool, src, Path(temp) / 'out', '--suffix', '.szs'],
                                    capture_output=True, text=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn('damaged Yaz0', result.stderr)


if __name__ == '__main__':
    unittest.main()
