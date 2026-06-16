#!/usr/bin/env python3
"""Generate the Follow the Box 10x15cm perfboard placement diagram.

This script is intentionally self-contained: it does not read the old wiring
SVG/JSON assets, because those files were known to contain incorrect layouts.
The placement below is derived from the current BOM, pin map, wiring guide, and
box design notes.
"""

from __future__ import annotations

from dataclasses import dataclass, field
from math import hypot
from pathlib import Path


BOARD_W_MM = 100.0
BOARD_H_MM = 150.0
HOLE_PITCH_MM = 2.54
MARGIN_X = 16.0
MARGIN_Y = 18.0
FOOTER_H = 58.0
SVG_W = MARGIN_X * 2 + BOARD_W_MM + 100.0
SVG_H = MARGIN_Y * 2 + BOARD_H_MM + FOOTER_H


@dataclass(frozen=True)
class Pin:
    x: float
    y: float
    label: str


@dataclass
class Item:
    key: str
    x: float
    y: float
    w: float
    h: float
    title: str
    subtitle: str
    fill: str
    stroke: str
    kind: str
    pins: list[Pin] = field(default_factory=list)
    note: str = ""
    dashed: bool = False

    @property
    def cx(self) -> float:
        return self.x + self.w / 2

    @property
    def cy(self) -> float:
        return self.y + self.h / 2


ITEMS: list[Item] = [
    Item("tof_l", 5, 2, 22, 10, "TOF L", "VL53L1X left", "#dcefff", "#2b6f9e", "sensor"),
    Item("tof_c", 39, 2, 22, 10, "TOF C", "VL53L1X center", "#dcefff", "#2b6f9e", "sensor"),
    Item("tof_r", 73, 2, 22, 10, "TOF R", "VL53L1X right", "#dcefff", "#2b6f9e", "sensor"),
    Item("ds600", 4, 18, 30, 24, "DS600 RX", "CH1-CH5 PWM", "#ffe5d0", "#b65b16", "input"),
    Item("ds_div", 36, 17, 13, 26, "DS600", "5x dividers", "#f3ead8", "#7a6843", "level", note="10k/20k each"),
    Item("uwb", 68, 18, 28, 26, "UWB", "GC-P2304 3V3", "#dff3df", "#27733e", "sensor", note=">=50mm from DC-DC"),
    Item("tca", 5, 51, 26, 18, "TCA9548A", "I2C mux", "#dcefff", "#2b6f9e", "logic"),
    Item("i2c_pull", 33, 46, 13, 14, "I2C", "4.7k pullups", "#edf7df", "#4d7d25", "level"),
    Item("us_l", 0, 77, 16, 20, "US L", "HC-SR04 port", "#d7f2ec", "#16816b", "connector"),
    Item("us_r", 84, 77, 16, 20, "US R", "HC-SR04 port", "#d7f2ec", "#16816b", "connector"),
    Item("us_div", 78, 100, 18, 12, "US", "2x echo div", "#f3ead8", "#7a6843", "level", note="10k/20k each"),
    Item("imu", 38, 65, 24, 20, "JY61P IMU", "center, level, front arrow", "#f0def4", "#734094", "sensor", note="TX -> GPIO42"),
    Item("imu_div", 64, 65, 12, 18, "IMU", "TX divider", "#f3ead8", "#7a6843", "level", note="if TX is 5V"),
    Item("adc", 8, 103, 30, 15, "BAT ADC", "220k/10k -> GPIO1", "#f9dddd", "#aa3939", "safety"),
    Item("estop_fb", 8, 121, 32, 16, "ESTOP FB", "GPIO21, 2nd NC/opto", "#ffeddc", "#b34b24", "safety"),
    Item("esp32", 42, 87, 34, 32, "ESP32-S3", "DevKitC-1, USB to rear", "#dceafa", "#1f5d99", "controller"),
    Item("dcdc", 4, 140, 28, 10, "DC-DC", "36V -> 5V", "#ffe5d0", "#b65b16", "power"),
    Item("filter", 34, 140, 16, 10, "5V CAP", "470uF + 0.1uF", "#eff1f3", "#68737c", "power"),
    Item("pwm_l", 52, 139, 20, 11, "PWM L", "GPIO12 -> 0-5V", "#fff2d7", "#d17900", "output", note="10k pulldown"),
    Item("pwm_r", 74, 139, 20, 11, "PWM R", "GPIO13 -> 0-5V", "#fff2d7", "#d17900", "output", note="10k pulldown"),
    Item("mos", 52, 124, 42, 12, "MOS/OPTO x4", "brake, rev L/R, enable", "#fbe2df", "#96362f", "output", note="GPIO14/15/16/39"),
    Item("cam_port", 78, 50, 18, 15, "CAM", "5V/GND port", "#ece7ff", "#5c50a6", "connector", dashed=True),
    Item("rails", 47, 48, 17, 10, "BUS", "5V GND 3V3", "#edf0f5", "#667085", "power"),
]


