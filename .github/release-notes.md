## Flashing

1. Download `adhan_clock-*.uf2` below.
2. Hold **BOOTSEL** on the Pico while plugging it into USB. A drive named **RPI-RP2** appears.
3. Copy the `.uf2` file onto that drive. The Pico reboots into the clock.

See the [README]({{REPO_URL}}/blob/{{TAG}}/README.md) for hardware, SD card setup and settings.

## Wiring this build expects

Pins are fixed at build time. This UF2 only works if your board is wired exactly as below (matching the README's Wiring section). For different wiring, edit `firmware/include/config.h` and build from source.
