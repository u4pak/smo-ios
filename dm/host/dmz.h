// DMZ: a read-only game file stored compressed in independent chunks, read back at any offset (the IPA's RomFS; dm/tools/dmz.c writes them).
// The file the game sees (the "view") is either the payload itself, or, for an archive whose original was Yaz0, a Yaz0 stream made only of literals over the
// decompressed payload, behind the original 16-byte Yaz0 header (sead reads the decompressed size and the alignment from it and sizes its buffers by them, never by the file size).
// Layout, little endian:
//   0x00 "DMZ1"   0x04 u8 view (0 payload, 1 Yaz0 literals)   0x05 u8 chunk size log2   0x06 u16 0   0x08 u32 chunk count   0x0c u32 0
//   0x10 u64 payload size   0x18 u64 view size   0x20 the original Yaz0 header (16 bytes; zeros for view 0)
//   0x30 u64 file offset of each chunk, then the end of the last one (count + 1)   then u8 codec of each chunk (0 stored, 1 LZFSE), padded to 8   then the chunks
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#define DMZ_MAGIC "DMZ1"
#define DMZ_HEADER 0x30
enum { DMZ_VIEW_PAYLOAD = 0, DMZ_VIEW_YAZ0 = 1 };
enum { DMZ_STORED = 0, DMZ_LZFSE = 1 };

typedef struct dmz dmz;
// The view size of a Yaz0-literal view of n payload bytes: header, then one control byte (0xff) before every 8 bytes
static inline uint64_t dmz_yaz0_view_size(uint64_t n) { return 16 + n / 8 * 9 + (n % 8 ? 1 + n % 8 : 0); }
// Open the DMZ file on fd (the fd stays the caller's; dmz_close does not close it). NULL with errno 0: not a DMZ file (read it as it is); NULL with errno set: a damaged DMZ file
dmz *dmz_open(int fd);
uint64_t dmz_size(const dmz *z);                                  // the view size: what GetFileSize and directory listings report
ssize_t dmz_pread(dmz *z, void *buf, size_t n, uint64_t off);     // read the view; short only at its end; -1 with errno EIO when a chunk does not decode
void dmz_close(dmz *z);
// The view size of the file at fd whose real size is st_size: the DMZ view size, or st_size for any other file (directory listings)
int64_t dmz_listed_size(int fd, int64_t st_size);

// A bundle: many small files in one payload view (the iOS shader libraries and their reflection, tens of thousands of files that would each cost the install a file).
// Payload: "DMZP", u32 count, u32 names size, u32 0, then per file by name order {u64 payload offset, u64 size, u32 name offset, u32 name length}, the names, then the files
#define DMZ_BUNDLE_MAGIC "DMZP"
#define DMZ_BUNDLE_HEADER 16
#define DMZ_BUNDLE_ENTRY 24
typedef struct dmz_bundle dmz_bundle;
dmz_bundle *dmz_bundle_open(int fd);                                   // NULL with errno set: not a valid bundle (the fd stays the caller's)
void *dmz_bundle_read(dmz_bundle *b, const char *name, size_t *size);  // a malloc'd copy of the named file (one spare byte after it); NULL with errno ENOENT when it is not there, EIO when it does not decode
void dmz_bundle_close(dmz_bundle *b);
