/*
 * IR Remote Input Driver — NEC protocol, onboard receiver on GP28
 *
 * The Waveshare carrier board hardwires an IR receiver to GP28.
 * This driver decodes NEC frames using GPIO interrupts and injects
 * decoded keys into the button event queue via buttons_inject().
 *
 * Key codes are defined in config.h (IR_KEY_UP, IR_KEY_DOWN,
 * IR_KEY_OK, IR_KEY_BACK, IR_REMOTE_ADDR).
 */

#ifndef DRIVER_IR_REMOTE_H
#define DRIVER_IR_REMOTE_H

#include <stdint.h>

void ir_remote_init(void);
void ir_remote_poll(void);  /* call each main-loop iteration */

/* Command byte of the last valid frame (for the Status screen, so an
 * unknown remote's codes can be read off the display). */
uint8_t ir_remote_last_code(void);

#endif /* DRIVER_IR_REMOTE_H */
