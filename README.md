# New SPIFFS for Raspberry Pico RP2040/RP2350

The **SPIFFS Flash File System** as a Raspberry Pico SDK library (SDK 2.3.1 or later recommended). 

Updated with multiple fixes and improvements made by AI and re-tested manually on a physical board.

Use with the Pico's onboard flash or an external Winbond W25Q chip on SPI. 

Mount the volume and the C library's usual file functions work on it:

`fopen`, `fread`, `fprintf`, `stat`, `rename`, `remove`, `opendir` and the
rest. 

Existing C programs written for an operating system need no changes.

Also includes many fixes to SPIFFS itself (see [CHANGES.md](CHANGES.md)).

<details>

<summary><b>Quick start</b></summary>

In your project's `CMakeLists.txt`:


```cmake
set(PICO_SPIFFS_FLASH ONBOARD)            # or EXTERNAL
add_subdirectory(path/to/lib_pico_spiffs)
target_link_libraries(my_app pico_stdlib pico_spiffs)
```

For `EXTERNAL`, add
[lib_winbond_w25q](https://github.com/Serialcomms/Raspberry_Pico_lib_winbond_w25q)
first, with `add_subdirectory(.../lib_winbond_w25q)`.

In your program:

```c
#include <stdio.h>
#include "pico/stdlib.h"
#include "pico_spiffs.h"

int main(void) {
    stdio_init_all();
    pico_spiffs_mount(PICO_SPIFFS_FORMAT_IF_BLANK);   /* optional: see below */

    FILE *f = fopen("hello.txt", "w");
    fprintf(f, "Hello, flash\n");
    fclose(f);
}
```

The first file call mounts the volume if the program hasn't, and formats the
flash if it is blank. 

`pico_spiffs_mount()` is only needed to choose what happens otherwise. 

There is no "mount, unmount, format, mount again" sequence:

| Call | Does |
|---|---|
| `pico_spiffs_mount(PICO_SPIFFS_MOUNT_ONLY)` | Mount an existing volume |
| `pico_spiffs_mount(PICO_SPIFFS_FORMAT_IF_BLANK)` | ...or format erased flash first (never erases data) |
| `pico_spiffs_mount(PICO_SPIFFS_FORMAT_IF_NEEDED)` | ...or format whatever is there |
| `pico_spiffs_format()` | Erase the volume and leave it mounted, empty |
| `pico_spiffs_unmount()` | Close every file and unmount |
| `pico_spiffs_info()`, `pico_spiffs_list()`, `pico_spiffs_check()` | Space used, a listing, a consistency check |
| `pico_spiffs_gc(max_blocks)` | Erase blocks holding only deleted pages now, while idle, so later writes don't wait for them |
| `pico_spiffs_spi_baud()` | The SPI clock in use for external flash (0 onboard) |

All return `SPIFFS_OK` (0) or a SPIFFS error code. `pico_spiffs_error_text()`
describes the code and `pico_spiffs_errno()` gives the matching `errno`.

The example program in `example/` builds with the `CMakeLists.txt` at the top
of this repository. The top folder is a Raspberry Pi Pico project: open it in
VS Code with the Raspberry Pi Pico extension (which installs SDK 2.3.1 and its
tools if missing), choose your board with the extension's board switcher (it
starts as `pico2`), then use **Compile Project** and **Run Project**. Or from
the command line:

```sh
cmake -B build -DPICO_BOARD=pico2                     # onboard flash (default)
cmake -B build -DPICO_BOARD=pico2 -DPICO_SPIFFS_FLASH=EXTERNAL
cmake --build build
```

</details>

<details>
<summary><b>Settings</b></summary>

Choose the flash in CMake with `PICO_SPIFFS_FLASH` (`ONBOARD` or `EXTERNAL`).

Everything else is a compile definition with a default, set for example with `target_compile_definitions(my_app PRIVATE PICO_SPIFFS_SIZE=0x400000)`:

