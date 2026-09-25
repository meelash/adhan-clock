/*
 * TinyUSB configuration for USB Mass Storage (expose SD card)
 */

#ifndef TUSB_CONFIG_H
#define TUSB_CONFIG_H

#ifdef __cplusplus
extern "C" {
#endif

/* ── Board ────────────────────────────────────────────────────────────── */
#define CFG_TUSB_MCU          OPT_MCU_RP2040
#define CFG_TUSB_RHPORT0_MODE OPT_MODE_DEVICE
#ifndef CFG_TUSB_OS
#define CFG_TUSB_OS           OPT_OS_NONE
#endif
#define CFG_TUSB_MEM_SECTION
#define CFG_TUSB_MEM_ALIGN    __attribute__((aligned(4)))

/* ── Device ───────────────────────────────────────────────────────────── */
#define CFG_TUD_ENDPOINT0_SIZE  64
#define CFG_TUD_MSC             1
#define CFG_TUD_CDC             0
#define CFG_TUD_HID             0
#define CFG_TUD_MIDI            0
#define CFG_TUD_VENDOR          0

/* ── MSC ──────────────────────────────────────────────────────────────── */
#define CFG_TUD_MSC_EP_BUFSIZE  4096   /* multi-block SD transfers */

#ifdef __cplusplus
}
#endif

#endif /* TUSB_CONFIG_H */
