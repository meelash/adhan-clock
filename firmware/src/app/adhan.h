/*
 * Adhan File Manager
 *
 * Scans the /adhan directory on SD for WAV files, manages selection,
 * triggers playback at prayer times.
 */

#ifndef APP_ADHAN_H
#define APP_ADHAN_H

#include <stdint.h>
#include <stdbool.h>
#include "config.h"
#include "drivers/audio_i2s.h"

#define ADHAN_MAX_NAME_LEN 64   /* incl. NUL; longer names are skipped */

/* Scan the SD card's adhan directory and populate the file list.
 * Returns the number of files found. */
int adhan_scan_files(void);

/* Get the count of discovered adhan files */
int adhan_get_count(void);

/* Get the filename at a given index (0-based) */
const char *adhan_get_filename(int index);

/* Get all filenames as a const char** array (for UI) */
const char **adhan_get_filenames(void);

/* Play the adhan audio file at the given index. */
audio_error_t adhan_play(int index);

/* Play the adhan by filename (basename, e.g. "makkah.wav") */
audio_error_t adhan_play_by_name(const char *name);

/* Stop adhan playback */
void adhan_stop(void);

/* Returns true if adhan is currently playing */
bool adhan_is_playing(void);

/* Find the index of a filename (case-insensitive), or -1 if not found */
int adhan_find_file(const char *name);

#endif /* APP_ADHAN_H */
