/*
 * PIO I2S Audio Driver + WAV Playback — implementation
 *
 * Design:
 *   - The I2S state machine runs continuously from audio_init(). When nothing
 *     is playing, DMA feeds it a silence buffer. Keeping BCLK running means
 *     the PCM5101A's PLL stays locked: no pop and no lost audio at the start
 *     of every adhan.
 *   - Two DMA channels are chained ping-pong. Each time one finishes, the IRQ
 *     re-points it at the next queued buffer (or at silence). The IRQ never
 *     touches the SD card.
 *   - audio_poll() (main loop) reads the WAV file into free buffers and queues
 *     them. All FatFS access therefore stays on the main loop, which FatFS
 *     requires (FF_FS_REENTRANT = 0).
 *
 * Supported files: PCM WAV, 8- or 16-bit, mono or stereo, 8-48 kHz. The PIO
 * clock follows the file; files below 32 kHz are upsampled 2x/4x (linear
 * interpolation) so the DAC's bit clock stays in its PLL's range.
 */

#include "drivers/audio_i2s.h"
#include "config.h"
#include "ff.h"
#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/dma.h"
#include "hardware/irq.h"
#include "hardware/clocks.h"
#include "hardware/sync.h"
#include "audio_i2s.pio.h"
#include <string.h>

#define NBUF            AUDIO_NUM_BUFFERS
#define BUF_FRAMES      AUDIO_DMA_BUF_SAMPLES
#define SILENCE_FRAMES  256

_Static_assert((NBUF & (NBUF - 1)) == 0, "AUDIO_NUM_BUFFERS must be a power of two");

/* ── Buffers + queue (shared with DMA IRQ) ──────────────────────────────── */
static uint32_t bufs[NBUF][BUF_FRAMES];
static uint32_t buf_len[NBUF];              /* frames of valid data */
static uint32_t silence[SILENCE_FRAMES];

enum { BUF_FREE = 0, BUF_QUEUED, BUF_PLAYING };
static volatile uint8_t buf_state[NBUF];

static volatile uint8_t queue[NBUF];
static volatile uint8_t q_head, q_tail;     /* push at head, pop at tail */

static int dma_ch[2];
static volatile int chan_buf[2];            /* buffer index in flight, -1 = silence */
static volatile uint32_t underruns;

/* ── WAV playback state (main loop only) ────────────────────────────────── */
static FIL       wav_file;
static bool      wav_open;
static uint32_t  wav_data_remaining;        /* bytes left in data chunk */
static uint16_t  wav_channels;
static uint16_t  wav_bits;
static uint32_t  wav_sample_rate;
static uint32_t  current_rate;
static uint32_t  upsample;                  /* 1, 2 or 4: keeps the I2S rate >= 32 kHz */
static int32_t   prev_l, prev_r;            /* last input sample, for interpolation */
static int       leadin_bufs;               /* silence buffers to queue first */
static uint8_t   volume = 200;              /* 0-255 */
static bool      initialised;

static uint8_t   raw[BUF_FRAMES * 4];       /* static: far too big for the stack */

static PIO  pio_inst = AUDIO_PIO;
static uint pio_sm   = AUDIO_PIO_SM;

/* ── Queue helpers ──────────────────────────────────────────────────────── */
static inline int queue_count(void) {
    return (int)((uint8_t)(q_head - q_tail)) ;
}

static int queue_pop(void) {            /* IRQ context */
    if (q_head == q_tail) return -1;
    int idx = queue[q_tail % NBUF];
    q_tail++;
    return idx;
}

static void queue_push(int idx) {       /* main loop */
    buf_state[idx] = BUF_QUEUED;
    queue[q_head % NBUF] = (uint8_t)idx;
    __dmb();
    q_head++;
}

