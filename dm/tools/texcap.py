#!/usr/bin/env python3
"""Cap the size of the textures in a game file by dropping their largest mip levels, in place (for the IPA, dm/tools/dmz_tree.py).

Every BNTX in the file is found by its header. A 2D, single-layer, block-linear texture with mip levels whose larger side exceeds the cap loses its top levels:
the remaining levels move to where level 0 was, the header gets the smaller size, level count, block height and image size, the level pointers are rewritten,
and the freed bytes are zeroed, so the file keeps its size and every offset in it (the containers - SARC, BFRES, effect archives - are never touched), and the
zeros cost almost nothing once compressed. A texture is changed only when its level pointers and image size agree exactly with the Tegra block-linear layout
computed here (the host's own algorithm, dm/gfx/nvn/nvn_tex.c), so the moved levels are exactly the texture the smaller header describes; any other texture
(arrays, cube maps, 3D, a format this file does not know, no mips, or a layout that does not agree) is left as it is.
The block height of the new top level is the one the old level had (the shrink rule gives every later level the same block height and size as before).
"""
import struct

BNTX = b'BNTX\0\0\0\0'
ASTC = [(4, 4), (5, 4), (5, 5), (6, 5), (6, 6), (8, 5), (8, 6), (8, 8), (10, 5), (10, 6), (10, 8), (10, 10), (12, 10), (12, 12)]


def block(fmt):
    """(bytes per block, block width, block height) of an nn::gfx image format (its type byte), None when unknown."""
    t = fmt >> 8
    if t in (0x1a, 0x1d): return 8, 4, 4                     # BC1, BC4
    if t in (0x1b, 0x1c, 0x1e, 0x1f, 0x20): return 16, 4, 4   # BC2, BC3, BC5, BC6H, BC7
    if 0x2d <= t <= 0x3a: return (16,) + ASTC[t - 0x2d]
    return {0x02: (1, 1, 1), 0x09: (2, 1, 1), 0x0b: (4, 1, 1), 0x0c: (4, 1, 1)}.get(t)


def adj_tile(shift, dim):
    """nvn_tex.c adj_tile with an 8-row unit: the block height (log2, GOBs) a level of dim block rows gets from the texture's."""
    if not shift: return 0
    x = 8 << (shift - 1)
    if x >= dim:
        while True:
            shift -= 1
            if not shift: break
            x >>= 1
            if x < dim: break
    return shift


def levels(w, h, mips, tile, fmt):
    """Byte offset of each level and the total size, 2D single layer (nvn_tex.c level_offset)."""
    bpb, bw, bh = block(fmt)
    off, out = 0, []
    for i in range(mips):
        lw, lh = -(-max(1, w >> i) // bw), -(-max(1, h >> i) // bh)
        th = adj_tile(tile, lh)
        gobs_wide, gob_rows = -(-lw * bpb // 64), -(-lh // 8)
        out.append(off)
        off += gobs_wide * -(-gob_rows // (1 << th)) << (9 + th)
    return out, off


def cap_file(data: bytearray, limit) -> list:
    """Cap every texture in data (a whole decompressed game file) to limit pixels on its larger side; limit(name) may also pick per texture.
    Returns (name, old w, old h, new w, new h) for each texture changed."""
    changed, b = [], data.find(BNTX)
    while b >= 0:
        changed += cap_bntx(data, b, limit)
        b = data.find(BNTX, b + 8)
    return changed


def cap_bntx(data, b, limit):
    changed = []
    if data[b + 0x20:b + 0x24] != b'NX  ': return changed
    count, info = struct.unpack_from('<iQ', data, b + 0x24)
    for i in range(count):
        t = b + struct.unpack_from('<Q', data, b + info + 8 * i)[0]
        if data[t:t + 4] != b'BRTI': continue
        dim, tilemode, _, mips, samples = struct.unpack_from('<BHHHI', data, t + 0x11)
        fmt, _, w, h, depth, layers, tile = struct.unpack_from('<IIiiiii', data, t + 0x1c)
        size, = struct.unpack_from('<I', data, t + 0x50)
        name_at, _, mips_at = struct.unpack_from('<QQQ', data, t + 0x60)
        n, = struct.unpack_from('<H', data, b + name_at)
        name = bytes(data[b + name_at + 2:b + name_at + 2 + n]).decode('ascii', 'replace')
        cap = limit(name) if callable(limit) else limit
        if not cap or max(w, h) <= cap or dim != 2 or data[t + 0x5c] != 1 or tilemode or samples != 1 or depth != 1 or layers != 1 or block(fmt) is None or not 0 <= tile <= 5:
            continue
        drop = 0
        while max(w >> drop, h >> drop) > cap: drop += 1
        if drop >= mips: continue   # not enough levels to drop (a texture without mips keeps its size)
        mo = struct.unpack_from(f'<{mips}Q', data, b + mips_at)
        offsets, total = levels(w, h, mips, tile, fmt)
        if [m - mo[0] for m in mo] != offsets or total != size: continue   # the data is not laid out as computed: leave it
        base, cut = b + mo[0], mo[drop] - mo[0]
        new_size = size - cut
        data[base:base + new_size] = data[base + cut:base + size]
        data[base + new_size:base + size] = bytes(cut)
        nw, nh, nmips = max(1, w >> drop), max(1, h >> drop), mips - drop
        new_tile = adj_tile(tile, -(-nh // block(fmt)[2]))
        new_offsets, new_total = levels(nw, nh, nmips, new_tile, fmt)
        assert new_offsets == [m - mo[drop] for m in mo[drop:]] and new_total == new_size, name   # the layout argument above, checked on each texture
        struct.pack_into('<H', data, t + 0x16, nmips)
        struct.pack_into('<ii', data, t + 0x24, nw, nh)
        data[t + 0x34] = new_tile   # the packaged layout's block height (log2 GOBs, byte 0 of NVNpackagedTextureLayout)
        struct.pack_into('<I', data, t + 0x50, new_size)
        struct.pack_into(f'<{mips}Q', data, b + mips_at, *[mo[0] + o for o in new_offsets], *[mo[0]] * drop)   # unused pointers stay valid
        changed.append((name, w, h, nw, nh))
    return changed
