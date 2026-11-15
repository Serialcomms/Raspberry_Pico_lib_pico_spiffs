/*
 * console.h - line input from the USB or UART terminal.
 */
#ifndef CONSOLE_H
#define CONSOLE_H

#include <stddef.h>

/* Prints prompt, then reads one line with echo and backspace. */
void console_read_line(const char *prompt, char *line, size_t size);

#endif
