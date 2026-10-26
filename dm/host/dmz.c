// DMZ reading (see dmz.h): the chunk table is checked against the file at open, one decoded chunk is kept per open file, and a Yaz0 view is built on the fly
// (the original header, then 0xff before every 8 payload bytes), so a whole archive is never held decompressed.
#include "dmz.h"
#include <compression.h>
#include <errno.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

struct dmz {
    int fd; uint8_t view, log2; uint32_t n; uint64_t payload, size; uint8_t yaz0[16];
    uint64_t *off; uint8_t *codec;
    pthread_mutex_t lock; int64_t cached; uint8_t *chunk, *in; void *scratch;   // the decoded chunk (index cached, -1 none), the compressed bytes read, LZFSE's work memory
};

static uint64_t le64(const uint8_t *p) { uint64_t v = 0; for (int i = 7; i >= 0; i--) v = v << 8 | p[i]; return v; }
static uint32_t le32(const uint8_t *p) { return (uint32_t)p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static uint32_t be32(const uint8_t *p) { return (uint32_t)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3]; }
static int full_pread(int fd, void *buf, size_t n, off_t off) {
    for (size_t got = 0; got < n;) {
        ssize_t r = pread(fd, (uint8_t *)buf + got, n - got, off + (off_t)got);
        if (r < 0 && errno == EINTR) continue;
        if (r <= 0) { if (!r) errno = EIO; return -1; }
        got += (size_t)r;
    }
    return 0;
}
// The fixed header: 0 = valid, 1 = not a DMZ file, -1 = a DMZ file with an impossible header (errno EINVAL)
static int header(const uint8_t *h, uint8_t *view, uint8_t *log2, uint32_t *n, uint64_t *payload, uint64_t *size) {
    if (memcmp(h, DMZ_MAGIC, 4)) return 1;
    *view = h[4]; *log2 = h[5]; *n = le32(h + 8); *payload = le64(h + 0x10); *size = le64(h + 0x18);
    uint64_t chunk = 1ull << (*log2 & 63);
    int ok = *view <= DMZ_VIEW_YAZ0 && *log2 >= 12 && *log2 <= 24 && !h[6] && !h[7] && !le32(h + 0x0c) && *n == (*payload + chunk - 1) / chunk
          && *size == (*view == DMZ_VIEW_YAZ0 ? dmz_yaz0_view_size(*payload) : *payload);
    if (ok && *view == DMZ_VIEW_YAZ0) ok = !memcmp(h + 0x20, "Yaz0", 4) && be32(h + 0x24) == *payload;
    if (!ok) { errno = EINVAL; return -1; }
    return 0;
}

