# Upstream SPIFFS test suite, run against this repository's core.
#
#   make            build and run all tests (ASan + UBSan)
#   make NOSAN=1    without sanitizers (faster)

CORE    = ../../lib_pico_spiffs/spiffs
CC     ?= gcc
WARN    = -Wall -Wextra -Wshadow -Wstrict-prototypes -Wmissing-prototypes \
          -Wpointer-arith -Wcast-qual -Wwrite-strings
SAN     = -fsanitize=address,undefined -fno-sanitize-recover=undefined
ifeq ($(NOSAN),1)
SAN     =
endif
CFLAGS  = -std=c11 -D_DEFAULT_SOURCE -D_SPIFFS_TEST -g -O1 $(SAN) \
          -I. -I$(CORE)
CORE_SRCS = $(CORE)/spiffs_nucleus.c $(CORE)/spiffs_gc.c \
            $(CORE)/spiffs_hydrogen.c $(CORE)/spiffs_cache.c \
            $(CORE)/spiffs_check.c
TEST_SRCS = main.c test_spiffs.c test_dev.c test_check.c test_hydrogen.c \
            test_bugreports.c testsuites.c testrunner.c

all: run

upstream_test: $(CORE_SRCS) $(TEST_SRCS) $(wildcard $(CORE)/*.h) *.h
	$(CC) $(CFLAGS) $(CORE_SRCS) $(TEST_SRCS) -o $@ 2> warnings.txt || \
	    { cat warnings.txt; exit 1; }
	@grep -c "warning:" warnings.txt | sed 's/^/core+test warnings: /' || true

run: upstream_test
	@mkdir -p /tmp/spiffs_upstream && cd /tmp/spiffs_upstream && \
	    ASAN_OPTIONS=detect_leaks=0 $(CURDIR)/upstream_test > $(CURDIR)/last_run.txt 2>&1; \
	    tail -6 $(CURDIR)/last_run.txt

clean:
	rm -f upstream_test warnings.txt last_run.txt

.PHONY: all run clean
