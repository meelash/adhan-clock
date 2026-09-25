/*
 * Buzzer — onboard buzzer on PIN_BUZZER (GP27)
 *
 * Only usable when GP27 is not also SD MOSI; otherwise every call is a no-op
 * and buzzer_available() returns false. Beeps are non-blocking: call
 * buzzer_poll() from the main loop to end them.
 */

#ifndef DRIVER_BUZZER_H
#define DRIVER_BUZZER_H

#include <stdint.h>
#include <stdbool.h>

void buzzer_init(void);
bool buzzer_available(void);

/* Start a beep of the given length (replaces any beep in progress). */
void buzzer_beep(uint16_t ms);

/* Ends the current beep when its time is up. Call every main-loop pass. */
void buzzer_poll(void);

#endif /* DRIVER_BUZZER_H */