dmz *dmz_open(int fd) {
    uint8_t h[DMZ_HEADER]; struct stat st;
    if (fstat(fd, &st) || st.st_size < DMZ_HEADER) { errno = 0; return NULL; }
    if (full_pread(fd, h, sizeof h, 0)) return NULL;
    dmz *z = calloc(1, sizeof *z); if (!z) return NULL;
    int r = header(h, &z->view, &z->log2, &z->n, &z->payload, &z->size);
    if (r) { free(z); if (r > 0) errno = 0; return NULL; }
    memcpy(z->yaz0, h + 0x20, 16); z->fd = fd; z->cached = -1;
    size_t table = ((size_t)z->n + 1) * 8, codecs = ((size_t)z->n + 7) & ~(size_t)7;
    uint64_t chunk = 1ull << z->log2, data = DMZ_HEADER + table + codecs;
    z->off = malloc(table); z->codec = malloc(codecs + 1);
    if (!z->off || !z->codec || full_pread(fd, z->off, table, DMZ_HEADER) || full_pread(fd, z->codec, codecs, (off_t)(DMZ_HEADER + table))) goto bad;
    for (uint32_t i = 0; i <= z->n; i++) z->off[i] = le64((uint8_t *)&z->off[i]);
    if (z->off[0] != data || z->off[z->n] != (uint64_t)st.st_size) { errno = EINVAL; goto bad; }
    for (uint32_t i = 0; i < z->n; i++) {   // each chunk: stored at its full length, or compressed into fewer bytes (dm/tools/dmz.c stores a chunk LZFSE does not shrink)
        uint64_t len = i + 1 < z->n ? chunk : z->payload - (uint64_t)i * chunk, c = z->off[i + 1] - z->off[i];
        if (z->off[i + 1] < z->off[i] || (z->codec[i] == DMZ_STORED ? c != len : z->codec[i] != DMZ_LZFSE || !c || c >= len)) { errno = EINVAL; goto bad; }
    }
    z->chunk = malloc(chunk + 1); z->in = malloc(chunk); z->scratch = malloc(compression_decode_scratch_buffer_size(COMPRESSION_LZFSE));
    if (!z->chunk || !z->in || !z->scratch || pthread_mutex_init(&z->lock, NULL)) goto bad;
    return z;
bad:
    { int e = errno ? errno : ENOMEM; free(z->off); free(z->codec); free(z->chunk); free(z->in); free(z->scratch); free(z); errno = e; return NULL; }
}
uint64_t dmz_size(const dmz *z) { return z->size; }
void dmz_close(dmz *z) {
    if (!z) return;
    pthread_mutex_destroy(&z->lock); free(z->off); free(z->codec); free(z->chunk); free(z->in); free(z->scratch); free(z);
}
int64_t dmz_listed_size(int fd, int64_t st_size) {
    uint8_t h[DMZ_HEADER], view, log2; uint32_t n; uint64_t payload, size;
    if (st_size < DMZ_HEADER || full_pread(fd, h, sizeof h, 0) || header(h, &view, &log2, &n, &payload, &size)) return st_size;
    return (int64_t)size;
}

// Decode chunk i into z->chunk (lock held)
static int load(dmz *z, uint64_t i) {
    if (z->cached == (int64_t)i) return 0;
    uint64_t chunk = 1ull << z->log2, len = i + 1 < z->n ? chunk : z->payload - i * chunk, c = z->off[i + 1] - z->off[i];
    z->cached = -1;
    if (z->codec[i] == DMZ_STORED) { if (full_pread(z->fd, z->chunk, len, (off_t)z->off[i])) return -1; }
    else {   // one byte of room past the chunk: a stream that decodes to more than the chunk is as damaged as one that decodes to less
        if (full_pread(z->fd, z->in, c, (off_t)z->off[i])) return -1;
        if (compression_decode_buffer(z->chunk, len + 1, z->in, c, z->scratch, COMPRESSION_LZFSE) != len) { errno = EIO; return -1; }
    }
    z->cached = (int64_t)i; return 0;
}
// Copy payload bytes [pos, pos + n), all inside the payload (lock held)
static int payload(dmz *z, uint8_t *dst, uint64_t pos, uint64_t n) {
    while (n) {
        uint64_t i = pos >> z->log2, at = pos & ((1ull << z->log2) - 1), len = i + 1 < z->n ? 1ull << z->log2 : z->payload - (i << z->log2);
        if (load(z, i)) return -1;
        uint64_t k = len - at < n ? len - at : n;
        memcpy(dst, z->chunk + at, k); dst += k; pos += k; n -= k;
    }
    return 0;
}
ssize_t dmz_pread(dmz *z, void *buf, size_t n, uint64_t off) {
    if (off >= z->size) return 0;
    if (n > z->size - off) n = (size_t)(z->size - off);
    uint8_t *o = buf; int r = 0;
    pthread_mutex_lock(&z->lock);
    if (z->view == DMZ_VIEW_PAYLOAD) r = payload(z, o, off, n);
    else for (uint64_t v = off, end = off + n; v < end && !r;) {
        if (v < 16) { uint64_t k = (end < 16 ? end : 16) - v; memcpy(o, z->yaz0 + v, k); o += k; v += k; continue; }
        uint64_t g = (v - 16) / 9, k = (v - 16) % 9;   // group g: control byte 0xff (k = 0), then payload bytes 8g … 8g + 7
        if (!k) { *o++ = 0xff; v++; continue; }
        uint64_t at = g * 8 + k - 1, left = z->payload - g * 8 < 8 ? z->payload - g * 8 : 8, take = left - (k - 1);
        if (take > end - v) take = end - v;
        r = payload(z, o, at, take); o += take; v += take;
    }
    pthread_mutex_unlock(&z->lock);
    return r ? -1 : (ssize_t)n;
}

