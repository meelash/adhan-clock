/*
 * Button Input Driver — debounce + long-press detection
 */

#ifndef DRIVER_BUTTONS_H
#define DRIVER_BUTTONS_H

#include <stdint.h>
#include <stdbool.h>

typedef enum {
    BTN_NONE = 0,
    BTN_OK,         /* select / confirm (K0 short press, IR "5") */
    BTN_UP,
    BTN_DOWN,
    BTN_BACK        /* cancel / exit    (K0 long press,  IR "CH-") */
} button_id_t;

typedef enum {
    BTN_EVT_NONE = 0,
    BTN_EVT_PRESS,          /* key pressed           */
    BTN_EVT_REPEAT          /* auto-repeat while held */
} button_event_t;

typedef struct {
    button_id_t    button;
    button_event_t event;
} btn_event_t;

void        buttons_init(void);
void        buttons_poll(void);           /* call at ~10-20 ms interval */
bool        buttons_get_event(btn_event_t *evt);
bool        buttons_is_held(button_id_t btn);
void        buttons_inject(button_id_t btn, button_event_t evt); /* IR remote / software inject */

#endif /* DRIVER_BUTTONS_H */
