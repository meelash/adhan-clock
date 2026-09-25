/*
 * Screen Definitions — all display screens for the prayer clock
 */

#ifndef UI_SCREENS_H
#define UI_SCREENS_H

#include "drivers/ds3231.h"
#include "prayer/prayer_times.h"
#include "prayer/hijri.h"
#include "app/settings.h"

typedef enum {
    SCREEN_HOME = 0,        /* 3-page carousel: clock + prayer times */
    SCREEN_ADHAN_PLAYING,   /* Adhan (or test) playing */
    SCREEN_SETTINGS_MENU,
    SCREEN_SET_TIME,
    SCREEN_SET_CALC_METHOD,
    SCREEN_SET_ASR_METHOD,
    SCREEN_SET_ADJUSTMENTS,
    SCREEN_SET_HIJRI_ADJ,
    SCREEN_SET_ADHAN_SEL,
    SCREEN_SET_VOLUME,
    SCREEN_SET_BRIGHTNESS,
    SCREEN_SET_TIMEZONE,
    SCREEN_SET_DST,
    SCREEN_SET_KEY_BEEP,
    SCREEN_STATUS,
    SCREEN_USB_CONFIRM,
    SCREEN_MESSAGE,         /* transient message (e.g. playback error) */
    SCREEN_COUNT
} screen_id_t;

/* Settings menu entries, in display order */
typedef enum {
    MENU_SET_TIME = 0,
    MENU_CALC_METHOD,
    MENU_ASR_METHOD,
    MENU_ADJUSTMENTS,
    MENU_HIJRI_ADJ,
    MENU_ADHAN_FILE,
    MENU_TEST_ADHAN,
    MENU_VOLUME,
    MENU_BRIGHTNESS,
    MENU_TIMEZONE,
    MENU_DST,
    MENU_KEY_BEEP,
    MENU_STATUS,
    MENU_USB,
    MENU_COUNT
} menu_item_t;

typedef struct {
    bool time_valid;       /* RTC set (or GPS time) */
    bool location_set;
} home_status_t;

typedef struct {
    /* GPS */
    bool     gps_data;     /* any NMEA received */
    bool     gps_fix;
    int      satellites;
    bool     location_set;
    double   lat, lon;
    /* SD */
    bool     sd_mounted;
    const char *sd_error;
    int      wav_count;
    /* RTC */
    bool     rtc_ok;
    bool     rtc_lost_power;
    /* IR */
    uint8_t  ir_code;
    uint32_t underruns;
    uint32_t sd_retries;      /* SD transfers retried (CRC error, rejection...) */
} status_info_t;

void screen_render_home(const rtc_time_t *time,
                        const hijri_date_t *hijri,
                        const prayer_day_t *prayers,
                        const home_status_t *status,
                        int page);

void screen_render_adhan_playing(const char *title, const char *file_name,
                                 bool audio_ok, uint32_t anim_ms);

void screen_render_settings_menu(int selected_item);

void screen_render_set_time(const rtc_time_t *t, int field, bool blink_on);
void screen_render_calc_method(calc_method_t method);
void screen_render_asr_method(asr_method_t method);
void screen_render_adjustments(const int adjustments[6], int cursor);
void screen_render_hijri_adj(int adj_days, const hijri_date_t *hijri);
void screen_render_adhan_select(const char *files[], int count, int cursor,
                                const char *selected);
void screen_render_brightness(int level);
void screen_render_timezone(double tz);
void screen_render_dst(dst_mode_t mode, bool active_now);
void screen_render_volume(uint8_t vol);
void screen_render_key_beep(bool on, bool available);
void screen_render_status(const status_info_t *st);
void screen_render_usb_confirm(void);
void screen_render_message(const char *l1, const char *l2, const char *l3);

#endif /* UI_SCREENS_H */
