#!/usr/bin/env python3
"""Generate docs/img/wiring.svg: the Pico pinout and the three add-on modules.

Pin assignments mirror firmware/include/config.h. The Pico plugs into the
Pico-RGB-Matrix carrier board, which uses the pins drawn in grey.
"""
from pathlib import Path

W, H = 1100, 720
PX0, PX1 = 470, 630          # Pico body
PIN_Y0, PITCH = 100, 24      # pin 1 / pin 40 centre, spacing

LEFT = ["GP0", "GP1", "GND", "GP2", "GP3", "GP4", "GP5", "GND", "GP6", "GP7",
        "GP8", "GP9", "GND", "GP10", "GP11", "GP12", "GP13", "GND", "GP14", "GP15"]
RIGHT = ["VBUS", "VSYS", "GND", "3V3_EN", "3V3", "ADC_VREF", "GP28", "GND", "GP27", "GP26",
         "RUN", "GP22", "GND", "GP21", "GP20", "GP19", "GP18", "GND", "GP17", "GP16"]  # pins 40..21

SD, GPS, AUDIO, PWR = "#5aa9ff", "#f2b84b", "#3fd69a", "#ff7a7a"
INK, DIM, BOARD, PANEL = "#e8e6df", "#7d8595", "#17613f", "#151a24"

# What the carrier board uses each remaining pin for (shown dimmed)
CARRIER = {"GP2": "R1", "GP3": "G1", "GP4": "B1", "GP5": "R2", "GP6": "RTC SDA", "GP7": "RTC SCL",
           "GP8": "G2", "GP9": "B2", "GP10": "A", "GP11": "CLK", "GP12": "LAT", "GP13": "OE",
           "GP16": "B", "GP18": "C", "GP20": "D", "GP26": "light", "GP27": "buzzer", "GP28": "IR"}
USED = {"GP0": SD, "GP1": GPS, "GP14": SD, "GP15": SD, "GP22": SD,
        "GP21": AUDIO, "GP19": AUDIO, "GP17": AUDIO, "VBUS": PWR, "VSYS": PWR, "3V3": PWR}


def ly(i):  # left pin i (0-based, pin i+1)
    return PIN_Y0 + i * PITCH


def ry(name):
    return PIN_Y0 + RIGHT.index(name) * PITCH


out = []
add = out.append


def text(x, y, s, fill=INK, size=13, anchor="start", weight=400, family="sans", extra=""):
    fam = "ui-monospace,SFMono-Regular,Menlo,Consolas,monospace" if family == "mono" else \
          "system-ui,-apple-system,'Segoe UI',Roboto,sans-serif"
    s = s.replace("&", "&amp;").replace("<", "&lt;")
    add(f'<text x="{x}" y="{y}" fill="{fill}" font-size="{size}" font-family="{fam}" '
        f'text-anchor="{anchor}" font-weight="{weight}" {extra}>{s}</text>')


def wire(points, colour, dash=False):
    d = " ".join(f"{'M' if i == 0 else 'L'}{x},{y}" for i, (x, y) in enumerate(points))
    da = ' stroke-dasharray="6 5"' if dash else ""
    add(f'<path d="{d}" fill="none" stroke="{colour}" stroke-width="3" '
        f'stroke-linejoin="round" stroke-linecap="round"{da}/>')


def module(x, y, w, h, title, subtitle, colour):
    add(f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="10" fill="{PANEL}" '
        f'stroke="{colour}" stroke-width="2"/>')
    text(x + 14, y + 24, title, colour, 15, weight=650)
    text(x + 14, y + 42, subtitle, DIM, 12)


def module_pin(x, y, label, colour, side="right"):
    add(f'<circle cx="{x}" cy="{y}" r="4.5" fill="{colour}"/>')
    if side == "right":
        text(x - 10, y + 4, label, INK, 12, "end", family="mono")
    else:
        text(x + 10, y + 4, label, INK, 12, family="mono")


add(f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {W} {H}" width="{W}" height="{H}" '
    f'role="img" aria-labelledby="t d">')
add('<title id="t">Adhan Clock wiring</title>')
add('<desc id="d">Raspberry Pi Pico pinout. SD card: MOSI GP0, SCK GP14, MISO GP15, CS GP22, '
    'power from VBUS. GPS: TX to GP1, RX shares GP0. Pico-Audio: DIN GP19, BCK GP21, LRCK GP17, '
    'power from VSYS and 3V3. All other pins are used by the matrix carrier board.</desc>')

# ── Pico board ──
add(f'<rect x="{PX0 + 55}" y="58" width="50" height="22" rx="4" fill="#9aa3ad"/>')  # USB
add(f'<rect x="{PX0}" y="72" width="{PX1 - PX0}" height="{ly(19) - 72 + 28}" rx="8" fill="{BOARD}"/>')
text((PX0 + PX1) / 2, 50, "USB", DIM, 11, "middle")
text((PX0 + PX1) / 2, (72 + ly(19)) / 2 - 8, "Raspberry Pi Pico", "#cfe9da", 15, "middle", 650,
     extra=f'transform="rotate(-90 {(PX0 + PX1) / 2} {(72 + ly(19)) / 2})"')
text((PX0 + PX1) / 2, (72 + ly(19)) / 2 + 12, "top view", "#8fbfa5", 11, "middle",
     extra=f'transform="rotate(-90 {(PX0 + PX1) / 2} {(72 + ly(19)) / 2})"')

