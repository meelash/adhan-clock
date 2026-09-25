/*
 * GPS NMEA Parser — implementation
 *
 * Handles $xxRMC (time, date, position, fix status) and $xxGGA (satellites,
 * altitude). Bytes are captured by a UART RX interrupt into a ring buffer so
 * nothing is lost while the main loop is busy (e.g. streaming audio from SD);
 * gps_poll() parses them on the main loop.
 */

#include "drivers/gps.h"
#include "config.h"
#include "pico/stdlib.h"
#include "hardware/uart.h"
#include "hardware/irq.h"
#include <string.h>
#include <stdlib.h>

#define NMEA_MAX_LEN   100
#define RX_RING_SIZE   512   /* power of two */
#define FIX_TIMEOUT_MS 5000

static gps_data_t g_gps;
static char       sentence_buf[NMEA_MAX_LEN];
static uint8_t    sentence_len;
static bool       sentence_active;

static volatile uint8_t  rx_ring[RX_RING_SIZE];
static volatile uint16_t rx_head, rx_tail;

/* ── UART RX interrupt ─────────────────────────────────────────────────── */
static void gps_uart_irq(void) {
    while (uart_is_readable(GPS_UART)) {
        uint8_t c = (uint8_t)uart_getc(GPS_UART);
        uint16_t next = (rx_head + 1) & (RX_RING_SIZE - 1);
        if (next != rx_tail) {
            rx_ring[rx_head] = c;
            rx_head = next;
        }
    }
}

/* ── NMEA field helpers ────────────────────────────────────────────────── */

/* Advance *p past the next comma; return pointer to start of field */
static const char *next_field(const char **p) {
    const char *start = *p;
    while (**p && **p != ',' && **p != '*') (*p)++;
    if (**p == ',') (*p)++;
    return start;
}

static bool field_empty(const char *f) {
    return *f == ',' || *f == '*' || *f == '\0';
}

static bool digits(const char *f, int n) {
    for (int i = 0; i < n; i++)
        if (f[i] < '0' || f[i] > '9') return false;
    return true;
}

static double parse_latlon(const char *field, char dir) {
    /* NMEA format: (D)DDMM.MMMM */
    double raw = strtod(field, NULL);
    int    deg = (int)(raw / 100.0);
    double min = raw - deg * 100.0;
    double val = deg + min / 60.0;
    if (dir == 'S' || dir == 'W') val = -val;
    return val;
}

static bool nmea_checksum_ok(const char *s, int len) {
    if (len < 6 || s[0] != '$') return false;
    const char *star = memchr(s, '*', (size_t)len);
    if (!star || star + 2 >= s + len) return false;
    uint8_t ck = 0;
    for (const char *p = s + 1; p < star; p++) ck ^= (uint8_t)*p;
    return ck == (uint8_t)strtol(star + 1, NULL, 16);
}

/* ── Sentence parsers ──────────────────────────────────────────────────── */

static void parse_rmc(const char *s) {
    /* $GPRMC,HHMMSS.ss,A,lat,N,lon,W,spd,crs,DDMMYY,mag,E,mode*ck */
    const char *p = s;
    next_field(&p);                          /* talker + RMC */
    const char *tf    = next_field(&p);
    const char *st    = next_field(&p);
    const char *lat_f = next_field(&p);
    const char *lat_d = next_field(&p);
    const char *lon_f = next_field(&p);
    const char *lon_d = next_field(&p);
    next_field(&p);                          /* speed */
    next_field(&p);                          /* course */
    const char *df    = next_field(&p);

    uint32_t now = to_ms_since_boot(get_absolute_time());
    bool valid = (*st == 'A');

    if (valid && digits(tf, 6) && digits(df, 6)) {
        g_gps.hour   = (uint8_t)((tf[0] - '0') * 10 + (tf[1] - '0'));
        g_gps.minute = (uint8_t)((tf[2] - '0') * 10 + (tf[3] - '0'));
        g_gps.second = (uint8_t)((tf[4] - '0') * 10 + (tf[5] - '0'));
        g_gps.day    = (uint8_t)((df[0] - '0') * 10 + (df[1] - '0'));
        g_gps.month  = (uint8_t)((df[2] - '0') * 10 + (df[3] - '0'));
        g_gps.year   = (uint16_t)(2000 + (df[4] - '0') * 10 + (df[5] - '0'));
        g_gps.time_ms = now;
        g_gps.time_valid = true;
    }

    if (valid && !field_empty(lat_f) && !field_empty(lon_f)
            && (*lat_d == 'N' || *lat_d == 'S')
            && (*lon_d == 'E' || *lon_d == 'W')) {
        g_gps.latitude    = parse_latlon(lat_f, *lat_d);
        g_gps.longitude   = parse_latlon(lon_f, *lon_d);
        g_gps.fix_valid   = true;
        g_gps.ever_fixed  = true;
        g_gps.last_fix_ms = now;
    } else {
        g_gps.fix_valid = false;
    }
}