| Setting | Default | Meaning |
|---|---|---|
| `PICO_SPIFFS_SIZE` | 1 MB onboard; whole chip external | Volume size in bytes |
| `PICO_SPIFFS_FLASH_OFFSET` | near the end of flash (see RP2350-E10 below); 0 external | Start of the volume in the chip (multiple of 4096; 65536 lets blocks use the fast 64 KB erase) |
| `PICO_SPIFFS_PAGE_SIZE` | 512 | Logical page (see Performance) |
| `PICO_SPIFFS_BLOCK_SIZE` | 65536 | Logical block, a multiple of 4096 |
| `PICO_SPIFFS_MAX_OPEN_FILES` | 8 | Files open at once |
| `PICO_SPIFFS_CACHE_PAGES` | 4 | RAM cache, in pages |
| `PICO_SPIFFS_INDEX_MAPS` | 2 | Index maps for large files (0: none) |
| `PICO_SPIFFS_INDEX_MAP_BYTES` | 8192 | RAM per map: covers about 2 MB of a file |
| `PICO_SPIFFS_INDEX_MAP_MIN_FILE` | 65536 | Files at least this big get a map when opened |
| `PICO_SPIFFS_AUTO_MOUNT` | 2 | First file call: 0 nothing, 1 mount, 2 mount + format blank flash, 3 mount + format anything |
| `PICO_SPIFFS_LED_READ_PIN`, `_WRITE_PIN`, `_ERASE_PIN` | none | GPIOs for activity LEDs |
| `PICO_SPIFFS_EXT_SPI`, `_EXT_PIN_MISO`, `_CS`, `_SCK`, `_MOSI`, `_EXT_BAUD` | `spi0`, 16, 17, 18, 19, 40 MHz | External chip wiring |
| `SPIFFS_OBJ_NAME_LEN` | 32 | Longest name plus its NUL |
| `SPIFFS_OBJIX_CACHE` | 32 | Index pages remembered per open file |

The onboard volume must lie after your program: `pico_spiffs_mount()` returns
`SPIFFS_ERR_BAD_CONFIG` if it would overlap the program. On a Pico Plus 2
(16 MB flash), `PICO_SPIFFS_SIZE=0xC00000` gives 12 MB of files.

**RP2350-E10:** for A2-stepping chips, the SDK adds an extra block to every
UF2, by default at 0x10FFFF00, the last 256 bytes of a 16 MB flash, and loading
the UF2 writes it there. So by default the volume ends at least one 4 KB sector
short of the end of flash, on a 64 KB boundary, and new programs can be loaded
without touching it. If you
set `PICO_SPIFFS_FLASH_OFFSET` yourself, keep the volume clear of that sector,
or move the block with
`set_target_properties(my_app PROPERTIES PICOTOOL_EXTRA_UF2_ARGS "--abs-block;<address>")`.

RAM with the defaults is about 20 KB: 16 KB of index maps, 2 KB of cache, and
1 KB each for the work buffer and the file descriptors.

**Changing the size or geometry** makes the old volume unreadable (by design,
SPIFFS records them in each block). `PICO_SPIFFS_FORMAT_IF_BLANK` and the
automatic mount then refuse to touch it (`ENODEV`) until you call
`pico_spiffs_format()`.

</details>

<details>
<summary><b>The C library interface</b></summary>

pico_spiffs provides the newlib system calls behind the C library's file functions, replacing the SDK's stubs. 

Descriptors 0, 1 and 2 still go to the console (USB or UART, as `pico_stdio` is set up), exactly as before.

| Function | Notes |
|---|---|
| `open`, `close`, `read`, `write`, `lseek` | `lseek` beyond the end is allowed; a write there fills the gap with zeros |
| `fstat`, `stat` | `st_size`, `st_mode` (`S_IFREG`; `S_IFDIR` for `/`; `S_IFCHR` for descriptors 0–2) |
| `unlink` / `remove`, `rename` | `rename` replaces an existing target, as POSIX specifies |
| `ftruncate`, `truncate` | Shorten, or lengthen with zeros |
| `fsync` | Store cached writes |
| `opendir`, `readdir`, `readdir_r`, `rewinddir`, `telldir`, `seekdir`, `closedir` | The one directory, `/` (or `.`) |
| `mkdir`, `rmdir` | `EEXIST` / `EBUSY` for `/`; `ENOSYS` for anything else |
| `isatty`, `link` | `link` fails with `EMLINK`: a file has exactly one name |