struct dmz_bundle { dmz *z; uint32_t n; uint8_t *index; const char *names; uint32_t names_size; };
void dmz_bundle_close(dmz_bundle *b) { if (b) { dmz_close(b->z); free(b->index); free(b); } }
dmz_bundle *dmz_bundle_open(int fd) {
    dmz_bundle *b = calloc(1, sizeof *b); uint8_t h[DMZ_BUNDLE_HEADER];
    if (!b) return NULL;
    if (!(b->z = dmz_open(fd))) { int e = errno ? errno : EINVAL; free(b); errno = e; return NULL; }
    if (b->z->view != DMZ_VIEW_PAYLOAD || dmz_pread(b->z, h, sizeof h, 0) != (ssize_t)sizeof h || memcmp(h, DMZ_BUNDLE_MAGIC, 4) || le32(h + 12)) goto bad;
    b->n = le32(h + 4); b->names_size = le32(h + 8);
    uint64_t len = (uint64_t)b->n * DMZ_BUNDLE_ENTRY + b->names_size;
    if (DMZ_BUNDLE_HEADER + len > b->z->payload || !(b->index = malloc(len + 1)) || dmz_pread(b->z, b->index, len, DMZ_BUNDLE_HEADER) != (ssize_t)len) goto bad;
    b->names = (const char *)b->index + (size_t)b->n * DMZ_BUNDLE_ENTRY;
    for (uint32_t i = 0; i < b->n; i++) {   // every file and name inside the bundle, names in strictly increasing order (the lookup is a binary search)
        const uint8_t *e = b->index + (size_t)i * DMZ_BUNDLE_ENTRY; uint64_t at = le64(e), size = le64(e + 8); uint32_t no = le32(e + 16), nl = le32(e + 20);
        if (at > b->z->payload || size > b->z->payload - at || no > b->names_size || nl > b->names_size - no) goto bad;
        if (i) { const uint8_t *p = e - DMZ_BUNDLE_ENTRY; uint32_t po = le32(p + 16), pl = le32(p + 20); int c = memcmp(b->names + po, b->names + no, pl < nl ? pl : nl);
                 if (c > 0 || (!c && pl >= nl)) goto bad; }
    }
    return b;
bad:
    dmz_bundle_close(b); errno = EINVAL; return NULL;
}
void *dmz_bundle_read(dmz_bundle *b, const char *name, size_t *size) {
    size_t k = strlen(name); uint32_t lo = 0, hi = b->n;
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2; const uint8_t *e = b->index + (size_t)mid * DMZ_BUNDLE_ENTRY; uint32_t nl = le32(e + 20);
        int c = memcmp(b->names + le32(e + 16), name, nl < k ? nl : k); if (!c) c = nl < k ? -1 : nl > k;
        if (c < 0) lo = mid + 1; else if (c > 0) hi = mid;
        else {
            uint64_t at = le64(e), n = le64(e + 8); uint8_t *out = malloc((size_t)n + 1);
            if (!out) return NULL;
            if (dmz_pread(b->z, out, (size_t)n, at) != (ssize_t)n) { free(out); errno = EIO; return NULL; }
            out[n] = 0; *size = (size_t)n; return out;
        }
    }
    errno = ENOENT; return NULL;
}
