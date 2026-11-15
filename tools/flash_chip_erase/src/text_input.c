/*
 * text_input.c - parsing of what the user typed at the menu prompts.
 */
#include <ctype.h>
#include <string.h>

#include "text_input.h"

static const char *skip_spaces(const char *text) {
    while (*text == ' ')
        text++;
    return text;
}

static size_t length_without_trailing_spaces(const char *text) {
    size_t length = strlen(text);
    while (length > 0 && text[length - 1] == ' ')
        length--;
    return length;
}

bool text_is(const char *typed, const char *word) {
    typed = skip_spaces(typed);
    size_t length = length_without_trailing_spaces(typed);
    if (length != strlen(word))
        return false;
    for (size_t i = 0; i < length; i++) {
        if (toupper((unsigned char)typed[i]) != toupper((unsigned char)word[i]))
            return false;
    }
    return true;
}

int text_option_index(const char *typed, size_t option_count) {
    typed = skip_spaces(typed);
    if (length_without_trailing_spaces(typed) != 1)
        return -1;
    int index = toupper((unsigned char)typed[0]) - 'A';
    if (index < 0 || (size_t)index >= option_count)
        return -1;
    return index;
}

bool text_is_blank(const char *typed) {
    return *skip_spaces(typed) == '\0';
}
