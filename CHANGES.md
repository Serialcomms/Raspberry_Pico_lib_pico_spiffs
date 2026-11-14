# Changes from upstream SPIFFS

pico_spiffs starts from [pellepl/spiffs](https://github.com/pellepl/spiffs) at
commit 3a4cf8b (0.3.7 plus later fixes, including #307 and #308). The SPIFFS
core is in `lib_pico_spiffs/spiffs/`. Every change below was found by, or checked
with, the tests in `tests/`: upstream's own suite (91 tests, all passing), a
model-based fuzzer, and the POSIX tests. Each change has a comment in the code
saying why.

Unless marked *(new)* or *(speed)*, each item fixes a bug that is also in
upstream SPIFFS.

## Data loss and corruption

1. **A zero-length write could destroy a file.** `SPIFFS_write(fd, buf, 0)` at
   an offset inside the file called `spiffs_object_modify` with nothing to
   write. Its loop never loaded the file's index, but its last step still
   rewrote the index header from the work buffer, which held whatever was loaded
   last: often another file's header. The file's real header was then deleted.
   Now a zero-length write returns 0 without touching anything, as POSIX
   specifies, and `modify`/`append` return early for a length of 0.

2. **Overwriting part of a file on a full volume could leave it pointing at
   deleted pages.** `modify` deleted each old data page *before* storing the
   index page that referred to the new one. If storing the index failed (the
   volume full), the file still referred to the deleted pages. Old pages are now
   deleted only after the index has been stored. If storing fails, the new pages
   are deleted instead, and the file keeps its old data.

3. **Truncating a file on a full volume could do the same.** `truncate`
   (`O_TRUNC`, `ftruncate`) deleted the data pages first and stored the shorter
   size afterwards. Large files need several index updates, so the risk grew
   with file size. Now the new size is stored first and the pages it no longer
   covers are deleted afterwards. A long truncation also runs garbage collection
   between index pages. An error part-way through is now reported; before, the
   final index update overwrote it. (Removing a whole file keeps upstream's
   order: the file is marked deleted first, so it is never seen half-removed.)

4. **Garbage collection could replace a file's data with a stray page.** When
   moving a block's data pages, `gc_clean` pointed the file's index at any live
   page with the right file and position, without checking that the index
   referred to it. A page left behind by a failed write could then replace the
   real one. Pages the index does not refer to are now deleted as orphans.

5. **Running out of space half-way through an operation.** SPIFFS keeps two
   free blocks for garbage collection, but the page allocator took the first
   free page after its cursor, often in one of those reserved blocks. The next
   allocation then failed, even though `spiffs_gc_check` had confirmed that
   free pages existed. When the reserve is at stake, the allocator now takes
   free pages in blocks already in use first. With this, operations that pass
   the free-space check no longer fail part-way through.

6. **Cached writes could vanish without an error.** `read`, `lseek`, `fstat`
   and `ftruncate` flushed the write cache and ignored the result, so on a full
   volume data that `write` had accepted was dropped silently. The error is now
   returned by the call that found it. `SPIFFS_fflush` also returned a byte
   count instead of `SPIFFS_OK`.

7. **Cached data was invisible to other handles.** A new file's only data could
   still be in the write cache: `read` checked for an empty file *before*
   flushing, and `stat`, `readdir` and `open` read sizes from flash without
   flushing. All of them now store cached writes first, so every handle sees the
   same file, as POSIX requires.

8. **A cache buffer smaller than the cache header overwrote memory.** The
   number of cache pages was computed as `(size - sizeof(header)) / page` in
   unsigned arithmetic, so a small (or zero-size) buffer gave millions of pages
   and a huge `memset`. The count can no longer wrap, it is capped at 32 (the
   width of the page bitmap), and `SPIFFS_mount` rejects a buffer too small for
   the header. The alignment step could also wrap a tiny buffer size; it no
   longer can.

9. **Creating a file wrote uninitialised stack bytes to flash.** The alignment
   and padding bytes of a new file's header were never set. Combined with flash
   AND-ing, the result depended on what happened to be on the stack. The header
   now starts as 0xFF.

10. **Writes to a full volume wore the flash for nothing.** Every write that
    found the volume full ran garbage collection again. When nothing could be
    reclaimed, each attempt still erased blocks: about 17 erases and 0.7 s per
    failed 4 KB write on a 1 MB volume. A program that kept writing after the
    volume filled spent minutes erasing. When collection fails, SPIFFS now
    remembers how much was reclaimable. Requests as large fail at once, without
    erasing, until a removal or truncation frees more. (Found on a Pico 2: a
    2 MB write to a 1 MB volume took 283 s.)

## Index maps (`SPIFFS_IX_MAP`)

11. **A removed file's map passed to the next file.** Removing an open file
    released its descriptors directly, without clearing their index map. The
    next file opened on that descriptor read through the old file's map.
12. **`SPIFFS_ix_remap` cleared the wrong memory.** Moving a map past its own
    length did `memset(&map->map_buf, ...)`, which clears the pointer field and
    whatever follows it, not the buffer. Moving it backwards by more than its
    length overran the buffer.
13. **`spiffs_populate_ix_map` clamped with `MAX`** instead of `MIN`, so a range
    past the end of the map was not limited.
14. **Truncation left stale map entries.** Updating a map stopped at the first
    free index entry. Entries beyond the new end kept pointing at deleted pages,
    and were used once the file grew again through another handle. Free entries
    now clear the map entry.

## Undefined behaviour and robustness

15. **Misaligned 16-bit loads and stores.** The index tables start after a page
    header whose size is not always even, so with the default configuration
    `spiffs_page_ix` entries were read and written misaligned. That is undefined
    behaviour, reported by UBSan. All 24 places now use small `memcpy` accessors
    (`SPIFFS_OBJ_HDR_IX_GET/SET`, `SPIFFS_OBJ_IX_GET/SET`).
16. **Configurations SPIFFS cannot use are rejected** *(new)*. `SPIFFS_mount`
    checks the geometry, the volume size against the 16-bit page and block
    indexes, and the alignment of the work buffer, and returns
    `SPIFFS_ERR_BAD_CONFIG`. Before, it could divide by zero or wrap an index
    without warning. For example, a 16 MB chip with 256-byte pages is 65,536
    pages, one too many.
17. **Seek offsets are computed in 64 bits**, so a large `SEEK_CUR` or
    `SEEK_END` offset cannot overflow.
18. **The SPIFFS core is compiled without type-based alias analysis**
    (`#pragma GCC optimize ("no-strict-aliasing")` in each core file): it reads
    its byte buffers as 16- and 32-bit tables.

## POSIX behaviour *(new)*

19. `SPIFFS_POSIX_SEEK` (on in pico_spiffs): `lseek` may move beyond the end of
    a file. Reading there gives end of file, and writing there first fills the
    gap with zeros.
20. When one handle truncates a file, the others keep their offsets. They used
    to be moved back to the new end; the next write fills the gap with zeros.
21. An `O_APPEND` handle's offset follows its write, even when another handle
    has made the file shorter.

## Speed for large files *(speed)*

22. `SPIFFS_lseek` no longer looks up the index page for the new offset. That
    meant scanning every block's lookup pages on each seek, and the following
    read or write does the lookup anyway if it needs to. With an index map it
    never needs to.
23. `SPIFFS_OBJIX_CACHE`: each open file remembers where its first 32 index
    pages are, and follows them when they move. Each remembered location is
    checked against the page header before use. Random access in a multi-MB
    file then rarely needs to scan the volume.

## Erasing

24. **Erase failures were ignored.** `spiffs_erase_block` discarded the HAL's
    result, so SPIFFS went on using a block that might not be erased. The
    error is now returned. The block is also erased in one HAL call, not one
    per 4 KB, so the HAL can use a faster large-block erase.

## Closing

25. **A close that failed on a full volume leaked its handle.** If `close`
    could not store the file's last cached write, it returned the error before
    giving the handle back, so each such close used up one of the open-file
    slots for good. The handle is now always released and the error still
    returned. Found by `tests/posix/xfer_test.c` (`make xfer`), which also
    writes 2 to 3.5 MB files in 128-byte to 8 KB pieces onto a 4 MB volume,
    as an XMODEM or ZMODEM receiver would.

## Configuration *(new)*

- `SPIFFS_NO_BLIND_WRITES=1` in pico_spiffs: flag bytes are read before bits
  are cleared, so every write states its intended value. The host tests then
  check that nothing ever tries to program a 0 bit back to 1.
- The debug macros (`SPIFFS_DBG` and so on) and their `_SPIPRI*` formats can be
  defined by the application.
- New error code `SPIFFS_ERR_BAD_CONFIG` (-10041).

## Pico layer

- Erasing skips 4 KB sectors that are already blank and erases the rest in
  runs. A fresh chip then formats almost at once, and aligned 64 KB runs use
  the flash's block erase.

- The default onboard volume ends at least one 4 KB sector short of the end of
  flash, and starts on a 64 KB boundary.
  For RP2350-A2 chips the SDK adds an RP2350-E10 workaround block to every UF2,
  at 0x10FFFF00, and loading the UF2 writes it there: inside a volume at the end
  of a 16 MB flash, which each new program would damage.
