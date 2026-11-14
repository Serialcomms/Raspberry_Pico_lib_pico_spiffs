/*
 * posix_stdio.c - stdin, stdout and stderr, exactly as the SDK's own
 * newlib interface handles them (pico_clib_interface), for the file
 * descriptors pico_spiffs leaves to the console.
 */
#include "pico/stdio.h"
#include "pico/time.h"

#include "posix_stdio.h"

int posix_stdio_read(char *buffer, int length) {
#if LIB_PICO_STDIO
    return stdio_get_until(buffer, length, at_the_end_of_time);
#else
    (void)buffer;
    (void)length;
    return -1;
#endif
}

int posix_stdio_write(const char *buffer, int length) {
#if LIB_PICO_STDIO
    stdio_put_string(buffer, length, false, true);
    return length;
#else
    (void)buffer;
    return length;
#endif
}
