#!/usr/bin/env python3
# Mirror a RomFS folder for the IPA, smaller: files with a listed suffix are packed as DMZ (dm/host/dmz.h) by the dmz tool (dm/tools/dmz.c, which reads every file back
# through the host's reader before it counts); with --tex-max, the textures in .szs archives are capped first (dm/tools/texcap.py, per-path --tex-rule overrides);
# with --movie-crf, .mp4 movies are re-encoded (H.264 at that CRF, AAC 128 kb/s, same size, frame rate, keyframe spacing and B-frame depth) and checked to decode
# to as many frames at the same size. Every other file, and any file the work does not shrink by --min-gain, is hard-linked (cloned when the destination is on another volume).
# Reruns redo only files whose size, modification time or recipe (the settings that apply to it) changed, recorded in <dst>.json beside the mirror; files gone from the source go.
# Usage: dmz_tree.py <dmz tool> <src> <dst> --suffix .szs:.bfstm [--tex-max 512 --tex-rule 'ObjectData/Sky*=1024' ...] [--movie-crf 26] [--min-gain 0.02] [--jobs N]
import argparse, concurrent.futures as cf, fnmatch, json, os, shutil, subprocess, sys, tempfile
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import texcap

MOVIE = ['-map', '0', '-fps_mode', 'passthrough', '-c:v', 'libx264', '-preset', 'slow', '-profile:v', 'high', '-level', '3.0', '-pix_fmt', 'yuv420p',
         '-g', '60', '-keyint_min', '60', '-sc_threshold', '0', '-x264-params', 'bframes=1:b-pyramid=0', '-c:a', 'aac_at', '-b:a', '128k', '-movflags', '+faststart']


def same_file(a, b):
    try: return os.path.samestat(os.stat(a), os.stat(b))
    except FileNotFoundError: return False


def link(src, dst):
    if same_file(src, dst): return
    if os.path.lexists(dst): os.unlink(dst)
    try: os.link(src, dst)
    except OSError: subprocess.run(['cp', '-c', src, dst], check=True)   # another volume: an APFS clone where possible, else a copy


def inside(path, folder):
    """Whether path is folder or inside it, by the folders themselves (names alone miss RomFS and romfs on a case-insensitive volume)."""
    p = os.path.abspath(path)
    while True:
        if os.path.exists(p) and os.path.exists(folder) and os.path.samefile(p, folder): return True
        if os.path.dirname(p) == p: return False
        p = os.path.dirname(p)


