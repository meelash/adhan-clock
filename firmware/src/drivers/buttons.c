/*
 * Button Input Driver — implementation
 *
 * Active-low buttons with internal pull-up. Debounce + long-press + repeat.
 */

#include "drivers/buttons.h"
#include "config.h"
#include "pico/stdlib.h"
#include "hardware/gpio.h"

#define NUM_BUTTONS 3
#define EVT_QUEUE_SIZE 8

typedef struct {
    uint8_t      gpio;
    button_id_t  id;
    bool         raw;
    bool         stable;
    bool         prev_stable;
    uint32_t     debounce_ms;
    uint32_t     press_start_ms;
    bool         long_fired;
    uint32_t     last_repeat_ms;
} btn_state_t;

static btn_state_t btns[NUM_BUTTONS] = {
    { PIN_BTN_MENU, BTN_OK,   true, true, true, 0, 0, false, 0 },
    { PIN_BTN_UP,   BTN_UP,   true, true, true, 0, 0, false, 0 },
    { PIN_BTN_DOWN, BTN_DOWN, true, true, true, 0, 0, false, 0 },
};

static btn_event_t evt_queue[EVT_QUEUE_SIZE];
static uint8_t     evt_head, evt_tail;

static void push_event(button_id_t btn, button_event_t evt) {
    uint8_t next = (evt_head + 1) % EVT_QUEUE_SIZE;
    if (next == evt_tail) return; /* queue full, drop */
    evt_queue[evt_head].button = btn;
    evt_queue[evt_head].event  = evt;
    evt_head = next;
}

void buttons_init(void) {
    for (int i = 0; i < NUM_BUTTONS; i++) {
        if (btns[i].gpio > 29) continue;
        gpio_init(btns[i].gpio);
        gpio_set_dir(btns[i].gpio, GPIO_IN);
        gpio_pull_up(btns[i].gpio);
    }
    evt_head = evt_tail = 0;
}

void buttons_poll(void) {
    uint32_t now = to_ms_since_boot(get_absolute_time());

    for (int i = 0; i < NUM_BUTTONS; i++) {
        btn_state_t *b = &btns[i];
        if (b->gpio > 29) continue;
        bool reading = !gpio_get(b->gpio); /* active low → invert */

        if (reading != b->raw) {
            b->raw = reading;
            b->debounce_ms = now;
        }

        if ((now - b->debounce_ms) >= BTN_DEBOUNCE_MS) {
            b->prev_stable = b->stable;
            b->stable = b->raw;

            /* Detect press (transition to pressed) */
            if (b->stable && !b->prev_stable) {
                b->press_start_ms = now;
                b->long_fired = false;
                b->last_repeat_ms = now;
            }

            /* While held */
            if (b->stable) {
                uint32_t held = now - b->press_start_ms;

                if (!b->long_fired && held >= BTN_LONG_PRESS_MS) {
                    /* Long press: MENU acts as BACK; UP/DOWN start repeating */
                    push_event(b->id == BTN_OK ? BTN_BACK : b->id,
                               b->id == BTN_OK ? BTN_EVT_PRESS : BTN_EVT_REPEAT);
                    b->long_fired = true;
                    b->last_repeat_ms = now;
                }

                if (b->long_fired && b->id != BTN_OK &&
                    (now - b->last_repeat_ms) >= BTN_REPEAT_MS) {
                    push_event(b->id, BTN_EVT_REPEAT);
                    b->last_repeat_ms = now;
                }
            }

            /* Release */
            if (!b->stable && b->prev_stable) {
                if (!b->long_fired) {
                    push_event(b->id, BTN_EVT_PRESS);
                }
            }
        }
    }
}

bool buttons_get_event(btn_event_t *evt) {
    if (evt_tail == evt_head) return false;
    *evt = evt_queue[evt_tail];
    evt_tail = (evt_tail + 1) % EVT_QUEUE_SIZE;
    return true;
}

bool buttons_is_held(button_id_t btn) {
    for (int i = 0; i < NUM_BUTTONS; i++) {
        if (btns[i].id == btn) return btns[i].stable;
    }
    return false;
}

void buttons_inject(button_id_t btn, button_event_t evt) {
    push_event(btn, evt);
}
