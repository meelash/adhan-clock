# Adhan Clock

A prayer-time clock on a Raspberry Pi Pico (RP2040) that shows the time, Hijri date and the day's prayer times on a 64×32 LED matrix, and plays the adhan from an SD card.

## Download and flash

Prebuilt firmware is on the [Releases](../../releases) page. No toolchain needed:

1. Download `adhan_clock-<version>.uf2` from the latest release.
2. Hold **BOOTSEL** on the Pico while plugging it into USB. A drive named **RPI-RP2** appears.
3. Copy the `.uf2` file onto that drive. The Pico reboots into the clock.

**Pins are fixed at build time.** A release UF2 only works on a board wired exactly as in [Wiring](#wiring); each release's notes list its pins. For other wiring, edit `firmware/include/config.h` and [build from source](#building).

## Hardware

| Part | Notes |
|---|---|
| Waveshare [**Pico-RGB-Matrix-P3-64x32**](https://www.waveshare.com/wiki/Pico-RGB-Matrix-P3-64x32) | Carrier board: HUB75 panel, DS3231 RTC, IR receiver, light sensor, buzzer, 3 keys |
| Waveshare [**Pico-Audio**](https://www.waveshare.com/wiki/Pico-Audio) (PCM5101A version) | Jumpered to free GPIOs. The Rev2.1 **CS4344** version needs an MCLK wire and will **not** work with this wiring |
| Waveshare [**Pico-GPS-L76B**](https://www.waveshare.com/wiki/Pico-GPS-L76B) | UART0; optional (sets time + location) |
| MicroSD breakout | Bit-banged SPI. If the module has a regulator/level shifter, power it from 5 V (VSYS), not 3V3 |
| NEC IR remote | The 21-key remote that comes with the carrier board |

### Wiring

The carrier board uses almost every GPIO, so audio and SD share pins with board features:

| Signal | GPIO | Shared with |
|---|---|---|
| I2S DIN / BCLK / LRCLK | 19 / 21 / 17 | K1 key / K2 key / free |
| SD CS / SCK / MOSI / MISO | 22 / 14 / 0 / 15 | HUB75 E (unused by this panel) / free / GPS module's RX / K0 key |
| GPS TX → Pico | 1 | |
| Buzzer | 27 | |

Consequences:
- **Don't press K0/K1/K2.** They short SD/I2S signals to ground. Use the IR remote.
- **SD MOSI shares GP0 with the GPS module's RX pin.** The GPS can stay plugged in: GP0 would only carry one optional config message *to* the GPS (disabled with `PIN_GPS_TX 255`), so the GPS just sees and ignores the SD traffic, and GPS data still arrives on GP1.
- SD MOSI can go on GP27 instead (`PIN_SD_MOSI 27`, and `PIN_GPS_TX 0` if you want the GPS config message), but then it shares the buzzer, which may click while the card is read, and Key Beep is disabled.
- All pins are in `firmware/include/config.h`.

## SD card

FAT32 (or FAT16). Layout:

```
/settings.txt      created on first boot; edit it on a PC
/adhan/*.wav       adhan recordings
```

**Audio format:** PCM WAV, 16-bit (or 8-bit), mono or stereo, 8–48 kHz. **22,050 Hz mono is recommended.** It sounds the same for a voice and needs only 44 KB/s from the bit-banged SD card; 44.1 kHz mono needs 88 KB/s, and 44.1 kHz stereo needs 176 KB/s. The SD diagnostic's speed test tells you what your card and wiring can sustain.

```sh
ffmpeg -i adhan.mp3 -ac 1 -ar 22050 -c:a pcm_s16le makkah.wav

# a whole folder:
for f in *.mp3; do ffmpeg -i "$f" -ac 1 -ar 22050 -c:a pcm_s16le "${f%.mp3}.wav"; done
```

Filenames up to 63 characters. Put an optional separate Fajr adhan in `fajr_adhan =` in settings.txt.

## Using it

Remote keys (VOL+/VOL-/PLAY/CH work as alternates):

| Key | Action |
|---|---|
| **5** | Open settings / select |
| **CH-** | Back / exit |
| **2** / **8** | Up / down (hold to repeat). On the clock screen, flips pages |
| any key | Stops a playing adhan |

Settings menu: Set Time, Calc Method, Asr Method, Adjustments, Hijri Adj, Adhan File, **Test Adhan**, Volume, Brightness, Timezone, DST, Key Beep, **Status**, USB Drive.

- **Timezone** is the *standard* (winter) offset. **DST** can be Off, On, Auto US or Auto EU, and switches automatically.
- **Status** shows GPS fix/satellites, location, SD state and WAV count, RTC state, and the last IR code received. The IR code helps if your remote uses different codes. `un` is the audio underrun count; it should stay 0.
- **USB Drive** (press 5 on its confirm screen) makes the SD card appear on your PC. Eject it on the PC, or press CH-, to return to the clock. Pressing 8 on that screen runs an **SD diagnostic** that includes a read-speed test.
- **settings.txt** holds everything, including latitude/longitude, which is easier than the remote. Without GPS or a set location, the home screen says **NO LOCATION**.
- **Key Beep** clicks the buzzer on each remote key press. It's disabled if SD MOSI is on GP27 (see Wiring). If the clicks are silent, the buzzer is probably passive: set `BUZZER_PASSIVE 1` in `config.h`.
- The RTC keeps UTC. If its battery died, the time shows in red with **SET TIME**.

## Building

Uses the Raspberry Pi Pico VS Code extension toolchain (SDK 1.5.1):

```sh
cd firmware
export PATH=$HOME/.pico-sdk/toolchain/13_2_Rel1/bin:$HOME/.pico-sdk/cmake/v3.28.6/bin:$PATH
export PICO_SDK_PATH=$HOME/.pico-sdk/sdk/1.5.1
cmake -S . -B build && cmake --build build --target adhan_clock -j8
```

Flash `firmware/build/adhan_clock.uf2` the same way as a release UF2 (see [Download and flash](#download-and-flash)).

GitHub Actions ([`.github/workflows/build.yml`](.github/workflows/build.yml)) builds every push. To publish a release, push a version tag; the workflow builds the UF2 and attaches it to a new GitHub Release:

```sh
git tag v1.0 && git push origin v1.0
```

## Design notes

- **Display:** Core 1 bit-bangs the HUB75 panel with 4-bit binary-code modulation. Core 0 draws into a framebuffer and calls `hub75_present()`, which converts it to GPIO bit-planes that Core 1 swaps in at a frame boundary, so there's no tearing.
- **Audio:** a PIO state machine (`firmware/pio/audio_i2s.pio`) generates I2S on three arbitrary pins, running continuously and outputting silence when idle. It uses 64 bit-clocks per frame, and files below 32 kHz are upsampled 2×/4×, because the PCM5101A locks its PLL to the bit clock and a slower clock made it drop out intermittently. DMA ping-pongs through a queue of buffers that the main loop fills from the SD card. SD access never happens in an interrupt.
- **Time:** the DS3231 stores UTC. Local time = UTC + timezone + DST rule. GPS corrects the RTC when it has a fix.
- **Prayer times:** the C port of [Adhan](https://github.com/batoulapps/Adhan) by Batoul Apps (`firmware/lib/adhan`; see [Acknowledgements](#acknowledgements)). The Hijri date uses the tabular Islamic calendar and advances at Maghrib; use Hijri Adj for local moon sighting.

## Acknowledgements

- Prayer times are calculated with [Adhan](https://github.com/batoulapps/Adhan) by **Batoul Apps**, using the C port by stormcaster from [radcheb/Adhan](https://github.com/radcheb/Adhan/tree/master/C/adhan). All of the astronomical calculation and the calculation methods (Muslim World League, ISNA, Umm al-Qura and others) come from their work.
- [FatFs](http://elm-chan.org/fsw/ff/) by ChaN reads the SD card.
- The [Raspberry Pi Pico SDK](https://github.com/raspberrypi/pico-sdk) and [TinyUSB](https://github.com/hathach/tinyusb) (USB Drive mode).

## License

MIT; see [LICENSE](LICENSE). Bundled and linked third-party code keeps its own license: the Raspberry Pi Pico SDK (BSD-3-Clause) with TinyUSB (MIT), the [Adhan](https://github.com/batoulapps/Adhan) C port in `firmware/lib/adhan` (MIT), and FatFs (FatFs license, downloaded at build time). Full texts are in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md), which ships with each release.