EXTERNAL_ITEMS = [
    ("Not on perfboard", "F1 main fuse, main switch, motor phase wires, controller BAT+/BAT-"),
    ("Rear harness", "BAT IN/F2, CTRL L/R, e-lock/ESTOP chain leave by rear edge"),
    ("Front/side ports", "TOF and ultrasonic modules may be panel-mounted; their JST headers are shown on the board"),
]


def fmt(value: float) -> str:
    return f"{value:.2f}".rstrip("0").rstrip(".")


def sx(x: float) -> float:
    return MARGIN_X + x


def sy(y: float) -> float:
    return MARGIN_Y + y


def esc(text: str) -> str:
    return (
        text.replace("&", "&amp;")
        .replace("<", "&lt;")
        .replace(">", "&gt;")
        .replace('"', "&quot;")
    )


def validate() -> None:
    errors: list[str] = []
    for item in ITEMS:
        if item.x < -0.01 or item.y < -0.01:
            errors.append(f"{item.key} starts outside board")
        if item.x + item.w > BOARD_W_MM + 0.01:
            errors.append(f"{item.key} exceeds board width")
        if item.y + item.h > BOARD_H_MM + 0.01:
            errors.append(f"{item.key} exceeds board height")

    allowed_touch = {
        frozenset(("dcdc", "filter")),
        frozenset(("pwm_l", "pwm_r")),
    }
    for index, item in enumerate(ITEMS):
        for other in ITEMS[index + 1 :]:
            overlap_x = min(item.x + item.w, other.x + other.w) - max(item.x, other.x)
            overlap_y = min(item.y + item.h, other.y + other.h) - max(item.y, other.y)
            if overlap_x > 0.4 and overlap_y > 0.4 and frozenset((item.key, other.key)) not in allowed_touch:
                errors.append(f"{item.key} overlaps {other.key}: {overlap_x:.1f}x{overlap_y:.1f}mm")

    center_item = next(item for item in ITEMS if item.key == "imu")
    center_offset = hypot(center_item.cx - BOARD_W_MM / 2, center_item.cy - BOARD_H_MM / 2)
    if center_offset > 2.5:
        errors.append(f"JY61P center offset is {center_offset:.1f}mm, expected <=2.5mm")

    uwb = next(item for item in ITEMS if item.key == "uwb")
    dcdc = next(item for item in ITEMS if item.key == "dcdc")
    uwb_dcdc_distance = hypot(uwb.cx - dcdc.cx, uwb.cy - dcdc.cy)
    if uwb_dcdc_distance < 50:
        errors.append(f"UWB to DC-DC distance is {uwb_dcdc_distance:.1f}mm, expected >=50mm")

    if errors:
        for error in errors:
            print(f"ERROR: {error}")
        raise SystemExit(1)


