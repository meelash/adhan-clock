/*
 * SD Card Driver — SPI interface + FatFS disk I/O glue
 *
 * Implements the FatFS diskio interface over bit-banged SPI (pins in
 * config.h). Also provides high-level mount/unmount.
 */

#ifndef DRIVER_SDCARD_H
#define DRIVER_SDCARD_H

#include <stdbool.h>
#include <stdint.h>
#include "ff.h"

/* Low-level SD init diagnostic codes. */
#define SD_ERR_NONE             0u
#define SD_ERR_NO_RESPONSE      1u  /* MISO never answered: no card / wiring */
#define SD_ERR_CMD0             2u  /* answered, but not with 'idle' */
#define SD_ERR_CMD8             3u
#define SD_ERR_ACMD41           4u  /* card never finished initialising */
#define SD_ERR_CS_STUCK         5u  /* CS pin reads low while driven high */
#define SD_ERR_NO_FAT           6u  /* card OK, but no FAT16/FAT32 volume */
#define SD_ERR_READ             7u  /* initialised, but can't read even slowly */

/* Initialise SPI pins + card and mount the FAT filesystem. */
bool sdcard_init(void);

/* Initialise SPI pins + card only (no filesystem) — for USB MSC mode. */
bool sdcard_init_raw(void);

/* Returns true if an SD card is mounted and accessible. */
bool sdcard_is_mounted(void);

/* Unmount the filesystem (call before removing card or entering MSC mode). */
void sdcard_unmount(void);

/* Re-mount after unmount. */
bool sdcard_mount(void);

/* Get the FatFS filesystem object (for direct FatFS calls). */
FATFS *sdcard_get_fs(void);

/* ── Low-level block access (used by USB MSC) ────────────────────────── */

/* Total number of 512-byte sectors on the card. */
uint32_t sdcard_get_sector_count(void);

/* Returns true if the SD card is initialised and ready for I/O. */
bool sdcard_is_ready(void);

/* Read `count` 512-byte sectors starting at `lba` into `buffer`. */
bool sdcard_read_blocks(uint32_t lba, uint8_t *buffer, uint32_t count);

/* Write `count` 512-byte sectors starting at `lba` from `buffer`. */
bool sdcard_write_blocks(uint32_t lba, const uint8_t *buffer, uint32_t count);

/* Wait for the card to finish any internal write. */
bool sdcard_sync(void);

/* Last low-level init error code (SD_ERR_* constants). */
uint8_t sdcard_get_last_error(void);
const char *sdcard_error_str(uint8_t err);

/* SPI clock chosen by the startup speed probe, measured (0 = no card). */
uint32_t sdcard_get_spi_khz(void);

/* Failed read/write attempts (CRC error, rejected command, timeout) that
 * were retried, since card init. */
uint32_t sdcard_get_retries(void);

#endif /* DRIVER_SDCARD_H */
