"""Wiring diagram for the LCTech 16-ch heating controller."""
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

OUT = Path(__file__).resolve().parent / "wiring.png"
FONT = Path(r"C:\Windows\Fonts\segoeui.ttf")
FONTB = Path(r"C:\Windows\Fonts\segoeuib.ttf")

BG = (255, 255, 255)
INK = (33, 37, 41)
MUTED = (73, 80, 87)
LINE = (52, 58, 64)
BLUE = (37, 99, 235)
ORANGE = (194, 65, 12)
GREEN = (21, 128, 61)
RED = (185, 28, 28)
BOARD = (219, 234, 254)
BOARD_STROKE = (29, 78, 216)
LOAD = (254, 243, 199)
LOAD_STROKE = (180, 83, 9)
SENS = (220, 252, 231)
SENS_STROKE = (21, 128, 61)
MAINS = (254, 226, 226)
MAINS_STROKE = (185, 28, 28)


def font(size, bold=False):
    p = FONTB if bold and FONTB.exists() else FONT
    if not p.exists():
        p = Path(r"C:\Windows\Fonts\arial.ttf")
    return ImageFont.truetype(str(p), size)


def tsize(draw, text, f):
    b = draw.textbbox((0, 0), text, font=f)
    return b[2] - b[0], b[3] - b[1]


def center(draw, cx, cy, text, f, fill=INK):
    lines = text.split("\n")
    lh = int(f.size * 1.22)
    y = cy - (lh * len(lines)) // 2
    for line in lines:
        w, h = tsize(draw, line, f)
        draw.text((cx - w // 2, y), line, font=f, fill=fill)
        y += lh


def box(draw, x, y, w, h, text, fill, stroke, f, radius=12):
    draw.rounded_rectangle((x, y, x + w, y + h), radius=radius, fill=fill, outline=stroke, width=2)
    center(draw, x + w // 2, y + h // 2, text, f)


def arrow(draw, pts, head="right"):
    draw.line(pts, fill=LINE, width=2)
    x, y = pts[-1]
    s = 8
    if head == "right":
        poly = [(x, y), (x - 2 * s, y - s), (x - 2 * s, y + s)]
    elif head == "left":
        poly = [(x, y), (x + 2 * s, y - s), (x + 2 * s, y + s)]
    elif head == "down":
        poly = [(x, y), (x - s, y - 2 * s), (x + s, y - 2 * s)]
    else:
        poly = [(x, y), (x - s, y + 2 * s), (x + s, y + 2 * s)]
    draw.polygon(poly, fill=LINE)


def main():
    w, h = 1400, 980
    img = Image.new("RGB", (w, h), BG)
    draw = ImageDraw.Draw(img)
    title = font(22, True)
    body = font(15)
    small = font(13)
    tiny = font(12)

    center(draw, w // 2, 28, "Heating controller wiring  —  LCTech ESP32 16-ch", title)

    # Inputs
    box(draw, 40, 80, 220, 70, "Szobatermosztát 1\ndry contact", SENS, SENS_STROKE, small)
    box(draw, 40, 180, 220, 70, "Szobatermosztát 2\ndry contact", SENS, SENS_STROKE, small)
    box(draw, 40, 300, 220, 110, "DS18B20\nfireplace pipe\nDQ  VDD  GND", SENS, SENS_STROKE, small)
    box(draw, 40, 450, 220, 70, "4.7 kΩ\nDQ to 3.3 V", SENS, SENS_STROKE, small)

    # Board
    box(draw, 430, 90, 460, 620, "", BOARD, BOARD_STROKE, body, 16)
    center(draw, 660, 120, "ESP32 16-ch relay board\n5 V DC jack  (board only)", font(16, True))
    draw.line([(450, 155), (870, 155)], fill=BOARD_STROKE, width=1)

    # GPIO table on board
    rows = [
        (180, "GPIO16  ←  TSTAT1"),
        (215, "GPIO17  ←  TSTAT2"),
        (250, "GPIO23  ←  DS18B20 DQ"),
        (285, "3.3 V   →  DS18B20 VDD + 4.7k"),
        (320, "GND     →  TSTAT1, TSTAT2, DS18B20"),
        (370, "Relay 1  COM/NO  →  Pump 1"),
        (405, "Relay 2  COM/NO  →  Pump 2"),
        (440, "Relay 3  COM/NO  →  Electric heater"),
        (475, "Relay 4  COM/NO  →  Valve (ON=fireplace)"),
        (525, "Relays 5–16 unused"),
        (560, "74HC595 is on-board — no extra wiring"),
    ]
    for y, text in rows:
        draw.text((470, y), text, font=small, fill=INK)

    box(draw, 500, 620, 320, 60, "5 V DC  →  VIN+   GND → VIN−\nDo not feed 12 V  (5 V coils)", LOAD, LOAD_STROKE, tiny)

    # Loads
    box(draw, 1060, 90, 280, 80, "Szivattyú 1\n230 V pump", LOAD, LOAD_STROKE, small)
    box(draw, 1060, 200, 280, 80, "Szivattyú 2\n230 V pump", LOAD, LOAD_STROKE, small)
    box(draw, 1060, 310, 280, 80, "Villany fűtés\n230 V heater", LOAD, LOAD_STROKE, small)
    box(draw, 1060, 420, 280, 100, "Vezérlő szelep\nON  = fireplace\nOFF = electric", LOAD, LOAD_STROKE, small)
    box(draw, 1060, 560, 280, 90, "230 V L / N / PE\nL switched by COM→NO\nN and PE stay direct", MAINS, MAINS_STROKE, tiny)

    # Input arrows
    arrow(draw, [(260, 115), (430, 195)], "right")
    draw.text((270, 95), "GPIO16", font=tiny, fill=GREEN)
    arrow(draw, [(260, 215), (430, 230)], "right")
    draw.text((270, 222), "GPIO17", font=tiny, fill=GREEN)
    arrow(draw, [(260, 340), (430, 265)], "right")
    draw.text((275, 300), "GPIO23 DQ", font=tiny, fill=GREEN)
    arrow(draw, [(260, 485), (430, 300)], "right")
    draw.text((280, 430), "3.3 V", font=tiny, fill=MUTED)

    # Output arrows
    arrow(draw, [(890, 200), (1060, 130)], "right")
    draw.text((900, 155), "R1", font=tiny, fill=ORANGE)
    arrow(draw, [(890, 235), (1060, 240)], "right")
    draw.text((900, 210), "R2", font=tiny, fill=ORANGE)
    arrow(draw, [(890, 270), (1060, 350)], "right")
    draw.text((900, 300), "R3", font=tiny, fill=ORANGE)
    arrow(draw, [(890, 305), (1060, 470)], "right")
    draw.text((900, 390), "R4", font=tiny, fill=ORANGE)

    note = (
        "Thermostats: dry contact only — no 230 V on GPIO.  "
        "DS18B20: 3-wire, 4.7 kΩ between DQ and 3.3 V.  "
        "Each load: 230 V L into relay COM, NO to the device, N/PE direct."
    )
    center(draw, w // 2, h - 40, note, tiny, MUTED)
    center(draw, w // 2, h - 18, "Programming (C3 SuperMini) is separate: GND, GPIO6→RX, GPIO7→TX. Not shown.", tiny, MUTED)

    img.save(OUT, "PNG", optimize=True)
    print("wrote", OUT)


if __name__ == "__main__":
    main()