for i, name in enumerate(LEFT):
    y = ly(i)
    col = USED.get(name, "#3a4250" if name == "GND" else "#c9a24a")
    add(f'<rect x="{PX0 + 4}" y="{y - 6}" width="12" height="12" rx="2" fill="{col}"/>')
    label_col = USED.get(name) and INK or DIM
    text(PX0 + 22, y + 4, name, label_col, 11, family="mono")
    if name in CARRIER:
        text(PX0 - 10, y + 4, CARRIER[name], DIM, 11, "end", family="mono")

for j, name in enumerate(RIGHT):
    y = PIN_Y0 + j * PITCH
    col = USED.get(name, "#3a4250" if name == "GND" else "#c9a24a")
    add(f'<rect x="{PX1 - 16}" y="{y - 6}" width="12" height="12" rx="2" fill="{col}"/>')
    label_col = USED.get(name) and INK or DIM
    text(PX1 - 22, y + 4, name, label_col, 11, "end", family="mono")
    if name in CARRIER:
        text(PX1 + 10, y + 4, CARRIER[name], DIM, 11, family="mono")

# ── GPS (top left) ──
gx, gy, gw, gh = 70, 64, 250, 118
module(gx, gy, gw, gh, "Pico-GPS-L76B", "optional · 9600 baud UART", GPS)
module_pin(gx + gw, ly(0), "RX", GPS)
module_pin(gx + gw, ly(1), "TX", GPS)
text(gx + 14, gy + gh - 14, "5V → VSYS · GND", PWR, 12, family="mono")

# ── SD card (bottom left) ──
sx, sy, sw, sh = 70, 372, 250, 236
module(sx, sy, sw, sh, "MicroSD breakout", "SPI · FAT32 card", SD)
mosi_y, cs_y = 426, 470
module_pin(sx + sw, mosi_y, "MOSI (DI)", SD)
module_pin(sx + sw, ly(18), "SCK", SD)
module_pin(sx + sw, ly(19), "MISO (DO)", SD)
module_pin(sx + 150, sy + sh, "CS", SD, side="left")
text(sx + 14, sy + 70, "VCC → VBUS (5 V)", PWR, 12, family="mono")
text(sx + 14, sy + 88, "GND → GND", PWR, 12, family="mono")

# ── Pico-Audio (right): pins along the top, so the title goes at the bottom ──
ax, ay, aw, ah = 790, 384, 250, 256
add(f'<rect x="{ax}" y="{ay}" width="{aw}" height="{ah}" rx="10" fill="{PANEL}" '
    f'stroke="{AUDIO}" stroke-width="2"/>')
for p, label, header in (("GP21", "BCK", "GP27"), ("GP19", "DIN", "GP26"), ("GP17", "LRCK", "GP28")):
    module_pin(ax, ry(p), label, AUDIO, side="left")
    text(ax + 64, ry(p) + 4, f"(its {header} pin)", DIM, 11)
text(ax + 14, ay + ah - 70, "Pico-Audio", AUDIO, 15, weight=650)
text(ax + 14, ay + ah - 52, "PCM5101A version", DIM, 12)
text(ax + 14, ay + ah - 32, "VSYS · 3V3 · GND → same pins", PWR, 12, family="mono")
text(ax + 14, ay + ah - 14, "speaker 4 Ω / 3.5 mm jack", DIM, 12)

# ── Wires ──
jx = 400  # GP0 junction
wire([(jx, ly(0)), (gx + gw, ly(0))], GPS, dash=True)             # GP0 → GPS RX (unused)
wire([(PX0, ly(0)), (jx, ly(0)), (jx, mosi_y), (sx + sw, mosi_y)], SD)  # GP0 → SD MOSI
# GP1 ← GPS TX, hopping over the MOSI wire
hop = 7
add(f'<path d="M{gx + gw},{ly(1)} L{jx - hop},{ly(1)} A{hop},{hop} 0 0 1 {jx + hop},{ly(1)} '
    f'L{PX0},{ly(1)}" fill="none" stroke="{GPS}" stroke-width="3" stroke-linecap="round"/>')
add(f'<circle cx="{jx}" cy="{ly(0)}" r="5" fill="{SD}"/>')
wire([(PX0, ly(18)), (sx + sw, ly(18))], SD)                       # GP14 SCK
wire([(PX0, ly(19)), (sx + sw, ly(19))], SD)                       # GP15 MISO
lane_x, low_y = 1070, 660
wire([(PX1, ry("GP22")), (lane_x, ry("GP22")), (lane_x, low_y),   # GP22 CS, around the audio board
      (sx + 150, low_y), (sx + 150, sy + sh)], SD)
for p in ("GP21", "GP19", "GP17"):
    wire([(PX1, ry(p)), (ax, ry(p))], AUDIO)

# Wire labels
text(PX0 - 94, ly(0) - 8, "GP0 shared", SD, 11, "middle", family="mono")
text(lane_x - 8, ry("GP22") - 8, "SD CS", SD, 11, "end", family="mono")

# ── Legend ──
x, y = 70, 690
for colour, label in [(SD, "SD card"), (GPS, "GPS"), (AUDIO, "Audio"), (PWR, "Power")]:
    add(f'<rect x="{x}" y="{y - 11}" width="14" height="14" rx="3" fill="{colour}"/>')
    text(x + 20, y + 1, label, INK, 12)
    x += 100
text(x + 10, y + 1, "Grey labels: pins the matrix carrier board uses.", DIM, 12)
text(70, y + 22, "Dashed: the GPS RX pin shares GP0. The firmware never sends to the GPS, "
     "so the SD traffic it sees there is harmless.", DIM, 12)

add("</svg>")
dest = Path(__file__).resolve().parent.parent / "docs" / "img" / "wiring.svg"
dest.parent.mkdir(parents=True, exist_ok=True)
dest.write_text("\n".join(out) + "\n")
print(f"wrote {dest}")
