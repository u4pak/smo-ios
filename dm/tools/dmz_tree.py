#!/usr/bin/env python3
# Mirror a RomFS folder for the IPA with its big files as DMZ (dm/host/dmz.h): files with a listed suffix are packed by the dmz tool (dm/tools/dmz.c, which reads every file back
# through the host's reader before it counts), every other file, and any file packing does not shrink by --min-gain, is hard-linked (cloned when the destination is on another volume).
# Reruns repack only files whose size or modification time changed (recorded in <dst>.json beside the mirror, so the mirror itself holds only game files); files gone from the source go.
# Usage: dmz_tree.py <dmz tool> <src> <dst> --suffix .szs:.bfstm [--min-gain 0.02] [--jobs N]
import argparse, concurrent.futures as cf, json, os, shutil, subprocess, sys


def same_file(a, b):
    try: return os.path.samestat(os.stat(a), os.stat(b))
    except FileNotFoundError: return False


def link(src, dst):
    if same_file(src, dst): return
    if os.path.lexists(dst): os.unlink(dst)
    try: os.link(src, dst)
    except OSError: subprocess.run(['cp', '-c', src, dst], check=True)   # another volume: an APFS clone where possible, else a copy


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('tool'); ap.add_argument('src'); ap.add_argument('dst')
    ap.add_argument('--suffix', required=True, help='suffixes to pack, colon separated (compared without regard to case)')
    ap.add_argument('--min-gain', type=float, default=0.02, help='keep the original unless packing saves at least this fraction')
    ap.add_argument('--jobs', type=int, default=max(1, (os.cpu_count() or 2) // 2))
    a = ap.parse_args()
    src, dst = os.path.abspath(a.src), os.path.abspath(a.dst)
    if dst == src or dst.startswith(src + os.sep) or src.startswith(dst + os.sep): sys.exit(f'[RED] dmz_tree: {dst} and {src} overlap')
    suffixes = tuple(s.lower() for s in a.suffix.split(':') if s)
    manifest_path = dst + '.json'
    try: manifest = json.load(open(manifest_path))
    except (FileNotFoundError, ValueError): manifest = {}
    files, dirs = [], set()
    for root, ds, fs in os.walk(src):
        rel = os.path.relpath(root, src)
        dirs.add(rel)
        files += [os.path.normpath(os.path.join(rel, f)) for f in fs]
    for rel in sorted(dirs): os.makedirs(os.path.join(dst, rel), exist_ok=True)
    keep, jobs, new_manifest = set(files), [], {}
    for rel in files:
        s, d = os.path.join(src, rel), os.path.join(dst, rel)
        st = os.stat(s); stamp = [st.st_size, st.st_mtime_ns]
        if not rel.lower().endswith(suffixes): link(s, d); continue
        old = manifest.get(rel)
        if old and old['source'] == stamp and os.path.exists(d) and (old['packed'] or same_file(s, d)) and os.path.getsize(d) == old['size']:
            new_manifest[rel] = old; continue
        jobs.append((rel, s, d, stamp))

    def pack(job):
        rel, s, d, stamp = job
        tmp = d + '.dmz'
        r = subprocess.run([a.tool, 'pack', s, tmp], capture_output=True, text=True)
        if r.returncode: raise RuntimeError(f'{rel}: {r.stderr.strip()}')
        size = os.path.getsize(s)
        if os.path.getsize(tmp) <= size * (1 - a.min_gain):
            if os.path.lexists(d): os.unlink(d)
            os.rename(tmp, d); return rel, {'source': stamp, 'packed': True, 'size': os.path.getsize(d)}
        os.unlink(tmp); link(s, d); return rel, {'source': stamp, 'packed': False, 'size': size}

    with cf.ThreadPoolExecutor(a.jobs) as ex:
        for i, (rel, entry) in enumerate(ex.map(pack, jobs), 1):
            new_manifest[rel] = entry
            if i % 500 == 0: print(f'dmz_tree: {i} / {len(jobs)} packed', flush=True)
    for root, ds, fs in os.walk(dst, topdown=False):   # what the source no longer has
        rel = os.path.relpath(root, dst)
        for f in fs:
            r = os.path.normpath(os.path.join(rel, f))
            if r not in keep: os.unlink(os.path.join(root, f))
        if rel not in dirs: shutil.rmtree(root, ignore_errors=True)
    with open(manifest_path + '.partial', 'w') as f: json.dump(new_manifest, f, indent=0, sort_keys=True)
    os.replace(manifest_path + '.partial', manifest_path)
    packed = [r for r, e in new_manifest.items() if e['packed']]
    before = sum(os.path.getsize(os.path.join(src, r)) for r in packed); after = sum(new_manifest[r]['size'] for r in packed)
    print(f'dmz_tree: {len(packed)} of {len(new_manifest)} files with {"/".join(suffixes)} packed ({len(jobs)} this run): {before / 1e9:.2f} GB → {after / 1e9:.2f} GB')


if __name__ == '__main__':
    main()
