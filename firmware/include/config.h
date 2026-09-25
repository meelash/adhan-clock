/*
 * Adhan Clock — Master Hardware Configuration
 *
 * Target : Raspberry Pi Pico (RP2040)
 * Display: Waveshare Pico-RGB-Matrix-P3-64x32 (HUB75)
 * Audio  : Waveshare Pico-Audio (PCM5101A) — rewired to free GPIOs via PIO I2S
 * GPS    : Waveshare Pico-GPS-L76B (UART)
 * Storage: MicroSD breakout (bit-banged SPI)
 * IR     : Onboard IR receiver on Waveshare carrier board (GP28, NEC protocol)
 */

#ifndef ADHAN_CLOCK_CONFIG_H
#define ADHAN_CLOCK_CONFIG_H

#include "hardware/i2c.h"
#include "hardware/uart.h"

/* ===== Display: HUB75 P3 64×32 =========================================== */
#define DISPLAY_WIDTH       64
#define DISPLAY_HEIGHT      32
#define DISPLAY_SCAN_ROWS   16      /* 1/16 scan */

#define PIN_HUB75_R1        2
#define PIN_HUB75_G1        3
#define PIN_HUB75_B1        4
#define PIN_HUB75_R2        5
#define PIN_HUB75_G2        8
#define PIN_HUB75_B2        9
#define PIN_HUB75_ADDR_A    10
#define PIN_HUB75_ADDR_B    16
#define PIN_HUB75_ADDR_C    18
#define PIN_HUB75_ADDR_D    20
#define PIN_HUB75_CLK       11
#define PIN_HUB75_LAT       12
#define PIN_HUB75_OE        13

/* ===== RTC: DS3231 on I2C0 ================================================ */
#define PIN_I2C_SDA         6
#define PIN_I2C_SCL         7
#define RTC_I2C             i2c1    /* GP6/GP7 belong to I2C1, not I2C0 */
#define RTC_I2C_ADDR        0x68
#define RTC_I2C_BAUD        400000

/* ===== Buttons ============================================================ */
/* The board's keys (K0=GP15, K1=GP19, K2=GP21) are repurposed for SD MISO
 * and I2S, so the IR remote is the input device. 255 = disabled.
 * Don't press K0/K1/K2 — it shorts those signals to ground. */
#define PIN_BTN_MENU        255
#define PIN_BTN_UP          255
#define PIN_BTN_DOWN        255
#define BTN_DEBOUNCE_MS     50
#define BTN_LONG_PRESS_MS   1000
#define BTN_REPEAT_MS       200

/* ===== Buzzer ============================================================= */
/* Onboard buzzer. Only used (key clicks) when GP27 is NOT SD MOSI; otherwise
 * the buzzer code compiles to nothing. If key clicks are silent, the buzzer
 * is probably passive: set BUZZER_PASSIVE to 1 (drives a PWM tone). */
#define PIN_BUZZER          27
#define BUZZER_PASSIVE      0
#define BUZZER_FREQ_HZ      2700
#define BUZZER_CLICK_MS     25

/* ===== Light Sensor (ADC0) ================================================ */
#define PIN_LIGHT_SENSOR    26
#define LIGHT_ADC_INPUT     0       /* ADC channel 0 = GP26 */

/* ===== GPS: Quectel L76B on UART0 ======================================= */
/* PIN_GPS_TX (Pico → GPS) only sends one optional config command. Set it to
 * 255 when GP0 is reused as SD MOSI (see README); the GPS can stay wired —
 * it just ignores the SD traffic on its RX pin. */
#define PIN_GPS_TX          255
#define PIN_GPS_RX          1
#define GPS_UART            uart0
#define GPS_BAUD            9600

/* ===== Audio: PCM5101A via PIO I2S (rewired) ============================== */
#define PIN_I2S_DIN         19      /* Data out to DAC */
#define PIN_I2S_BCLK        21      /* Bit clock       */
#define PIN_I2S_LRCLK       17      /* Word select     */
#define AUDIO_PIO           pio0
#define AUDIO_PIO_SM        0
#define AUDIO_SAMPLE_RATE   44100
#define AUDIO_BIT_DEPTH     16
#define AUDIO_CHANNELS      2
#define AUDIO_DMA_BUF_SAMPLES 2048  /* stereo frames per buffer (46 ms @ 44.1k) */
#define AUDIO_NUM_BUFFERS   8       /* power of two; 8 x 46 ms ≈ 370 ms of cushion
                                     * against slow SD reads (64 KB of RAM) */

/* ===== SD Card (bit-banged SPI) ============================================ */
/* Wiring: CS→GP22 (HUB75 'E', unused by this 1/16-scan panel), SCK→GP14,
 * MOSI→GP0 (shared with the GPS module's RX pin; or GP27, shared with the
 * buzzer), MISO→GP15 (shared with key K0). */
#define PIN_SD_SCK          14
#define PIN_SD_MOSI         0
#define PIN_SD_MISO         15
#define PIN_SD_CS           22
/* Fastest SPI clock to try, as extra delay loops (3 cycles each) per half
 * clock; 0 = as fast as the bit-bang goes. At startup the driver verifies
 * reads against a slow reference, slows down until they're correct, then
 * backs off one more step for margin. The SD diagnostic (Settings → USB
 * Drive → DN) shows the measured clock and throughput. */
#define SD_SPI_FAST_DELAY   0

/* ===== IR Remote: onboard receiver on GP28 (NEC protocol) ================ */
/* Waveshare carrier board hardwires an IR receiver to GP28. */
#define PIN_IR_DATA         28
/* NEC command bytes for a standard 21-key remote (address = IR_REMOTE_ADDR). */
/* Run an IR decoder sketch to find your remote's address/codes if different. */
#define IR_REMOTE_ADDR      0x00    /* set to 0xFF to accept any remote address */
#define IR_KEY_UP           0x18    /* "2" — scroll up / increment value       */
#define IR_KEY_DOWN         0x52    /* "8" — scroll down / decrement value     */
#define IR_KEY_OK           0x1C    /* "5" — select / open settings            */
#define IR_KEY_BACK         0x45    /* "CH-" — back / exit                     */
/* Alternates on the same remote */
#define IR_KEY_UP_ALT       0x15    /* "VOL+"  */
#define IR_KEY_DOWN_ALT     0x07    /* "VOL-"  */
#define IR_KEY_OK_ALT       0x43    /* "PLAY"  */
#define IR_KEY_BACK_ALT     0x46    /* "CH"    */

/* ===== Application Defaults =============================================== */
#define MAX_ADHAN_FILES     15
#define ADHAN_DIR           "/adhan"
#define SETTINGS_FILE       "/settings.txt"   /* human-editable key=value */
#define SETTINGS_FILE_V1    "/settings.dat"   /* old binary format, migrated once */

#define DEFAULT_CALC_METHOD CALC_KARACHI
#define DEFAULT_ASR_METHOD  ASR_HANAFI
#define DEFAULT_LATITUDE    0.0
#define DEFAULT_LONGITUDE   0.0
#define DEFAULT_TIMEZONE    0.0
#define DEFAULT_ELEVATION   0.0

/* ===== Display Timing ===================================================== */
#define COLOR_DEPTH_BITS    4       /* bits per colour channel (R/G/B) */

/* ===== Brightness ========================================================= */
#define BRIGHTNESS_LEVELS   16
#define BRIGHTNESS_AUTO     0       /* 0 = auto, 1-16 = manual */

#endif /* ADHAN_CLOCK_CONFIG_H */
