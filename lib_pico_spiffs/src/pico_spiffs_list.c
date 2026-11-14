/*
 * pico_spiffs_list.c - print the files on the volume, with their sizes.
 */
#include <stdio.h>

#include "pico_spiffs.h"

/* "1,234,567": digits in groups of three. */
static void format_with_commas(uint32_t value, char text[16]) {
    char digits[11];
    int count = snprintf(digits, sizeof(digits), "%lu", (unsigned long)value);
    int out = 0;
    for (int i = 0; i < count; i++) {
        if (i > 0 && (count - i) % 3 == 0)
            text[out++] = ',';
        text[out++] = digits[i];
    }
    text[out] = '\0';
}

static void print_entry(const struct spiffs_dirent *e) {
    char size[16];
    format_with_commas(e->size, size);
    printf("  %-*s %13s\n", SPIFFS_OBJ_NAME_LEN, (const char *)e->name, size);
}

static void print_totals(int files) {
    uint32_t total = 0, used = 0;
    char total_text[16], used_text[16];
    pico_spiffs_info(&total, &used);
    format_with_commas(total, total_text);
    format_with_commas(used, used_text);
    printf("  %d file%s, %s of %s bytes used\n", files, files == 1 ? "" : "s", used_text,
           total_text);
}

void pico_spiffs_list(void) {
    spiffs_DIR dir;
    struct spiffs_dirent e;
    int files = 0;
    if (SPIFFS_opendir(pico_spiffs_fs(), "/", &dir) == NULL) {
        printf("  (volume not mounted)\n");
        return;
    }
    while (SPIFFS_readdir(&dir, &e) != NULL) {
        print_entry(&e);
        files++;
    }
    SPIFFS_closedir(&dir);
    print_totals(files);
}