/* ── DMA IRQ: re-arm the channel that just finished ─────────────────────── */
static void __isr dma_irq_handler(void) {
    for (int c = 0; c < 2; c++) {
        uint32_t mask = 1u << dma_ch[c];
        if (!(dma_hw->ints0 & mask)) continue;
        dma_hw->ints0 = mask;

        int done = chan_buf[c];
        if (done >= 0) buf_state[done] = BUF_FREE;

        int next = queue_pop();
        if (next >= 0) {
            buf_state[next] = BUF_PLAYING;
            chan_buf[c] = next;
            dma_channel_set_read_addr(dma_ch[c], bufs[next], false);
            dma_channel_set_trans_count(dma_ch[c], buf_len[next], false);
        } else {
            if (wav_open && wav_data_remaining > 0) underruns++;
            chan_buf[c] = -1;
            dma_channel_set_read_addr(dma_ch[c], silence, false);
            dma_channel_set_trans_count(dma_ch[c], SILENCE_FRAMES, false);
        }
    }
}

/* ── WAV header parsing ─────────────────────────────────────────────────── */
typedef struct __attribute__((packed)) {
    char     riff[4];
    uint32_t file_size;
    char     wave[4];
} wav_riff_t;

typedef struct __attribute__((packed)) {
    char     id[4];
    uint32_t size;
} wav_chunk_t;

typedef struct __attribute__((packed)) {
    uint16_t audio_format;
    uint16_t num_channels;
    uint32_t sample_rate;
    uint32_t byte_rate;
    uint16_t block_align;
    uint16_t bits_per_sample;
} wav_fmt_t;

static audio_error_t parse_wav_header(void) {
    UINT br;
    wav_riff_t riff;
    if (f_read(&wav_file, &riff, sizeof(riff), &br) != FR_OK || br != sizeof(riff))
        return AUDIO_ERR_READ;
    if (memcmp(riff.riff, "RIFF", 4) != 0 || memcmp(riff.wave, "WAVE", 4) != 0)
        return AUDIO_ERR_NOT_WAV;

    bool got_fmt = false;
    for (int guard = 0; guard < 32; guard++) {
        wav_chunk_t chunk;
        if (f_read(&wav_file, &chunk, sizeof(chunk), &br) != FR_OK || br != sizeof(chunk))
            return AUDIO_ERR_NOT_WAV;
        uint32_t padded = chunk.size + (chunk.size & 1); /* RIFF chunks are word aligned */

        if (memcmp(chunk.id, "fmt ", 4) == 0) {
            wav_fmt_t fmt;
            if (chunk.size < sizeof(fmt)) return AUDIO_ERR_NOT_WAV;
            if (f_read(&wav_file, &fmt, sizeof(fmt), &br) != FR_OK || br != sizeof(fmt))
                return AUDIO_ERR_READ;
            /* 0xFFFE = WAVE_FORMAT_EXTENSIBLE, which is still plain PCM for us */
            if (fmt.audio_format != 1 && fmt.audio_format != 0xFFFE)
                return AUDIO_ERR_FORMAT;
            if (fmt.num_channels < 1 || fmt.num_channels > 2)
                return AUDIO_ERR_FORMAT;
            if (fmt.bits_per_sample != 8 && fmt.bits_per_sample != 16)
                return AUDIO_ERR_FORMAT;
            if (fmt.sample_rate < 8000 || fmt.sample_rate > 48000)
                return AUDIO_ERR_FORMAT;
            wav_channels    = fmt.num_channels;
            wav_sample_rate = fmt.sample_rate;
            wav_bits        = fmt.bits_per_sample;
            got_fmt = true;
            if (padded > sizeof(fmt))
                f_lseek(&wav_file, f_tell(&wav_file) + padded - sizeof(fmt));
        } else if (memcmp(chunk.id, "data", 4) == 0) {
            if (!got_fmt) return AUDIO_ERR_NOT_WAV;
            wav_data_remaining = chunk.size;
            return AUDIO_OK;
        } else {
            f_lseek(&wav_file, f_tell(&wav_file) + padded);
        }
    }
    return AUDIO_ERR_NOT_WAV;
}

