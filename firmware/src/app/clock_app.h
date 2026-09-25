/*
 * Main Clock Application — State Machine
 *
 * Orchestrates: time display, prayer time checking, adhan triggers,
 * settings navigation, GPS updates, and brightness control.
 */

#ifndef APP_CLOCK_APP_H
#define APP_CLOCK_APP_H

#include <stdbool.h>

typedef enum {
	CLOCK_APP_MODE_CLOCK = 0,
	CLOCK_APP_MODE_USB,
	CLOCK_APP_MODE_SD_DEBUG,
} clock_app_mode_t;

/* Initialise all subsystems and start the clock. */
void clock_app_init(void);

/* Main loop iteration — call continuously.
 * Handles: polling GPS, buttons, audio; updating display; checking adhan. */
clock_app_mode_t clock_app_update(void);

/* The adhan file Test Adhan would play (NULL if none). */
const char *clock_app_selected_adhan(void);

#endif /* APP_CLOCK_APP_H */
