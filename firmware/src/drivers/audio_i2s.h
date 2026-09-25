/*
 * PIO I2S Audio Driver + WAV Playback
 *
 * I2S master output to the PCM5101A DAC on arbitrary GPIOs (see
 * pio/audio_i2s.pio). The I2S clock runs continuously; playback swaps
 * silence for file data.
 */

#ifndef DRIVER_AUDIO_I2S_H
#define DRIVER_AUDIO_I2S_H

#include <stdint.h>
#include <stdbool.h>

typedef enum {
    AUDIO_OK = 0,
    AUDIO_ERR_NOT_INIT,
    AUDIO_ERR_OPEN,
    AUDIO_ERR_READ,
    AUDIO_ERR_NOT_WAV,
    AUDIO_ERR_FORMAT,     /* not 8/16-bit PCM, 1-2 ch, 8-48 kHz */
} audio_error_t;

/* Start the I2S clock (outputs silence) and DMA. Call once at boot. */
void audio_init(void);

/* Begin playing a WAV file from the SD card, e.g. "/adhan/makkah.wav". */
audio_error_t audio_play_wav(const char *path);

/* Stop current playback (the last ~50 ms already queued still plays). */
void audio_stop(void);

/* True while a file is playing (including the buffered tail). */
bool audio_is_playing(void);

/* Refill audio buffers from the SD card. Call from the main loop at least
 * every ~50 ms while playing. */
void audio_poll(void);

/* Number of times the DAC ran dry during the current file (SD too slow). */
uint32_t audio_get_underruns(void);

/* Volume: 0 (mute) – 255 (full), applied digitally. */
void audio_set_volume(uint8_t vol);
uint8_t audio_get_volume(void);

const char *audio_error_str(audio_error_t err);

#endif /* DRIVER_AUDIO_I2S_H */