/* ── Fill one buffer from the file. Returns frames written (0 = EOF). ──── */
static uint32_t fill_from_file(uint32_t *out) {
    uint32_t bytes_per_frame = wav_channels * (wav_bits / 8);
    uint32_t want = (BUF_FRAMES / upsample) * bytes_per_frame;
    if (want > wav_data_remaining) want = wav_data_remaining;
    want -= want % bytes_per_frame;
    if (want == 0) { wav_data_remaining = 0; return 0; }

    UINT br = 0;
    if (f_read(&wav_file, raw, want, &br) != FR_OK || br == 0) {
        wav_data_remaining = 0;
        return 0;
    }
    wav_data_remaining -= br;

    uint32_t frames = br / bytes_per_frame;
    int32_t vol = volume;
    const uint8_t *p = raw;
    uint32_t o = 0;

    for (uint32_t i = 0; i < frames; i++) {
        int32_t l, r;
        if (wav_bits == 16) {
            l = (int16_t)(p[0] | (p[1] << 8));
            r = (wav_channels == 2) ? (int16_t)(p[2] | (p[3] << 8)) : l;
        } else {
            l = ((int32_t)p[0] - 128) << 8;
            r = (wav_channels == 2) ? (((int32_t)p[1] - 128) << 8) : l;
        }
        p += bytes_per_frame;
        l = (l * vol) >> 8;
        r = (r * vol) >> 8;
        /* Low-rate files: linear interpolation up to the I2S rate */
        for (uint32_t k = 1; k <= upsample; k++) {
            int32_t il = prev_l + (l - prev_l) * (int32_t)k / (int32_t)upsample;
            int32_t ir = prev_r + (r - prev_r) * (int32_t)k / (int32_t)upsample;
            out[o++] = ((uint32_t)(uint16_t)il << 16) | (uint16_t)ir;
        }
        prev_l = l;
        prev_r = r;
    }
    return o;
}

static void set_sample_rate(uint32_t rate) {
    if (rate == current_rate) return;
    /* 64 BCLK per frame, 4 PIO cycles per BCLK (see audio_i2s.pio) */
    float div = (float)clock_get_hz(clk_sys) / (float)(rate * 64u * 4u);
    pio_sm_set_clkdiv(pio_inst, pio_sm, div);
    current_rate = rate;
}

/* ── Public API ─────────────────────────────────────────────────────────── */

void audio_init(void) {
    if (initialised) return;
    memset(silence, 0, sizeof(silence));
    for (int i = 0; i < NBUF; i++) buf_state[i] = BUF_FREE;
    q_head = q_tail = 0;
    upsample = 1;

    uint offset = pio_add_program(pio_inst, &audio_i2s_program);
    pio_sm_claim(pio_inst, pio_sm);
    audio_i2s_program_init(pio_inst, pio_sm, offset,
                           PIN_I2S_DIN, PIN_I2S_BCLK, PIN_I2S_LRCLK);
    current_rate = 0;
    set_sample_rate(AUDIO_SAMPLE_RATE);

    for (int c = 0; c < 2; c++) dma_ch[c] = dma_claim_unused_channel(true);
    for (int c = 0; c < 2; c++) {
        dma_channel_config cfg = dma_channel_get_default_config(dma_ch[c]);
        channel_config_set_transfer_data_size(&cfg, DMA_SIZE_32);
        channel_config_set_read_increment(&cfg, true);
        channel_config_set_write_increment(&cfg, false);
        channel_config_set_dreq(&cfg, pio_get_dreq(pio_inst, pio_sm, true));
        channel_config_set_chain_to(&cfg, dma_ch[c ^ 1]);
        dma_channel_configure(dma_ch[c], &cfg, &pio_inst->txf[pio_sm],
                              silence, SILENCE_FRAMES, false);
        chan_buf[c] = -1;
    }

    dma_hw->ints0 = (1u << dma_ch[0]) | (1u << dma_ch[1]);
    dma_channel_set_irq0_enabled(dma_ch[0], true);
    dma_channel_set_irq0_enabled(dma_ch[1], true);
    irq_add_shared_handler(DMA_IRQ_0, dma_irq_handler,
                           PICO_SHARED_IRQ_HANDLER_DEFAULT_ORDER_PRIORITY);
    irq_set_enabled(DMA_IRQ_0, true);

    dma_channel_start(dma_ch[0]);
    pio_sm_set_enabled(pio_inst, pio_sm, true);
    initialised = true;
}

