// Write DMZ files (dm/host/dmz.h) on the Mac, for the IPA: every file written is read back through the host's own reader (dm/host/dmz.c) and compared before it counts.
//   dmz pack [-c log2] [--payload <p>] <in> <out>
//                                     one file; a Yaz0 original gets the Yaz0 view over its decompressed bytes (checked by decoding the view as Yaz0), any other file the payload view.
//                                     --payload: the payload is file <p>, an edited copy of the decompressed original (dm/tools/texcap.py); <in> gives only the Yaz0 header
//   dmz decode <in>                   the decompressed bytes of a Yaz0 file (any other file as it is) to stdout
//   dmz bundle [-c log2] <out> <dir>  the regular files of <dir> in one payload view behind a name index (dmz_bundle_* in dmz.h): the iOS shader libraries
//   dmz cat <in>                      the view to stdout
// Chunks are zstd level 19 (DMZ_LEVEL overrides; DMZ_CODEC=lzfse for Apple's LZFSE), compressed in parallel; a chunk that does not shrink is stored. Exit status 0 = written and checked.
// Build (zstd's encoder from Homebrew, its decoder from dm/third_party/zstd through dm/host/dmz.c):
//   cc -O2 -I$(brew --prefix zstd)/include -o dmz dm/tools/dmz.c dm/host/dmz.c $(brew --prefix zstd)/lib/libzstd.a -lcompression   (its zstd.h is the same 1.5.7)
#include "../host/dmz.h"
#include <compression.h>
#include <zstd.h>
#include <dirent.h>
#include <dispatch/dispatch.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static void die(const char *fmt, const char *a) { fprintf(stderr, "dmz: "); fprintf(stderr, fmt, a); fprintf(stderr, "\n"); exit(1); }
static uint8_t *slurp(const char *path, size_t *n) {
    int fd = open(path, O_RDONLY); struct stat st;
    if (fd < 0 || fstat(fd, &st)) die("cannot open %s", path);
    uint8_t *b = malloc((size_t)st.st_size + 1); if (!b) die("out of memory for %s", path);
    for (size_t got = 0; got < (size_t)st.st_size;) { ssize_t r = read(fd, b + got, (size_t)st.st_size - got); if (r <= 0) die("cannot read %s", path); got += (size_t)r; }
    close(fd); *n = (size_t)st.st_size; return b;
}
static void put64(uint8_t *p, uint64_t v) { for (int i = 0; i < 8; i++) p[i] = (uint8_t)(v >> 8 * i); }
static void put32(uint8_t *p, uint32_t v) { for (int i = 0; i < 4; i++) p[i] = (uint8_t)(v >> 8 * i); }
static uint32_t be32(const uint8_t *p) { return (uint32_t)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3]; }

// Yaz0 decoding with every reference and length checked; NULL when the stream is damaged
static uint8_t *yaz0_decode(const uint8_t *s, size_t n, size_t *out_n) {
    if (n < 16 || memcmp(s, "Yaz0", 4)) return NULL;
    size_t size = be32(s + 4), p = 16, o = 0; uint8_t *d = malloc(size + 1); if (!d) return NULL;
    while (o < size) {
        if (p >= n) goto bad;
        uint8_t ctl = s[p++];
        for (int b = 7; b >= 0 && o < size; b--) {
            if (ctl >> b & 1) { if (p >= n) goto bad; d[o++] = s[p++]; continue; }
            if (p + 2 > n) goto bad;
            size_t dist = ((size_t)(s[p] & 15) << 8 | s[p + 1]) + 1, len = s[p] >> 4; p += 2;
            if (!len) { if (p >= n) goto bad; len = (size_t)s[p++] + 0x12; } else len += 2;
            if (dist > o || len > size - o) goto bad;
            for (size_t i = 0; i < len; i++, o++) d[o] = d[o - dist];
        }
    }
    *out_n = size; return d;
bad:
    free(d); return NULL;
}

