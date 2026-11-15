/*
 * console.c - line input from the USB or UART terminal.
 */
#include <stdbool.h>
#include <stdio.h>

#include "pico/stdlib.h"

#include "console.h"

#define KEY_BACKSPACE 0x08
#define KEY_DELETE    0x7F

static bool is_end_of_line(int c) {
    return c == '\r' || c == '\n';
}

static bool is_backspace(int c) {
    return c == KEY_BACKSPACE || c == KEY_DELETE;
}

static bool is_printable(int c) {
    return c >= ' ' && c < KEY_DELETE;
}

static int wait_for_key(void) {
    int c;
    do {
        c = getchar_timeout_us(100 * 1000);
    } while (c == PICO_ERROR_TIMEOUT);
    return c;
}

void console_read_line(const char *prompt, char *line, size_t size) {
    size_t length = 0;
    printf("%s", prompt);
    stdio_flush();
    for (;;) {
        int c = wait_for_key();
        if (is_end_of_line(c))
            break;
        if (is_backspace(c) && length > 0) {
            length--;
            printf("\b \b");
        } else if (is_printable(c) && length + 1 < size) {
            line[length++] = (char)c;
            putchar(c);
        }
        stdio_flush();
    }
    line[length] = '\0';
    printf("\n");
}
