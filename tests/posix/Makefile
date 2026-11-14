# Tests of pico_spiffs through its POSIX interface, on a PC, with the
# onboard and the external (W25Q) backend over simulated flash chips.
#
#   make          build and run both
#
# The library's POSIX functions would replace the host C library's own
# (open, rename, opendir, ...), so they are renamed with -D for this build.

LIB     = ../../lib_pico_spiffs
CC     ?= gcc
SAN     = -fsanitize=address,undefined -fno-sanitize-recover=undefined
RENAME  = -D_open=pico_open -D_close=pico_close -D_read=pico_read -D_write=pico_write \
          -D_lseek=pico_lseek -D_fstat=pico_fstat -D_stat=pico_stat -D_isatty=pico_isatty \
          -D_unlink=pico_unlink -D_link=pico_link -Drename=pico_rename -Dmkdir=pico_mkdir \
          -Drmdir=pico_rmdir -Dfsync=pico_fsync -Dftruncate=pico_ftruncate \
          -Dtruncate=pico_truncate -Dopendir=pico_opendir -Dreaddir=pico_readdir \
          -Dreaddir_r=pico_readdir_r -Drewinddir=pico_rewinddir -Dtelldir=pico_telldir \
          -Dseekdir=pico_seekdir -Dclosedir=pico_closedir
CFLAGS  = -std=gnu11 -g -O1 $(SAN) -Wall -Wextra -Ihost -I. -I$(LIB)/include \
          -I$(LIB)/spiffs -I$(LIB)/src $(RENAME) -DPICO_SPIFFS_SIZE='(512u*1024u)'
SRCS    = posix_test.c host_sdk.c \
          $(LIB)/spiffs/spiffs_nucleus.c $(LIB)/spiffs/spiffs_gc.c \
          $(LIB)/spiffs/spiffs_hydrogen.c $(LIB)/spiffs/spiffs_cache.c \
          $(LIB)/spiffs/spiffs_check.c \
          $(LIB)/src/pico_spiffs.c $(LIB)/src/pico_spiffs_errors.c \
          $(LIB)/src/pico_spiffs_leds.c $(LIB)/src/pico_spiffs_list.c \
          $(LIB)/src/posix_common.c $(LIB)/src/posix_files.c \
          $(LIB)/src/posix_names.c $(LIB)/src/posix_dirs.c $(LIB)/src/posix_maps.c
DEPS    = $(wildcard $(LIB)/*/*.h $(LIB)/include/sys/*.h host/*.h host/*/*.h *.h)

all: run

posix_onboard: $(SRCS) $(LIB)/src/flash_onboard.c $(DEPS)
	$(CC) $(CFLAGS) -DPICO_SPIFFS_FLASH_EXTERNAL=0 $(SRCS) $(LIB)/src/flash_onboard.c \
	    -Wl,--defsym,__flash_binary_end=host_flash+0x10000 -o $@

posix_external: $(SRCS) $(LIB)/src/flash_external.c $(DEPS)
	$(CC) $(CFLAGS) -DPICO_SPIFFS_FLASH_EXTERNAL=1 -DPICO_SPIFFS_FLASH_OFFSET=0x100000u \
	    $(SRCS) $(LIB)/src/flash_external.c -o $@

run: posix_onboard posix_external
	ASAN_OPTIONS=detect_leaks=0 ./posix_onboard
	ASAN_OPTIONS=detect_leaks=0 ./posix_external

# Large transfers onto a 4 MB volume (not part of 'make run': slower).
XFER_SRCS = $(filter-out posix_test.c,$(SRCS)) xfer_test.c
XFER_VOL  = -UPICO_SPIFFS_SIZE -DPICO_SPIFFS_SIZE='(4096u*1024u)'

xfer_onboard: $(XFER_SRCS) $(LIB)/src/flash_onboard.c $(DEPS)
	$(CC) $(CFLAGS) $(XFER_VOL) -DPICO_FLASH_SIZE_BYTES='(16u*1024u*1024u)' \
	    -DPICO_SPIFFS_FLASH_EXTERNAL=0 $(XFER_SRCS) $(LIB)/src/flash_onboard.c \
	    -Wl,--defsym,__flash_binary_end=host_flash+0x10000 -o $@

xfer_external: $(XFER_SRCS) $(LIB)/src/flash_external.c $(DEPS)
	$(CC) $(CFLAGS) $(XFER_VOL) -DPICO_SPIFFS_FLASH_EXTERNAL=1 -DPICO_SPIFFS_FLASH_OFFSET=0u \
	    $(XFER_SRCS) $(LIB)/src/flash_external.c -o $@

xfer: xfer_onboard xfer_external
	ASAN_OPTIONS=detect_leaks=0 ./xfer_onboard
	ASAN_OPTIONS=detect_leaks=0 ./xfer_external

clean:
	rm -f posix_onboard posix_external xfer_onboard xfer_external

.PHONY: all run xfer clean
