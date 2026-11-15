# flash_chip_erase

A small RP2350 utility that finds Winbond W25Q flash on any of four channels
and performs a full chip erase on the one you choose.

| Channel | Where | Default wiring |
|---|---|---|
| QSPI channel 0 | The board's own program flash | (built in) |
| QSPI channel 1 | Second QSPI chip select | GPIO 47 on Pimoroni Pico Plus 2 (its PSRAM); not scanned on boards without one |
| SPI channel 0 | SPI0 | MISO 16, CS 17, SCK 18, MOSI 19 (same as lib_winbond_w25q) |
| SPI channel 1 | SPI1 | MISO 12, CS 13, SCK 14, MOSI 15 |

The program runs entirely from RAM (a `no_flash` binary), so loading the
UF2 writes nothing to flash, and the onboard flash can be erased as well.

> [!CAUTION]
> Erasing QSPI channel 0 wipes the board's own flash: its program and any
> SPIFFS volume. The board will start in BOOTSEL mode afterwards.

## Use

Copy the UF2 to the board in BOOTSEL mode and open a terminal (USB serial,
or UART on GPIO 0/1). Press Enter if the menu has already scrolled past.

1. `S` scans all four channels and lists each flash found, with its device,
   JEDEC and unique IDs. Each erasable chip gets an option letter.
2. Choose the letter, then type the last 4 digits of that chip's unique ID
   to start (or just press Enter to cancel and rescan).
3. A progress bar is redrawn in place while the chip erases:

   ```
   ERASING FLASH [**************....................................]  14 s
   ```

   A chip erase reports no progress of its own, so the stars fill against
   the datasheet's typical erase time (about 2.5 s per MB). If the chip
   takes longer, the bar waits one star short of full until it finishes.
   The LED pulses while erasing and stays on once the erase completes and
   the chip reads back blank.

`BOOT` at the first prompt reboots to BOOTSEL.

Only Winbond chips (JEDEC manufacturer EFh) are offered for erase. A chip
with block protection set is listed but not offered, and other devices on a
channel (such as PSRAM) are only sent ID reads. The unique ID is checked
again just before erasing, in case the chip was changed after the scan.

## Build

This is a separate build from the library and example. From this folder:

```sh
cmake -B build -DPICO_BOARD=pico2
cmake -B build -DPICO_BOARD=pimoroni_pico_plus2_rp2350
cmake --build build
```

This also writes a copy named with the board and version, such as
`flash_chip_erase_pico2_v2.uf2`. Wiring can be changed with
`-DFLASH_ERASE_QSPI_CS1_PIN=...`, `-DFLASH_ERASE_SPI0_PIN_MISO=...` and the
rest (see `src/flash_port.c`).

## Tests on a PC

```sh
cd tests
cc -std=c11 -Wall -Wextra -I../src host_test.c ../src/flash_id.c ../src/text_input.c -o host_test && ./host_test

cd sim     # the real menu and erase code against simulated W25Q chips
cc -std=gnu11 -Wall -Wextra -I. -I../../src '-DFLASH_ERASE_VERSION="1"' sim.c ../../src/main.c \
   ../../src/console.c ../../src/flash_chip.c ../../src/flash_erase.c ../../src/flash_id.c \
   ../../src/text_input.c -o sim
printf 'S\nA\n7788\n' | ./sim
```