static void parse_gga(const char *s) {
    /* $GPGGA,time,lat,N,lon,E,fix,sats,hdop,alt,M,... */
    const char *p = s;
    for (int i = 0; i < 7; i++) next_field(&p);
    const char *sats = next_field(&p);
    if (!field_empty(sats)) g_gps.satellites = (uint8_t)atoi(sats);
    next_field(&p);                          /* HDOP */
    const char *alt = next_field(&p);
    if (!field_empty(alt)) g_gps.altitude_m = strtod(alt, NULL);
}

static void process_sentence(const char *s, int len) {
    if (!nmea_checksum_ok(s, len)) return;
    g_gps.sentences++;
    /* $GPRMC / $GNRMC / $GLRMC ... */
    if (strncmp(s + 3, "RMC,", 4) == 0)      parse_rmc(s);
    else if (strncmp(s + 3, "GGA,", 4) == 0) parse_gga(s);
}

/* ── Public API ─────────────────────────────────────────────────────────── */

void gps_init(void) {
    memset(&g_gps, 0, sizeof(g_gps));
    sentence_len = 0;
    sentence_active = false;
    rx_head = rx_tail = 0;

    uart_init(GPS_UART, GPS_BAUD);
    gpio_set_function(PIN_GPS_RX, GPIO_FUNC_UART);
#if PIN_GPS_TX <= 29
    gpio_set_function(PIN_GPS_TX, GPIO_FUNC_UART);
#endif
    uart_set_fifo_enabled(GPS_UART, true);

    int irq = (GPS_UART == uart0) ? UART0_IRQ : UART1_IRQ;
    irq_set_exclusive_handler(irq, gps_uart_irq);
    irq_set_enabled(irq, true);
    uart_set_irq_enables(GPS_UART, true, false);

#if PIN_GPS_TX <= 29
    /* Only RMC + GGA, once per second (less parsing, no overflow). */
    uart_puts(GPS_UART, "$PMTK314,0,1,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0*28\r\n");
#endif
}

void gps_poll(void) {
    while (rx_tail != rx_head) {
        char c = (char)rx_ring[rx_tail];
        rx_tail = (rx_tail + 1) & (RX_RING_SIZE - 1);
        g_gps.bytes++;

        if (c == '$') {
            sentence_len = 0;
            sentence_active = true;
        }
        if (!sentence_active) continue;

        if (c == '\n' || c == '\r') {
            sentence_buf[sentence_len] = '\0';
            process_sentence(sentence_buf, sentence_len);
            sentence_active = false;
        } else if (sentence_len < NMEA_MAX_LEN - 1) {
            sentence_buf[sentence_len++] = c;
        } else {
            sentence_active = false;        /* overlong: discard */
        }
    }

    if (g_gps.fix_valid && gps_fix_age_ms() > FIX_TIMEOUT_MS)
        g_gps.fix_valid = false;
}

const gps_data_t *gps_get_data(void) {
    return &g_gps;
}

bool gps_has_fix(void) {
    return g_gps.fix_valid;
}

uint32_t gps_fix_age_ms(void) {
    if (!g_gps.ever_fixed) return UINT32_MAX;
    return to_ms_since_boot(get_absolute_time()) - g_gps.last_fix_ms;
}