The volume is flat: subdirectories are out of scope. Names may contain `/`,
but `/a.txt`, `./a.txt` and `a.txt` all mean the same file. Names are up to 31
characters (`ENAMETOOLONG` beyond).

**`open()` flags**, checked against the [Open Group specification](https://pubs.opengroup.org/onlinepubs/7908799/xsh/open.html)

| Flag                               | Open Group                              | pico_spiffs                                                        |
|------------------------------------|-----------------------------------------|--------------------------------------------------------------------|
| `O_RDONLY`, `O_WRONLY`, `O_RDWR`   | Access mode                             | Mapped; invalid mode: `EINVAL`                                     |
| `O_CREAT`                          | Create if missing                       | Always honoured                                                    |
| `O_EXCL`                           | With `O_CREAT`: fail if the file exists | Honoured with `O_CREAT`; no effect without it (undefined in POSIX) |
| `O_TRUNC`                          | With write access: truncate             | Write access only (undefined with `O_RDONLY`)                      |
| `O_APPEND`                         | Writes at the end                       | Mapped                                                             |
| `O_SYNC`, `O_DSYNC`, `O_RSYNC`     | Synchronous writes                      | Writes bypass the RAM cache                                        |
| `O_NONBLOCK`, `O_NOCTTY`           | No effect on regular files              | Ignored                                                            |
| `O_DIRECTORY`                      | (later POSIX)                           | `ENOTDIR`                                                          |


**Differences from POSIX that remain:**

- Removing an open file closes its descriptors; POSIX keeps them usable until closed.
- `rename` onto an existing file removes the target first, so it isn't atomic.

</details>

<details>
<summary><b>Performance</b></summary>

Measured on a PC with `tests/bench` (the real library over a simulated W25Q64).
Times are estimates from the datasheet's typical figures: 0.4 ms per page
program, 45 ms per 4 KB erase, 40 MHz SPI reads. The test writes two 1.5 MB
files, then makes 2,000 random 512-byte accesses, as an emulator's disk does.

| On an 8 MB volume (37% full) | Upstream SPIFFS, 256-byte pages | pico_spiffs defaults |
|---|---|---|
| Write 2 × 1.5 MB | 167 KB/s | 223 KB/s |
| Read 2 × 1.5 MB | 4.2 MB/s | 4.5 MB/s |
| 2,000 random 512-byte writes | 27 KB/s | 50 KB/s |
| 2,000 random 512-byte reads | 64 KB/s | 2.3 MB/s |

**On the board** (Pico Plus 2, RP2350 at 150 MHz, 8 MB volume, the example's
2 MB file, every byte checked on read-back):

| | Onboard QSPI flash | W25Q64JV on SPI at 37.5 MHz |
|---|---|---|
| Write 2 MB, erased blocks ready | 12.1 s (169 KB/s) | 5.2 s (407 KB/s) |
| Write 2 MB, erasing as it goes | 16.1 s (replacing the file) | 15.5 s (134 KB/s) |
| Remove the 2 MB file | | 1.2 s |
| `pico_spiffs_gc()` | | about 280 ms per 64 KB block |
| Read 2 MB | 0.17 s (12 MB/s) | 0.78 s with checking (2.7 MB/s) |
| `pico_spiffs_check()` | | 2.5 s |
| Mount, existing volume | 9 ms | |
| Mount, formatting blank flash | 0.98 s | |

The W25Q figures use a 10 µs status poll for page programs in the W25Q
library (a 500 µs poll gave 74 KB/s, as SPIFFS makes many small programs).
Onboard writes are slower because code can't run from flash while it is
written.

Where the gains come from:

- **512-byte pages:** fewer page programs per KB, and half-size index maps.
- **`lseek`:** it no longer scans the whole volume.
- **Index-page cache:** each open file remembers where its index pages are.
- **Automatic index maps:** reads go straight to the right page.

**Leave free space for random writes.** SPIFFS never rewrites flash in place,
so garbage collection runs more as the volume fills. With the same files on a
4 MB volume (75% full), random 512-byte writes drop to about 4.5 KB/s. Keeping
large, randomly written files under about half the volume avoids that.
Sequential writes and all reads are not affected.

**Erase ahead when idle.** Removing a file only marks its pages deleted.
Once a volume has been written over a few times, no erased blocks are left
and every new block a write needs costs a block erase first (about
150-300 ms per 64 KB on a W25Q). `pico_spiffs_gc(0)` does those erases
while the program is idle, so the next large write runs at program speed.

Limits: a volume holds at most 65,535 logical pages (32 MB with 512-byte
pages, less one page); a file at most 65,535 (about 33 MB).

**Large File Erase / Garbage Collection.**
Deleting large files is now significantly quicker, this is due to SPIFFS now not running garbage collection (gc) as part of delete.
Users now have control of gc via the `pico_spiffs_gc(max_blocks)` function.

</details>

<details>
<summary><b>Receiving files over a serial line (XMODEM, ZMODEM)</b></summary>

A flash write blocks the Pico for milliseconds: longer than the UART's
32-byte FIFO lasts (about 3 ms at 115200 baud), so data that arrives during
a write is lost. A file received over a serial line can then come out
corrupt or short. The receiver must make the sender wait after each block
until the block is written:

- **ZMODEM:** in ZRINIT, set `ZF0_CANFDX`, leave out `ZF0_CANOVIO`, and give
  a 1 KB buffer size in ZP0/ZP1; send `ZACK` only after the block is written
  and flushed. With lrzsz, also give the sender a window:
  `sz -vv --zmodem -b -w 1024 file`. Without `-w`, `sz` streams `ZCRCG`
  subpackets that the Pico cannot keep up with. The companion library
  lib_pico_zmodem does all this.
- **XMODEM:** it already waits for an ACK after each packet; send the ACK
  after the write.

Check every `write()` (or `fwrite()`) and `close()`, and cancel the transfer
if one fails: a full volume is reported there. A file received under a new
name needs room beside the old copy, which a 4 MB volume (about 3.5 MB of
files) may not have.

</details>

<details>
<summary><b>Both cores, interrupts and power loss</b></summary>

- **Both cores:** every SPIFFS call holds a recursive mutex, so both cores may
  use files.
- **Onboard flash writes:** these run inside the SDK's `flash_safe_execute()`,
  which pauses the other core and interrupts. If core 1 runs code from flash,
  call `flash_safe_execute_core_init()` on core 1 at start-up (or use
  `multicore_lockout_victim_init()`), as the SDK requires.
- **External flash:** this has no such restriction, but its SPI bus belongs to
  pico_spiffs.
- **Power loss:** SPIFFS is designed to survive it, and `pico_spiffs_check()`
  repairs what an interrupted operation leaves. The tests here cover full
  volumes and failed operations, not power cuts on real hardware.

</details>

<details>
<summary><b>Erasing a whole flash chip (flash_chip_erase)</b></summary>

[`tools/flash_chip_erase/`](tools/flash_chip_erase/) is a companion utility
for RP2350 boards that wipes an entire flash chip, for example to start
again with a blank volume.

- **Finds the flash:** it scans QSPI chip selects 0 and 1 (the board's own
  flash, and a second chip such as the Pico Plus 2's) and SPI0 and SPI1 for
  Winbond W25Q chips, and shows each one's device, JEDEC and unique IDs.
- **Asks before erasing:** you choose a chip, then type the last 4 digits of
  its unique ID to start. Write-protected chips and other devices, such as
  PSRAM, are listed but never offered for erase.
- **Shows progress:** a progress bar fills while the chip erases and the LED
  pulses; afterwards it checks that the chip reads back blank.
- **Runs from RAM:** loading its UF2 writes nothing to flash, so it can erase
  the board's own flash too.

> [!CAUTION]
> Erasing QSPI channel 0 wipes the board's own flash: its program and any
> SPIFFS volume. The board starts in BOOTSEL mode afterwards.

It is a separate build from the library; see its
[README](tools/flash_chip_erase/README.md) for building and wiring.

</details>

<details>
<summary><b>Layout</b></summary>

| Folder | What's there |
|---|---|
| `lib_pico_spiffs/` | The library: `CMakeLists.txt`, `include/` (`pico_spiffs.h`, `spiffs_config.h`, `sys/dirent.h`), `src/` (volume, backends, POSIX layer), `spiffs/` (the SPIFFS core) |
| `lib_winbond_w25q/` | Copy of [lib_winbond_w25q](https://github.com/Serialcomms/Raspberry_Pico_lib_winbond_w25q), the external flash driver used when `PICO_SPIFFS_FLASH` is `EXTERNAL` |
| `example/` | An example program |
| `tools/flash_chip_erase/` | RP2350 utility that finds Winbond flash on QSPI CS0/CS1, SPI0 and SPI1 and performs a full chip erase; a separate RAM-only build (see its README) |
| `tests/upstream/` | Upstream SPIFFS's own test suite, run against this core |
| `tests/fuzz/` | Model-based fuzzer of the core |
| `tests/posix/` | Tests of the C library interface with both backends, over simulated chips that check the SDK's rules |
| `tests/bench/` | The performance measurements above |
| `tests/common/` | A simulated NOR flash chip |

</details>

<details>
<summary><b>Testing</b></summary>

Everything runs on a Linux PC with gcc, under AddressSanitizer and
UndefinedBehaviorSanitizer:

```sh
make -C tests/upstream      # upstream's 91 tests
make -C tests/fuzz          # 200 seeds x 2,000 operations (SEEDS=, OPS= for more)
make -C tests/posix         # the C library interface, onboard and W25Q backends
tests/bench/bench.sh        # performance
```

The fuzzer runs random sequences against SPIFFS and an in-RAM model of the
files, comparing every result and every byte read. The operations are opens
with random flags, writes, reads, seeks, truncation, renames, removals,
`stat`, listings, remounts, checks, garbage collection and index maps. It runs
until the volume is full, and checks the model allows for the partial results
a full volume leaves. The simulated chip reports any attempt to program a 0
bit back to 1. It found most of the bugs listed in [CHANGES.md](CHANGES.md);
the final code passes 55,000 seeds of 2,000 operations and 400 of 20,000.

**On the board,** the example writes a large file whose every byte depends on
its offset, reads it back, and reports the number of bytes that differ and the
first bad offset; then it runs `pico_spiffs_check()`. For external flash it
first prints the SPI clock the divider actually gives
(`pico_spiffs_spi_baud()`), which can be below `PICO_SPIFFS_EXT_BAUD`. To test
the wiring's margin, run it at a few clocks and use one comfortably below the
first that shows mismatches. At a 150 MHz system clock the divider can give
75, 37.5, 25 or 18.75 MHz (and slower), so requests of 20, 25 and 40 MHz give
18.75, 25 and 37.5 MHz; a request of 50 MHz also gives 37.5 MHz.

The POSIX tests run the real onboard and W25Q backends against stand-ins for
the SDK, which enforce the rules of the real functions:

- `flash_range_program` takes whole, aligned 256-byte pages;
- erases are 4 KB aligned;
- W25Q page programs can't cross a page.

</details>

<details>
<summary><b>Licence</b></summary>

MIT, as SPIFFS: see [LICENSE](LICENSE). 

SPIFFS is Copyright (c) 2013-2017
Peter Andersson; the changes in `lib_pico_spiffs/spiffs/` are listed in
[CHANGES.md](CHANGES.md).

</details>
