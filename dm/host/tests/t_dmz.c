// DMZ unit test (dmz.h): files are assembled here byte by byte and every expected view is written out by hand, then read through the file layer (hle_fs.c included directly)
// as the game reads the IPA's RomFS: the size GetFileSize and listings report, reads across chunks and Yaz0 groups, damaged files refused, a save file never taken for one, and bundle lookups.
#include "../hle_fs.c"
#include <compression.h>
struct dm_flags g_flag; uint64_t g_t0_ns;
uint64_t now_ns(void) { return 0; }
void dm_guest_write_prepare(void *a, size_t n) { (void)a; (void)n; }
int dm_guest_as_contains(uintptr_t a) { (void)a; return 0; }
static int checks, fails;
#define CHECK(c, text) do { checks++; int ok = !!(c); fails += !ok; printf("%s %s\n", ok ? "✓" : "✗", text); } while (0)

static uint8_t file[1 << 16]; static size_t flen;
static void p64(uint8_t *p, uint64_t v) { for (int i = 0; i < 8; i++) p[i] = (uint8_t)(v >> 8 * i); }
// A DMZ file of chunks (data, length, codec) with chunk size 4096 (log2 12)
static void build(int view, uint64_t payload, uint64_t size, const uint8_t *yaz0, int n, const uint8_t **data, const size_t *len, const uint8_t *codec) {
    memset(file, 0, sizeof file);
    memcpy(file, "DMZ1", 4); file[4] = (uint8_t)view; file[5] = 12; file[8] = (uint8_t)n; p64(file + 0x10, payload); p64(file + 0x18, size);
    if (yaz0) memcpy(file + 0x20, yaz0, 16);
    size_t at = 0x30 + (n + 1) * 8 + ((n + 7) & ~7);
    for (int i = 0; i <= n; i++) { p64(file + 0x30 + i * 8, at); if (i < n) { memcpy(file + at, data[i], len[i]); at += len[i]; } }
    memcpy(file + 0x30 + (n + 1) * 8, codec, n); flen = at;
}
static void put(const char *dir, const char *name) { char p[1024]; snprintf(p, sizeof p, "%s/%s", dir, name); FILE *f = fopen(p, "wb"); fwrite(file, 1, flen, f); fclose(f); }
static int read_at(const char *path, int64_t off, size_t n, uint8_t *out, size_t *got) {
    uint64_t h; if (h_fs_OpenFile(&h, path, MODE_READ)) return -1;
    Result r = h_fs_ReadFile(got, h, off, out, n); h_fs_CloseFile(h); return r ? -1 : 0;
}
static int64_t size_of(const char *path) { uint64_t h; int64_t s = -1; if (h_fs_OpenFile(&h, path, MODE_READ)) return -1; h_fs_GetFileSize(&s, h); h_fs_CloseFile(h); return s; }
static int64_t listed(const char *dir, const char *name) {
    uint64_t h; if (h_fs_OpenDirectory(&h, dir, 3)) return -2;
    static DirectoryEntry e[16]; int64_t n = 0, s = -1; h_fs_ReadDirectory(&n, e, h, 16); h_fs_CloseDirectory(h);
    for (int i = 0; i < n; i++) if (!strcmp(e[i].name, name)) s = e[i].size;
    return s;
}

