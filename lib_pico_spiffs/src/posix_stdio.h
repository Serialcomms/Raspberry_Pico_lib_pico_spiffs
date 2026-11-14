/*
 * posix_stdio.h - the console side of file descriptors 0, 1 and 2.
 */
#ifndef POSIX_STDIO_H
#define POSIX_STDIO_H

/* Read from stdin; returns the bytes read. */
int posix_stdio_read(char *buffer, int length);

/* Write to stdout or stderr; returns the bytes written. */
int posix_stdio_write(const char *buffer, int length);

#endif
