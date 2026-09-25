/*
 * USB Mass Storage — header
 *
 * Exposes the SD card as a USB mass storage device (Settings → USB File Mode).
 */

#ifndef USB_MSC_H
#define USB_MSC_H

#include <stdbool.h>

/* Initialise TinyUSB and register the MSC class.  Call once. */
void usb_msc_init(void);

/* Process pending USB events.  Call in a tight main loop. */
void usb_msc_task(void);

/* Returns true when a host is actively connected (mounted). */
bool usb_msc_mounted(void);

/* Returns true once the host has ejected the drive. */
bool usb_msc_ejected(void);

#endif /* USB_MSC_H */