int main(void) {
    char root[] = "/tmp/t_dmz.XXXXXX", save[1100]; if (!mkdtemp(root)) return 1;
    snprintf(save, sizeof save, "%s/save", root); mkdir(save, 0755);
    char rom[1100]; snprintf(rom, sizeof rom, "%s/rom", root); mkdir(rom, 0755);
    setenv("DM_ROMFS", rom, 1); setenv("DM_SAVE", save, 1);

    // (1) payload view, two chunks: 4096 bytes compressed, then 904 stored
    static uint8_t payload[5000], z[4096]; for (int i = 0; i < 5000; i++) payload[i] = (uint8_t)(i % 251 < 120 ? 'a' : i * 7);
    size_t zl = compression_encode_buffer(z, 4095, payload, 4096, NULL, COMPRESSION_LZFSE);
    const uint8_t *d1[] = {z, payload + 4096}; size_t l1[] = {zl, 904}; uint8_t c1[] = {DMZ_LZFSE, DMZ_STORED};
    build(DMZ_VIEW_PAYLOAD, 5000, 5000, NULL, 2, d1, l1, c1); put(rom, "plain.bin");
    snprintf((char *)file, sizeof file, "raw file"); flen = 8; put(rom, "raw.txt");
    const uint8_t yaz0[16] = {'Y', 'a', 'z', '0', 0, 0, 0, 10, 0, 0, 0x20, 0};
    const uint8_t *d2[] = {(const uint8_t *)"ABCDEFGHIJ"}; size_t l2[] = {10}; uint8_t c2[] = {DMZ_STORED};
    build(DMZ_VIEW_YAZ0, 10, 28, yaz0, 1, d2, l2, c2); put(rom, "arc.szs");
    build(DMZ_VIEW_PAYLOAD, 5000, 5000, NULL, 2, d1, l1, c1); flen -= 1; put(rom, "short.bin");   // the last chunk ends past the file
    build(DMZ_VIEW_PAYLOAD, 5000, 4999, NULL, 2, d1, l1, c1); put(rom, "size.bin");
    const uint8_t *d3[] = {payload, payload + 4096}; size_t l3[] = {4096, 904}; uint8_t c3[] = {DMZ_LZFSE, DMZ_STORED};
    build(DMZ_VIEW_PAYLOAD, 5000, 5000, NULL, 2, d3, l3, c3); put(rom, "badz.bin");
    uint8_t zz[4096]; memcpy(zz, z, zl); zz[zl / 2] ^= 0x55;
    const uint8_t *d4[] = {zz, payload + 4096}; build(DMZ_VIEW_PAYLOAD, 5000, 5000, NULL, 2, d4, l1, c1); put(rom, "flip.bin");
    build(DMZ_VIEW_PAYLOAD, 5000, 5000, NULL, 2, d1, l1, c1); put(save, "game.sav"); size_t save_len = flen;
    // the RomFS is fixed once mounted (the host keeps a name index per folder), so every file is written first
    CHECK(h_fs_MountRom("rom", NULL, 0) == RES_OK && h_fs_MountSave("save", NULL) == RES_OK, "mount rom and save");
    uint8_t b[6000]; size_t got = 0;
    CHECK(size_of("rom:/plain.bin") == 5000, "GetFileSize of a payload view is the payload size (5000), not the file's");
    CHECK(listed("rom:/", "plain.bin") == 5000, "the listing reports 5000 too");
    CHECK(!read_at("rom:/plain.bin", 4000, 200, b, &got) && got == 200 && !memcmp(b, payload + 4000, 200), "a read across the LZFSE chunk into the stored one");
    CHECK(!read_at("rom:/plain.bin", 4990, 100, b, &got) && got == 10 && !memcmp(b, payload + 4990, 10), "a read past the end stops at 5000 (10 bytes)");
    CHECK(!read_at("rom:/raw.txt", 0, 64, b, &got) && got == 8 && !memcmp(b, "raw file", 8) && size_of("rom:/raw.txt") == 8, "any other file reads as it is");

    // (2) Yaz0 view of "ABCDEFGHIJ" behind a header with alignment 0x2000: header, ff ABCDEFGH, ff IJ = 28 bytes
    uint8_t want[28]; memcpy(want, yaz0, 16); memcpy(want + 16, "\xff" "ABCDEFGH" "\xff" "IJ", 12);
    CHECK(size_of("rom:/arc.szs") == 28 && listed("rom:/", "arc.szs") == 28, "the Yaz0 view is 28 bytes, by GetFileSize and the listing");
    CHECK(!read_at("rom:/arc.szs", 0, 64, b, &got) && got == 28 && !memcmp(b, want, 28), "the whole view: the original header, then a control byte 0xff before every 8 bytes");
    CHECK(!read_at("rom:/arc.szs", 20, 6, b, &got) && got == 6 && !memcmp(b, "DEFGH\xff", 6), "a read from the middle of a group (offset 20: DEFGH, then the next control byte)");
    CHECK(!read_at("rom:/arc.szs", 10, 8, b, &got) && got == 8 && !memcmp(b, "\x20\0\0\0\0\0\xff" "A", 8), "a read across the header into the first group");

    // (3) damaged files are refused, not read as something else
    uint64_t h; CHECK(h_fs_OpenFile(&h, "rom:/short.bin", MODE_READ) == RES_FAIL, "a file shorter than its chunk table is refused at open");
    CHECK(h_fs_OpenFile(&h, "rom:/size.bin", MODE_READ) == RES_FAIL, "a view size that disagrees with the payload is refused");
    CHECK(h_fs_OpenFile(&h, "rom:/badz.bin", MODE_READ) == RES_FAIL, "an LZFSE chunk as long as the chunk itself is refused (it would have been stored)");
    CHECK(read_at("rom:/flip.bin", 0, 5000, b, &got) < 0 || memcmp(b, payload, 5000), "a corrupted LZFSE chunk is never read back as the payload");
    CHECK(size_of("save:/game.sav") == (int64_t)save_len && listed("save:/", "game.sav") == (int64_t)save_len, "on the save mount the same bytes are an ordinary file");

    // (4) bundle: two files by name; lookups of a missing name and of a prefix fail
    static uint8_t bp[256]; memset(bp, 0, sizeof bp);
    memcpy(bp, "DMZP", 4); bp[4] = 2; bp[8] = 6 + 10;
    p64(bp + 16, 80); p64(bp + 24, 3); bp[32] = 0; bp[36] = 6;    // a.json at 80, 3 bytes, name 0..6
    p64(bp + 40, 96); p64(bp + 48, 5); bp[56] = 6; bp[60] = 10;   // b.metallib at 96, 5 bytes, name 6..16
    memcpy(bp + 64, "a.jsonb.metallib", 16); memcpy(bp + 80, "{}\n", 3); memcpy(bp + 96, "MTLB!", 5);
    const uint8_t *d5[] = {bp}; size_t l5[] = {112}; uint8_t c5[] = {DMZ_STORED};
    build(DMZ_VIEW_PAYLOAD, 112, 112, NULL, 1, d5, l5, c5); put(rom, "shaders.dmz");
    char p[1200]; snprintf(p, sizeof p, "%s/shaders.dmz", rom); int fd = open(p, O_RDONLY); dmz_bundle *bu = dmz_bundle_open(fd); size_t n = 0;
    uint8_t *x = bu ? dmz_bundle_read(bu, "b.metallib", &n) : NULL;
    CHECK(x && n == 5 && !memcmp(x, "MTLB!", 5), "the bundle gives b.metallib back (MTLB!)"); free(x);
    x = bu ? dmz_bundle_read(bu, "a.json", &n) : NULL;
    CHECK(x && n == 3 && !memcmp(x, "{}\n", 3), "and a.json"); free(x);
    errno = 0; CHECK(bu && !dmz_bundle_read(bu, "a.js", &n) && errno == ENOENT && !dmz_bundle_read(bu, "c", &n), "a prefix of a name, or a name it does not hold, is not found");
    dmz_bundle_close(bu); close(fd);
    memcpy(bp + 64, "b.jsona.metallib", 16); build(DMZ_VIEW_PAYLOAD, 112, 112, NULL, 1, d5, l5, c5); put(rom, "unsorted.dmz");
    snprintf(p, sizeof p, "%s/unsorted.dmz", rom); fd = open(p, O_RDONLY);
    CHECK(!dmz_bundle_open(fd), "a bundle whose names are out of order is refused"); close(fd);

    char cmd[1200]; snprintf(cmd, sizeof cmd, "rm -rf %s", root); system(cmd);
    printf("%d checks, %d failed\n", checks, fails);
    return fails != 0;
}