def video(path):
    """(codec, width, height, decoded frames) of the first video stream."""
    r = subprocess.run(['ffprobe', '-v', 'error', '-select_streams', 'v:0', '-count_frames', '-show_entries', 'stream=codec_name,width,height,nb_read_frames',
                        '-of', 'csv=p=0', path], capture_output=True, text=True, check=True)
    return tuple(r.stdout.strip().split(','))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('tool'); ap.add_argument('src'); ap.add_argument('dst')
    ap.add_argument('--suffix', required=True, help='suffixes to pack, colon separated (compared without regard to case)')
    ap.add_argument('--tex-max', type=int, default=0, help='cap textures in .szs archives to this many pixels on their larger side (0: leave them)')
    ap.add_argument('--tex-rule', action='append', default=[], help='PATTERN=N: archives whose RomFS path matches take N instead (0: leave them); the first match wins')
    ap.add_argument('--movie-crf', type=int, default=0, help='re-encode .mp4 movies at this H.264 CRF (0: leave them)')
    ap.add_argument('--min-gain', type=float, default=0.02, help='keep the original unless the work saves at least this fraction')
    ap.add_argument('--jobs', type=int, default=max(1, (os.cpu_count() or 2) // 2))
    a = ap.parse_args()
    src, dst = os.path.abspath(a.src), os.path.abspath(a.dst)
    if inside(dst, src) or inside(src, dst): sys.exit(f'[RED] dmz_tree: {dst} and {src} overlap')
    suffixes = tuple(s.lower() for s in a.suffix.split(':') if s)
    rules = []
    for r in a.tex_rule:
        pattern, sep, n = r.rpartition('=')
        if not sep or not n.isdigit(): sys.exit(f'[RED] dmz_tree: not PATTERN=N: {r}')
        rules.append((pattern, int(n)))
    if a.movie_crf and not shutil.which('ffmpeg'): sys.exit('[RED] dmz_tree: --movie-crf needs ffmpeg (brew install ffmpeg)')

    def cap_of(rel):
        if not a.tex_max or not rel.lower().endswith('.szs'): return 0
        return next((n for pattern, n in rules if fnmatch.fnmatchcase(rel, pattern)), a.tex_max)

    def recipe(rel):
        low = rel.lower()
        if low.endswith(suffixes): return f'dmz2 tex{cap_of(rel)}'   # bump the number when dmz.c's codec or chunking changes
        if low.endswith('.mp4') and a.movie_crf: return f'mp4-1 crf{a.movie_crf}'
        return None

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
        st = os.stat(s); stamp = [st.st_size, st.st_mtime_ns]; how = recipe(rel)
        if not how: link(s, d); continue
        old = manifest.get(rel)
        if old and old['source'] == stamp and old.get('recipe') == how and os.path.exists(d) and (old['packed'] or same_file(s, d)) and os.path.getsize(d) == old['size']:
            new_manifest[rel] = old; continue
        jobs.append((rel, s, d, stamp, how))

    def work(job):
        rel, s, d, stamp, how = job
        tmp, textures = d + '.dmz-work', []
        if rel.lower().endswith('.mp4'):
            r = subprocess.run(['ffmpeg', '-v', 'error', '-y', '-i', s, *MOVIE[:6], '-crf', str(a.movie_crf), *MOVIE[6:], '-f', 'mp4', tmp], capture_output=True, text=True)
            if r.returncode: raise RuntimeError(f'{rel}: ffmpeg: {r.stderr.strip()}')
            if video(tmp) != video(s): raise RuntimeError(f'{rel}: the re-encoded movie does not decode to the same frames ({video(tmp)} against {video(s)})')
        else:
            cmd = [a.tool, 'pack', s, tmp]
            if cap_of(rel):
                payload = bytearray(subprocess.run([a.tool, 'decode', s], capture_output=True, check=True).stdout)
                textures = texcap.cap_file(payload, cap_of(rel))
                if textures:
                    with tempfile.NamedTemporaryFile(dir=os.path.dirname(d), delete=False) as f: f.write(payload)
                    cmd = [a.tool, 'pack', '--payload', f.name, s, tmp]
            r = subprocess.run(cmd, capture_output=True, text=True)
            if len(cmd) > 4: os.unlink(cmd[3])
            if r.returncode: raise RuntimeError(f'{rel}: {r.stderr.strip()}')
        size = os.path.getsize(s)
        if os.path.getsize(tmp) <= size * (1 - a.min_gain):
            if os.path.lexists(d): os.unlink(d)
            os.rename(tmp, d); return rel, {'source': stamp, 'recipe': how, 'packed': True, 'size': os.path.getsize(d), 'textures': len(textures)}
        os.unlink(tmp); link(s, d); return rel, {'source': stamp, 'recipe': how, 'packed': False, 'size': size, 'textures': 0}

    with cf.ThreadPoolExecutor(a.jobs) as ex:
        for i, (rel, entry) in enumerate(ex.map(work, jobs), 1):
            new_manifest[rel] = entry
            if i % 500 == 0: print(f'dmz_tree: {i} / {len(jobs)} done', flush=True)
    for root, ds, fs in os.walk(dst, topdown=False):   # what the source no longer has
        rel = os.path.relpath(root, dst)
        for f in fs:
            r = os.path.normpath(os.path.join(rel, f))
            if r not in keep: os.unlink(os.path.join(root, f))
        if rel not in dirs: shutil.rmtree(root, ignore_errors=True)
    with open(manifest_path + '.partial', 'w') as f: json.dump(new_manifest, f, indent=0, sort_keys=True)
    os.replace(manifest_path + '.partial', manifest_path)
    done = [r for r, e in new_manifest.items() if e['packed']]
    before = sum(os.path.getsize(os.path.join(src, r)) for r in done); after = sum(new_manifest[r]['size'] for r in done)
    textures = sum(e.get('textures', 0) for e in new_manifest.values())
    print(f'dmz_tree: {len(done)} of {len(new_manifest)} files made smaller ({len(jobs)} this run, {textures} textures capped): {before / 1e9:.2f} GB → {after / 1e9:.2f} GB')


if __name__ == '__main__':
    main()