// Write payload under the given view; then read the whole view back through dmz.c
static void write_dmz(const char *out, const uint8_t *payload, uint64_t n, int view, const uint8_t yaz0[16], int log2) {
    uint64_t chunk = 1ull << log2, count = (n + chunk - 1) / chunk;
    if (count > UINT32_MAX) die("too many chunks for %s", out);
    uint8_t **data = calloc(count + 1, sizeof *data); size_t *len = calloc(count + 1, sizeof *len); uint8_t *codec = calloc(count + 8, 1);
    if (!data || !len || !codec) die("out of memory for %s", out);
    __block int failed = 0;
    const char *ce = getenv("DMZ_CODEC"), *le = getenv("DMZ_LEVEL"); int lzfse = ce && !strcmp(ce, "lzfse"), level = le ? atoi(le) : 19;
    dispatch_apply((size_t)count, dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^(size_t i) {
        uint64_t at = i * chunk, k = n - at < chunk ? n - at : chunk;
        uint8_t *z = malloc(k);
        size_t c = 0;   // at most k - 1 bytes: a chunk that does not shrink is stored
        if (z && lzfse) c = compression_encode_buffer(z, k - 1, payload + at, k, NULL, COMPRESSION_LZFSE);
        else if (z && k > 1) { c = ZSTD_compress(z, k - 1, payload + at, k, level); if (ZSTD_isError(c)) c = 0; }
        if (!z) { failed = 1; return; }
        if (c) { data[i] = z; len[i] = c; codec[i] = lzfse ? DMZ_LZFSE : DMZ_ZSTD; }
        else { free(z); data[i] = (uint8_t *)payload + at; len[i] = k; codec[i] = DMZ_STORED; }
    });
    if (failed) die("out of memory compressing %s", out);
    size_t table = (count + 1) * 8, codecs = (count + 7) & ~7ull;
    uint8_t *h = calloc(1, DMZ_HEADER + table + codecs); if (!h) die("out of memory for %s", out);
    memcpy(h, DMZ_MAGIC, 4); h[4] = (uint8_t)view; h[5] = (uint8_t)log2; put32(h + 8, (uint32_t)count);
    put64(h + 0x10, n); put64(h + 0x18, view == DMZ_VIEW_YAZ0 ? dmz_yaz0_view_size(n) : n);
    if (view == DMZ_VIEW_YAZ0) memcpy(h + 0x20, yaz0, 16);
    uint64_t at = DMZ_HEADER + table + codecs;
    for (uint64_t i = 0; i <= count; i++) { put64(h + DMZ_HEADER + i * 8, at); if (i < count) at += len[i]; }
    memcpy(h + DMZ_HEADER + table, codec, count);
    char tmp[4096]; snprintf(tmp, sizeof tmp, "%s.partial", out);
    FILE *f = fopen(tmp, "wb"); if (!f) die("cannot create %s", tmp);
    int ok = fwrite(h, 1, DMZ_HEADER + table + codecs, f) == DMZ_HEADER + table + codecs;
    for (uint64_t i = 0; ok && i < count; i++) ok = fwrite(data[i], 1, len[i], f) == len[i];
    if (fclose(f) || !ok) die("cannot write %s", tmp);
    for (uint64_t i = 0; i < count; i++) if (codec[i] != DMZ_STORED) free(data[i]);
    free(data); free(len); free(codec); free(h);

    int fd = open(tmp, O_RDONLY); dmz *z = fd < 0 ? NULL : dmz_open(fd);
    if (!z) die("the reader does not accept %s", tmp);
    uint64_t size = dmz_size(z); uint8_t *v = malloc(size + 1);
    if (!v || dmz_pread(z, v, size, 0) != (ssize_t)size) die("the reader cannot read %s back", tmp);
    if (view == DMZ_VIEW_YAZ0) {
        size_t m; uint8_t *d = yaz0_decode(v, size, &m);
        if (!d || m != n || memcmp(d, payload, n) || memcmp(v, yaz0, 16)) die("the Yaz0 view of %s does not decode to the original", tmp);
        free(d);
    } else if (size != n || memcmp(v, payload, n)) die("%s does not read back as the original", tmp);
    for (uint64_t off = 0; off < size; off += size / 7 + 1) {   // reads that start mid-chunk and mid-group agree with the whole read
        uint8_t b[300]; size_t k = size - off < sizeof b ? (size_t)(size - off) : sizeof b;
        if (dmz_pread(z, b, k, off) != (ssize_t)k || memcmp(b, v + off, k)) die("a partial read of %s disagrees", tmp);
    }
    free(v); dmz_close(z); close(fd);
    if (rename(tmp, out)) die("cannot rename to %s", out);
}

