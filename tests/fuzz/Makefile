# Model-based fuzzer for the SPIFFS core, built with the library's own
# spiffs_config.h (lib_pico_spiffs/include) and ASan + UBSan.
#
#   make                     build, then run 200 seeds x 2000 operations
#   make run SEEDS=5000      more seeds
#   ./fuzz <seed> 1 <ops>    replay one seed

CORE    = ../../lib_pico_spiffs/spiffs
CONFIG  = ../../lib_pico_spiffs/include
COMMON  = ../common
CC     ?= gcc
SAN     = -fsanitize=address,undefined -fno-sanitize-recover=undefined
CFLAGS  = -std=gnu11 -g -O1 $(SAN) -Wall -Wextra -fno-strict-aliasing \
          -I$(CONFIG) -I$(CORE) -I$(COMMON)
SRCS    = fuzz.c $(COMMON)/nor_flash.c $(CORE)/spiffs_nucleus.c $(CORE)/spiffs_gc.c \
          $(CORE)/spiffs_hydrogen.c $(CORE)/spiffs_cache.c $(CORE)/spiffs_check.c
SEEDS  ?= 200
OPS    ?= 2000

all: run

fuzz: $(SRCS) $(wildcard $(CORE)/*.h $(CONFIG)/*.h $(COMMON)/*.h)
	$(CC) $(CFLAGS) $(SRCS) -o $@

run: fuzz
	ASAN_OPTIONS=detect_leaks=0 ./fuzz 1 $(SEEDS) $(OPS)

clean:
	rm -f fuzz

.PHONY: all run clean