audio_error_t audio_play_wav(const char *path) {
    if (!initialised) return AUDIO_ERR_NOT_INIT;
    audio_stop();

    if (f_open(&wav_file, path, FA_READ) != FR_OK)
        return AUDIO_ERR_OPEN;
    wav_open = true;

    audio_error_t err = parse_wav_header();
    if (err != AUDIO_OK) {
        f_close(&wav_file);
        wav_open = false;
        return err;
    }

    /* A rate change makes the DAC re-lock; give it a little more silence. */
    /* The DAC's PLL locks to BCLK; keep the I2S rate >= 32 kHz by
     * upsampling low-rate files (22.05 kHz -> 44.1 kHz, 8 kHz -> 32 kHz). */
    upsample = 1;
    while (wav_sample_rate * upsample < 32000) upsample *= 2;
    prev_l = prev_r = 0;
    uint32_t i2s_rate = wav_sample_rate * upsample;
    leadin_bufs = (i2s_rate != current_rate) ? 2 : 1;
    set_sample_rate(i2s_rate);
    underruns = 0;

    audio_poll(); /* queue the first buffers immediately */
    return AUDIO_OK;
}

void audio_stop(void) {
    if (!initialised) return;
    /* Drop everything still queued; at most the two in-flight buffers
     * (~50 ms) finish playing. */
    uint32_t save = save_and_disable_interrupts();
    while (q_tail != q_head) {
        buf_state[queue[q_tail % NBUF]] = BUF_FREE;
        q_tail++;
    }
    restore_interrupts(save);

    if (wav_open) {
        f_close(&wav_file);
        wav_open = false;
    }
    wav_data_remaining = 0;
    leadin_bufs = 0;
}

bool audio_is_playing(void) {
    if (wav_open) return true;
    /* File finished: still playing until the tail drains. */
    return queue_count() > 0 || chan_buf[0] >= 0 || chan_buf[1] >= 0;
}

void audio_poll(void) {
    if (!wav_open) return;

    /* Fill at most two buffers per call so a slow card doesn't freeze the
     * display and remote; the main loop calls this several times per pass. */
    int filled = 0;
    for (int i = 0; i < NBUF && filled < 2; i++) {
        if (buf_state[i] != BUF_FREE) continue;
        filled++;

        uint32_t frames;
        if (leadin_bufs > 0) {
            memset(bufs[i], 0, sizeof(bufs[i]));
            frames = BUF_FRAMES;
            leadin_bufs--;
        } else {
            frames = fill_from_file(bufs[i]);
        }
        if (frames == 0) {
            f_close(&wav_file);
            wav_open = false;
            return;
        }
        buf_len[i] = frames;
        queue_push(i);
    }
}

uint32_t audio_get_underruns(void) { return underruns; }

void audio_set_volume(uint8_t vol) { volume = vol; }
uint8_t audio_get_volume(void) { return volume; }

const char *audio_error_str(audio_error_t err) {
    switch (err) {
    case AUDIO_OK:           return "OK";
    case AUDIO_ERR_NOT_INIT: return "no audio";
    case AUDIO_ERR_OPEN:     return "open fail";
    case AUDIO_ERR_READ:     return "read err";
    case AUDIO_ERR_NOT_WAV:  return "not WAV";
    case AUDIO_ERR_FORMAT:   return "bad fmt";
    }
    return "?";
}