static int name_cmp(const void *a, const void *b) { return strcmp(*(char *const *)a, *(char *const *)b); }
int main(int argc, char **argv) {
    int log2 = 21, a = 2;
    if (argc > 3 && !strcmp(argv[2], "-c")) { log2 = atoi(argv[3]); a = 4; }
    if (log2 < 12 || log2 > 24) die("chunk size log2 %s out of range (12 to 24)", argc > 3 ? argv[3] : "");
    const char *edited = NULL;
    if (argc > a + 1 && !strcmp(argv[1], "pack") && !strcmp(argv[a], "--payload")) { edited = argv[a + 1]; a += 2; }
    if (argc == a + 2 && !strcmp(argv[1], "pack")) {
        size_t n; uint8_t *src = slurp(argv[a], &n);
        if (n >= 16 && !memcmp(src, "Yaz0", 4)) {
            size_t m; uint8_t *d = edited ? slurp(edited, &m) : yaz0_decode(src, n, &m);
            if (!d) die("%s is a damaged Yaz0 stream", argv[a]);
            if (edited && m != be32(src + 4)) die("the edited payload %s is not the size the Yaz0 header gives", edited);   // the header (decompressed size, alignment) is the original's
            write_dmz(argv[a + 1], d, m, DMZ_VIEW_YAZ0, src, log2); free(d);
        } else if (edited) { size_t m; uint8_t *d = slurp(edited, &m); write_dmz(argv[a + 1], d, m, DMZ_VIEW_PAYLOAD, NULL, log2); free(d); }
        else write_dmz(argv[a + 1], src, n, DMZ_VIEW_PAYLOAD, NULL, log2);
        free(src); return 0;
    }
    if (argc == 3 && !strcmp(argv[1], "decode")) {
        size_t n, m; uint8_t *src = slurp(argv[2], &n), *d = n >= 16 && !memcmp(src, "Yaz0", 4) ? yaz0_decode(src, n, &m) : (m = n, src);
        if (!d) die("%s is a damaged Yaz0 stream", argv[2]);
        if (fwrite(d, 1, m, stdout) != m || fflush(stdout)) die("cannot write the decoded %s", argv[2]);
        return 0;
    }
    if (argc == a + 2 && !strcmp(argv[1], "bundle")) {   // payload: DMZ_BUNDLE_MAGIC, count, names size, 0, entries {u64 offset, u64 size, u32 name offset, u32 name length} by name, names, then the files 16-aligned
        DIR *d = opendir(argv[a + 1]); if (!d) die("cannot open %s", argv[a + 1]);
        size_t cap = 1024, count = 0, names = 0; char **list = malloc(cap * sizeof *list); struct dirent *e;
        while ((e = readdir(d))) {
            char p[4096]; struct stat st; snprintf(p, sizeof p, "%s/%s", argv[a + 1], e->d_name);
            if (e->d_name[0] == '.' || stat(p, &st) || !S_ISREG(st.st_mode)) continue;
            if (count == cap && !(list = realloc(list, (cap *= 2) * sizeof *list))) die("out of memory for %s", argv[a + 1]);
            list[count++] = strdup(e->d_name); names += strlen(e->d_name);
        }
        closedir(d);
        qsort(list, count, sizeof *list, name_cmp);
        uint64_t head = DMZ_BUNDLE_HEADER + count * DMZ_BUNDLE_ENTRY + names, at = (head + 15) & ~15ull, total = at;
        uint64_t *size = malloc((count + 1) * sizeof *size);
        for (size_t i = 0; i < count; i++) { char p[4096]; struct stat st; snprintf(p, sizeof p, "%s/%s", argv[a + 1], list[i]); stat(p, &st); size[i] = (uint64_t)st.st_size; total += (size[i] + 15) & ~15ull; }
        uint8_t *payload = calloc(1, total + 1); if (!payload) die("out of memory for %s", argv[a]);
        memcpy(payload, DMZ_BUNDLE_MAGIC, 4); put32(payload + 4, (uint32_t)count); put32(payload + 8, (uint32_t)names);
        uint64_t name_at = DMZ_BUNDLE_HEADER + count * DMZ_BUNDLE_ENTRY;
        for (size_t i = 0; i < count; i++) {
            uint8_t *ent = payload + DMZ_BUNDLE_HEADER + i * DMZ_BUNDLE_ENTRY; size_t k = strlen(list[i]);
            put64(ent, at); put64(ent + 8, size[i]); put32(ent + 16, (uint32_t)(name_at - DMZ_BUNDLE_HEADER - count * DMZ_BUNDLE_ENTRY)); put32(ent + 20, (uint32_t)k);
            memcpy(payload + name_at, list[i], k); name_at += k;
            char p[4096]; snprintf(p, sizeof p, "%s/%s", argv[a + 1], list[i]);
            size_t m; uint8_t *f = slurp(p, &m); if (m != size[i]) die("%s changed while bundling", p);
            memcpy(payload + at, f, m); free(f); at += (m + 15) & ~15ull;
        }
        write_dmz(argv[a], payload, total, DMZ_VIEW_PAYLOAD, NULL, log2);
        int fd = open(argv[a], O_RDONLY); dmz_bundle *b = fd < 0 ? NULL : dmz_bundle_open(fd);   // every file comes back by its name
        if (!b) die("the reader does not accept the bundle %s", argv[a]);
        for (size_t i = 0; i < count; i++) {
            char p[4096]; snprintf(p, sizeof p, "%s/%s", argv[a + 1], list[i]);
            size_t m, k; uint8_t *f = slurp(p, &m), *g = dmz_bundle_read(b, list[i], &k);
            if (!g || k != m || memcmp(f, g, m)) die("%s does not come back from the bundle", p);
            free(f); free(g); free(list[i]);
        }
        if (dmz_bundle_read(b, "", &names)) die("the bundle %s answers a name it does not hold", argv[a]);
        dmz_bundle_close(b); close(fd); free(list); free(size); free(payload);
        printf("bundle: %zu files\n", count); return 0;
    }
    if (argc == 3 && !strcmp(argv[1], "cat")) {
        int fd = open(argv[2], O_RDONLY); dmz *z = fd < 0 ? NULL : dmz_open(fd);
        if (!z) die("%s is not a DMZ file", argv[2]);
        uint8_t *b = malloc(1 << 20);
        for (uint64_t off = 0; off < dmz_size(z);) { ssize_t r = dmz_pread(z, b, 1 << 20, off); if (r <= 0 || fwrite(b, 1, (size_t)r, stdout) != (size_t)r) die("cannot read %s", argv[2]); off += (uint64_t)r; }
        return 0;
    }
    fprintf(stderr, "usage: dmz pack [-c log2] [--payload <p>] <in> <out> | dmz decode <in> | dmz bundle [-c log2] <out> <dir> | dmz cat <in>\n");
    return 2;
}
