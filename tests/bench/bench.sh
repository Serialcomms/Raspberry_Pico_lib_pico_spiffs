#!/bin/sh
# bench.sh - run bench.c for each page size, block size and cache size.
set -e
cd "$(dirname "$0")"
LIB=../../lib_pico_spiffs
POSIX=../posix
RENAME="-D_open=pico_open -D_close=pico_close -D_read=pico_read -D_write=pico_write \
  -D_lseek=pico_lseek -D_fstat=pico_fstat -D_stat=pico_stat -D_isatty=pico_isatty \
  -D_unlink=pico_unlink -D_link=pico_link -Drename=pico_rename -Dmkdir=pico_mkdir \
  -Drmdir=pico_rmdir -Dfsync=pico_fsync -Dftruncate=pico_ftruncate -Dtruncate=pico_truncate \
  -Dopendir=pico_opendir -Dreaddir=pico_readdir -Dreaddir_r=pico_readdir_r \
  -Drewinddir=pico_rewinddir -Dtelldir=pico_telldir -Dseekdir=pico_seekdir \
  -Dclosedir=pico_closedir"
SRCS="bench.c $POSIX/host_sdk.c $LIB/spiffs/*.c $LIB/src/pico_spiffs.c \
  $LIB/src/pico_spiffs_errors.c $LIB/src/pico_spiffs_leds.c $LIB/src/pico_spiffs_list.c \
  $LIB/src/posix_common.c $LIB/src/posix_files.c $LIB/src/posix_names.c \
  $LIB/src/posix_dirs.c $LIB/src/posix_maps.c $LIB/src/flash_external.c"
for page in ${PAGES:-256 512 1024}; do
  for block in ${BLOCKS:-32768 65536}; do
    for cache in ${CACHES:-8}; do
      gcc -std=gnu11 -O2 -w -I$POSIX/host -I$POSIX -I$LIB/include -I$LIB/spiffs -I$LIB/src \
        $RENAME -DPICO_SPIFFS_FLASH_EXTERNAL=1 -DPICO_SPIFFS_SIZE="${SIZE:-(4u*1024u*1024u)}" \
        -DPICO_SPIFFS_PAGE_SIZE=${page}u -DPICO_SPIFFS_BLOCK_SIZE=${block}u \
        -DPICO_SPIFFS_CACHE_PAGES=$cache $EXTRA $SRCS -o /tmp/bench_run
      /tmp/bench_run || true
    done
  done
done
