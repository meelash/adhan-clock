/*
 * USB Mass Storage — SD card block-device callbacks for TinyUSB
 */

#include "drivers/usb_msc.h"
#include "drivers/sdcard.h"
#include "config.h"

#include "tusb.h"
#include "pico/stdlib.h"
#include <string.h>

static volatile bool host_mounted = false;
static volatile bool ejected = false;

/* ── Public API ─────────────────────────────────────────────────────────── */

void usb_msc_init(void) {
    tusb_init();
}

void usb_msc_task(void) {
    tud_task();
}

bool usb_msc_mounted(void) {
    return host_mounted;
}

bool usb_msc_ejected(void) {
    return ejected;
}

/* ── TinyUSB MSC callbacks ──────────────────────────────────────────────── */

/* Called when the device is mounted (host appeared) */
void tud_mount_cb(void) {
    host_mounted = true;
}

/* Called when the device is unmounted */
void tud_umount_cb(void) {
    host_mounted = false;
}

void tud_suspend_cb(bool remote_wakeup_en) {
    (void)remote_wakeup_en;
}

void tud_resume_cb(void) {
}

/* ── MSC class callbacks ────────────────────────────────────────────────── */

/* Invoked when host sends SCSI READ_CAPACITY / READ_FORMAT_CAPACITY */
void tud_msc_capacity_cb(uint8_t lun, uint32_t *block_count,
                          uint16_t *block_size) {
    (void)lun;
    *block_size  = 512;
    *block_count = sdcard_get_sector_count();
}

/* Invoked on SCSI INQUIRY */
void tud_msc_inquiry_cb(uint8_t lun, uint8_t vendor_id[8],
                         uint8_t product_id[16], uint8_t product_rev[4]) {
    (void)lun;
    const char vid[] = "Adhan";
    const char pid[] = "Clock Storage";
    const char rev[] = "1.0";
    memcpy(vendor_id,   vid, strlen(vid));
    memcpy(product_id,  pid, strlen(pid));
    memcpy(product_rev, rev, strlen(rev));
}

/* Invoked on SCSI TEST_UNIT_READY */
bool tud_msc_test_unit_ready_cb(uint8_t lun) {
    if (ejected || !sdcard_is_ready()) {
        tud_msc_set_sense(lun, SCSI_SENSE_NOT_READY, 0x3A, 0x00); /* medium not present */
        return false;
    }
    return true;
}

/* Invoked on SCSI START STOP UNIT — "Eject" on the PC */
bool tud_msc_start_stop_cb(uint8_t lun, uint8_t power_condition, bool start, bool load_eject) {
    (void)lun;
    (void)power_condition;
    if (load_eject && !start) ejected = true;
    return true;
}

/* Read blocks from SD card */
int32_t tud_msc_read10_cb(uint8_t lun, uint32_t lba,
                           uint32_t offset, void *buffer,
                           uint32_t bufsize) {
    (void)offset;   /* always 0: EP buffer is a multiple of 512 */
    uint32_t count = bufsize / 512;
    if (!sdcard_read_blocks(lba, (uint8_t *)buffer, count)) {
        tud_msc_set_sense(lun, SCSI_SENSE_MEDIUM_ERROR, 0x11, 0x00);
        return -1;
    }
    return (int32_t)bufsize;
}

/* Write blocks to SD card */
int32_t tud_msc_write10_cb(uint8_t lun, uint32_t lba,
                            uint32_t offset, uint8_t *buffer,
                            uint32_t bufsize) {
    (void)offset;
    uint32_t count = bufsize / 512;
    if (!sdcard_write_blocks(lba, buffer, count)) {
        tud_msc_set_sense(lun, SCSI_SENSE_MEDIUM_ERROR, 0x03, 0x00);
        return -1;
    }
    return (int32_t)bufsize;
}

/* Invoked on SCSI command not in built-in list */
int32_t tud_msc_scsi_cb(uint8_t lun, uint8_t const scsi_cmd[16],
                         void *buffer, uint16_t bufsize) {
    (void)buffer;
    (void)bufsize;

    switch (scsi_cmd[0]) {
    case SCSI_CMD_PREVENT_ALLOW_MEDIUM_REMOVAL:
        /* Sent by Linux `eject` before START STOP UNIT; rejecting it makes
         * the whole eject fail. Nothing to lock, so just accept. */
        return 0;

    case 0x35: /* SYNCHRONIZE CACHE (10): writes are synchronous already */
        sdcard_sync();
        return 0;

    default:
        tud_msc_set_sense(lun, SCSI_SENSE_ILLEGAL_REQUEST, 0x20, 0x00);
        return -1;
    }
}
