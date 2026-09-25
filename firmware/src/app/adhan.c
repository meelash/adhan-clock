/*
 * Adhan File Manager — implementation
 */

#include "app/adhan.h"
#include "drivers/audio_i2s.h"
#include "ff.h"
#include <string.h>
#include <stdio.h>
#include <strings.h>

static char file_names[MAX_ADHAN_FILES][ADHAN_MAX_NAME_LEN];
static const char *file_ptrs[MAX_ADHAN_FILES];
static int file_count = 0;

int adhan_scan_files(void) {
    DIR dir;
    FILINFO fno;
    file_count = 0;

    /* Create the adhan directory if it doesn't exist */
    f_mkdir(ADHAN_DIR);

    if (f_opendir(&dir, ADHAN_DIR) != FR_OK)
        return 0;

    while (file_count < MAX_ADHAN_FILES) {
        if (f_readdir(&dir, &fno) != FR_OK || fno.fname[0] == '\0')
            break;

        /* Skip directories and hidden files */
        if (fno.fattrib & AM_DIR) continue;
        if (fno.fname[0] == '.') continue;

        /* Only accept .wav files */
        const char *ext = strrchr(fno.fname, '.');
        if (!ext) continue;
        if (!(ext[1] == 'w' || ext[1] == 'W') ||
            !(ext[2] == 'a' || ext[2] == 'A') ||
            !(ext[3] == 'v' || ext[3] == 'V') ||
            ext[4] != '\0') continue;

        /* Names that don't fit would be truncated and then fail to open */
        if (strlen(fno.fname) >= ADHAN_MAX_NAME_LEN) continue;
        strcpy(file_names[file_count], fno.fname);
        file_ptrs[file_count] = file_names[file_count];
        file_count++;
    }

    f_closedir(&dir);
    return file_count;
}

int adhan_get_count(void) { return file_count; }

const char *adhan_get_filename(int index) {
    if (index < 0 || index >= file_count) return NULL;
    return file_names[index];
}

const char **adhan_get_filenames(void) {
    return (const char **)file_ptrs;
}

audio_error_t adhan_play(int index) {
    const char *name = adhan_get_filename(index);
    if (!name) return AUDIO_ERR_OPEN;
    return adhan_play_by_name(name);
}

audio_error_t adhan_play_by_name(const char *name) {
    char path[sizeof(ADHAN_DIR) + 72];
    snprintf(path, sizeof(path), "%s/%s", ADHAN_DIR, name);
    return audio_play_wav(path);
}

void adhan_stop(void) {
    audio_stop();
}

bool adhan_is_playing(void) {
    return audio_is_playing();
}

int adhan_find_file(const char *name) {
    for (int i = 0; i < file_count; i++) {
        if (strcasecmp(file_names[i], name) == 0)
            return i;
    }
    return -1;
}