def wire(svg: list[str], start: str, end: str, color: str, label: str = "") -> None:
    a = next(item for item in ITEMS if item.key == start)
    b = next(item for item in ITEMS if item.key == end)
    x1, y1 = sx(a.cx), sy(a.cy)
    x2, y2 = sx(b.cx), sy(b.cy)
    mid_x = (x1 + x2) / 2
    svg.append(
        f'<path d="M{fmt(x1)} {fmt(y1)} C{fmt(mid_x)} {fmt(y1)} {fmt(mid_x)} {fmt(y2)} {fmt(x2)} {fmt(y2)}" '
        f'stroke="{color}" stroke-width="0.7" fill="none" opacity="0.55"/>'
    )
    if label:
        svg.append(
            f'<text x="{fmt((x1 + x2) / 2)}" y="{fmt((y1 + y2) / 2 - 1)}" '
            f'font-size="3" fill="{color}" text-anchor="middle">{esc(label)}</text>'
        )


def render_svg() -> str:
    validate()
    svg: list[str] = []
    svg.append(
        f'<svg viewBox="0 0 {fmt(SVG_W)} {fmt(SVG_H)}" xmlns="http://www.w3.org/2000/svg" '
        'role="img" aria-labelledby="title desc" '
        'style="font-family:Segoe UI,Microsoft YaHei,Arial,sans-serif">'
    )
    svg.append('<title id="title">Follow the Box 10x15cm perfboard layout</title>')
    svg.append('<desc id="desc">Perfboard placement diagram generated from current BOM, wiring, pin map, and box design.</desc>')
    svg.append('<defs>')
    svg.append('<pattern id="holes" width="2.54" height="2.54" patternUnits="userSpaceOnUse"><circle cx="0" cy="0" r="0.28" fill="#806f55" opacity="0.34"/></pattern>')
    svg.append('<marker id="arrow" viewBox="0 0 10 10" refX="8" refY="5" markerWidth="4" markerHeight="4" orient="auto-start-reverse"><path d="M0 0 L10 5 L0 10 z" fill="#58606a"/></marker>')
    svg.append('</defs>')
    svg.append(f'<rect x="0" y="0" width="{fmt(SVG_W)}" height="{fmt(SVG_H)}" fill="#f7f5ef"/>')
    svg.append(f'<text x="{fmt(MARGIN_X)}" y="10" font-size="6" font-weight="700" fill="#171717">Follow the Box - 10x15cm perfboard placement</text>')
    svg.append(f'<text x="{fmt(MARGIN_X)}" y="16" font-size="3.8" fill="#525b66">Derived from CURRENT-PARTS-LIST, PIN-MAP-V1, CURRENT-WIRING-AI, CURRENT-BOX-DESIGN. Old SVG/JSON wiring diagrams are not used.</text>')

    board_x, board_y = sx(0), sy(0)
    svg.append(f'<rect x="{fmt(board_x)}" y="{fmt(board_y)}" width="{fmt(BOARD_W_MM)}" height="{fmt(BOARD_H_MM)}" rx="2" fill="#e9dfc8" stroke="#7c6748" stroke-width="1.3"/>')
    svg.append(f'<rect x="{fmt(board_x)}" y="{fmt(board_y)}" width="{fmt(BOARD_W_MM)}" height="{fmt(BOARD_H_MM)}" fill="url(#holes)" opacity="0.9"/>')
    svg.append(f'<path d="M{fmt(sx(3))} {fmt(sy(0))} L{fmt(sx(50))} {fmt(sy(-8))} L{fmt(sx(97))} {fmt(sy(0))}" fill="none" stroke="#58606a" stroke-width="0.8" marker-end="url(#arrow)"/>')
    svg.append(f'<text x="{fmt(sx(50))}" y="{fmt(sy(-10))}" font-size="4" text-anchor="middle" fill="#30363d">FRONT: TOF / UWB / camera side</text>')
    svg.append(f'<text x="{fmt(sx(50))}" y="{fmt(sy(155))}" font-size="4" text-anchor="middle" fill="#30363d">REAR: battery, controller and USB service side</text>')
    svg.append(f'<line x1="{fmt(sx(50))}" y1="{fmt(sy(0))}" x2="{fmt(sx(50))}" y2="{fmt(sy(150))}" stroke="#b7aa8d" stroke-width="0.3" stroke-dasharray="2 2"/>')
    svg.append(f'<line x1="{fmt(sx(0))}" y1="{fmt(sy(75))}" x2="{fmt(sx(100))}" y2="{fmt(sy(75))}" stroke="#b7aa8d" stroke-width="0.3" stroke-dasharray="2 2"/>')

    for start, end, color, label in [
        ("ds600", "ds_div", "#a35d15", "CH1-5"),
        ("ds_div", "esp32", "#a35d15", "GPIO4-8"),
        ("tca", "esp32", "#2b6f9e", "SDA10/SCL11"),
        ("tof_l", "tca", "#2b6f9e", "CH1"),
        ("tof_c", "tca", "#2b6f9e", "CH0"),
        ("tof_r", "tca", "#2b6f9e", "CH2"),
        ("us_l", "esp32", "#16816b", "GPIO9/40"),
        ("us_r", "us_div", "#16816b", "ECHO"),
        ("us_div", "esp32", "#16816b", "GPIO41"),
        ("imu", "imu_div", "#734094", "TX"),
        ("imu_div", "esp32", "#734094", "GPIO42"),
        ("uwb", "esp32", "#27733e", "UART17/18"),
        ("adc", "esp32", "#aa3939", "GPIO1"),
        ("estop_fb", "esp32", "#b34b24", "GPIO21"),
        ("esp32", "pwm_l", "#d17900", "GPIO12"),
        ("esp32", "pwm_r", "#d17900", "GPIO13"),
        ("esp32", "mos", "#96362f", "14/15/16/39"),
        ("dcdc", "filter", "#68737c", "5V"),
        ("filter", "rails", "#68737c", "bus"),
        ("rails", "esp32", "#68737c", "5V/3V3/GND"),
        ("cam_port", "rails", "#5c50a6", "5V"),
    ]:
        wire(svg, start, end, color, label)

    for item in ITEMS:
        rect_x, rect_y = sx(item.x), sy(item.y)
        dash = ' stroke-dasharray="2 1.6"' if item.dashed else ""
        svg.append(
            f'<rect x="{fmt(rect_x)}" y="{fmt(rect_y)}" width="{fmt(item.w)}" height="{fmt(item.h)}" rx="1.5" '
            f'fill="{item.fill}" stroke="{item.stroke}" stroke-width="0.9"{dash}/>'
        )
        svg.append(f'<text x="{fmt(rect_x + item.w / 2)}" y="{fmt(rect_y + 4.2)}" font-size="3.6" font-weight="700" fill="#111" text-anchor="middle">{esc(item.title)}</text>')
        svg.append(f'<text x="{fmt(rect_x + item.w / 2)}" y="{fmt(rect_y + 8.2)}" font-size="2.7" fill="#39404a" text-anchor="middle">{esc(item.subtitle)}</text>')
        if item.note:
            svg.append(f'<text x="{fmt(rect_x + item.w / 2)}" y="{fmt(rect_y + item.h - 2)}" font-size="2.5" fill="#8a2020" text-anchor="middle">{esc(item.note)}</text>')

    svg.append(f'<rect x="{fmt(sx(104))}" y="{fmt(sy(5))}" width="86" height="78" rx="2" fill="#ffffff" stroke="#d4d7dc"/>')
    svg.append(f'<text x="{fmt(sx(108))}" y="{fmt(sy(13))}" font-size="4.2" font-weight="700" fill="#202428">Placement rules</text>')
    rules = [
        "JY61P center is within 2.5mm of board center; keep it level and mark FRONT.",
        "UWB stays in the front-right RF corner; DC-DC stays rear-left; spacing check >=50mm.",
        "All 5V-to-ESP32 inputs have independent dividers or level shifting.",
        "GPIO12/13/14/15/16/39 keep external 10k pulldowns near the output modules.",
        "Motor main current, controller BAT+/BAT-, phase wires and F1 never cross this board.",
        "GPIO21 reads only 3.3V from second NC contact or optocoupler isolation.",
    ]
    for index, rule in enumerate(rules):
        svg.append(f'<text x="{fmt(sx(108))}" y="{fmt(sy(21 + index * 8))}" font-size="3.2" fill="#39404a">{index + 1}. {esc(rule)}</text>')

    svg.append(f'<rect x="{fmt(sx(104))}" y="{fmt(sy(91))}" width="86" height="49" rx="2" fill="#fffaf0" stroke="#e3c36a"/>')
    svg.append(f'<text x="{fmt(sx(108))}" y="{fmt(sy(99))}" font-size="4.2" font-weight="700" fill="#4b3812">External / not plugged into perfboard</text>')
    for index, (label, text) in enumerate(EXTERNAL_ITEMS):
        y = sy(107 + index * 10)
        svg.append(f'<text x="{fmt(sx(108))}" y="{fmt(y)}" font-size="3.3" font-weight="700" fill="#5f4b1b">{esc(label)}:</text>')
        svg.append(f'<text x="{fmt(sx(136))}" y="{fmt(y)}" font-size="3.2" fill="#5f4b1b">{esc(text)}</text>')

    legend_y = sy(164)
    svg.append(f'<rect x="{fmt(MARGIN_X)}" y="{fmt(legend_y)}" width="{fmt(SVG_W - MARGIN_X * 2)}" height="42" rx="2" fill="#ffffff" stroke="#d6d8dd"/>')
    svg.append(f'<text x="{fmt(MARGIN_X + 4)}" y="{fmt(legend_y + 7)}" font-size="4" font-weight="700" fill="#202428">Board-mounted BOM shown in this diagram</text>')
    bom = [
        "ESP32-S3-DevKitC-1, DC-DC 36V->5V, 5V/GND/3V3 bus, 470uF + 0.1uF filter cap",
        "DS600 receiver with 5 independent PWM dividers; JY61P centered with optional TX divider",
        "TCA9548A with 4.7k I2C pullups; front TOF headers/modules; left/right HC-SR04 ports and echo dividers",
        "PWM->0-5V L/R modules; MOS/opto x4 driver board; BAT ADC 220k/10k; ESTOP feedback input",
    ]
    for index, line in enumerate(bom):
        svg.append(f'<text x="{fmt(MARGIN_X + 4)}" y="{fmt(legend_y + 15 + index * 6)}" font-size="3.4" fill="#39404a">- {esc(line)}</text>')
    svg.append(f'<text x="{fmt(MARGIN_X + 4)}" y="{fmt(legend_y + 39)}" font-size="3" fill="#6b7280">Generator validates: board bounds, component overlap, JY61P center, and UWB/DC-DC spacing.</text>')
    svg.append('</svg>')
    return "\n".join(svg)


def render_html(svg: str) -> str:
    return f"""<!doctype html>
<html lang=\"zh-CN\">
<head>
  <meta charset=\"utf-8\">
  <meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">
  <title>Follow the Box 10x15cm 洞洞板布局</title>
  <style>
    :root {{ color-scheme: light; }}
    body {{ margin: 0; padding: 18px; background: #f1eee6; font-family: Segoe UI, Microsoft YaHei, Arial, sans-serif; }}
    main {{ max-width: 1280px; margin: 0 auto; }}
    svg {{ display: block; width: 100%; height: auto; background: #f7f5ef; border: 1px solid #d9d5ca; box-shadow: 0 8px 26px rgba(30, 24, 15, 0.12); }}
  </style>
</head>
<body>
  <main>
{svg}
  </main>
</body>
</html>
"""


def main() -> None:
    root = Path(__file__).resolve().parent
    svg = render_svg()
    (root / "PERFBOARD-LAYOUT.svg").write_text(svg, encoding="utf-8")
    (root / "PERFBOARD-LAYOUT.html").write_text(render_html(svg), encoding="utf-8")
    print("OK: generated PERFBOARD-LAYOUT.svg and PERFBOARD-LAYOUT.html")


if __name__ == "__main__":
    main()
