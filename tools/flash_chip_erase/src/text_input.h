/*
 * text_input.h - parsing of what the user typed at the menu prompts.
 *
 * Nothing here touches hardware, so it can be tested on a PC.
 */
#ifndef TEXT_INPUT_H
#define TEXT_INPUT_H

#include <stdbool.h>
#include <stddef.h>

/* Case-insensitive comparison, ignoring leading and trailing spaces. */
bool text_is(const char *typed, const char *word);

/* Index of a single option letter (A = 0) below option_count, or -1. */
int text_option_index(const char *typed, size_t option_count);

/* True if nothing but spaces was typed. */
bool text_is_blank(const char *typed);

#endif
