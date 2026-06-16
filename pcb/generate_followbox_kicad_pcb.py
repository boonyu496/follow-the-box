from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
from uuid import uuid5, NAMESPACE_URL
import zipfile


PROJECT = "FollowBox_LowVoltage_Interface_PCB_v1"
OUT_DIR = Path(__file__).resolve().parent / "generated"
BOARD = OUT_DIR / f"{PROJECT}.kicad_pcb"
SCHEMATIC = OUT_DIR / f"{PROJECT}.kicad_sch"
PROJECT_FILE = OUT_DIR / f"{PROJECT}.kicad_pro"
ZIP = OUT_DIR / f"{PROJECT}.zip"


def tid(name: str) -> str:
    return str(uuid5(NAMESPACE_URL, f"followbox-pcb-v1/{name}"))


@dataclass(frozen=True)
class PadDef:
    number: str
    net: str
    label: str | None = None


class PcbBuilder:
    def __init__(self) -> None:
        self.nets: dict[str, int] = {"": 0}
        self.items: list[str] = []

    def net_id(self, name: str) -> int:
        if name not in self.nets:
            self.nets[name] = len(self.nets)
        return self.nets[name]

    def net_expr(self, name: str) -> str:
        return f'(net {self.net_id(name)} "{name}")'

    def add(self, text: str) -> None:
        self.items.append(text)

    def gr_line(self, name: str, x1: float, y1: float, x2: float, y2: float, layer: str = "F.SilkS", width: float = 0.15) -> None:
        self.add(f'  (gr_line (start {x1:.2f} {y1:.2f}) (end {x2:.2f} {y2:.2f}) (layer "{layer}") (width {width:.2f}) (tstamp {tid(name)}))')

    def gr_rect(self, name: str, x1: float, y1: float, x2: float, y2: float, layer: str = "F.SilkS", width: float = 0.15) -> None:
        self.gr_line(name + "-a", x1, y1, x2, y1, layer, width)
        self.gr_line(name + "-b", x2, y1, x2, y2, layer, width)
        self.gr_line(name + "-c", x2, y2, x1, y2, layer, width)
        self.gr_line(name + "-d", x1, y2, x1, y1, layer, width)

    def gr_text(self, name: str, text: str, x: float, y: float, size: float = 1.4, layer: str = "F.SilkS", rot: float = 0) -> None:
        escaped = text.replace('"', "'")
        self.add(
            f'  (gr_text "{escaped}" (at {x:.2f} {y:.2f} {rot:.1f}) (layer "{layer}") (tstamp {tid(name)})\n'
            f'    (effects (font (size {size:.2f} {size:.2f}) (thickness 0.15)) (justify left))\n'
            f'  )'
        )

    def pin_header(self, ref: str, value: str, x: float, y: float, pads: list[PadDef], cols: int = 1, rot: float = 0) -> None:
        rows = (len(pads) + cols - 1) // cols
        lines = [
            f'  (footprint "FollowBox:PinHeader_{cols}x{rows}_P2.54mm" (layer "F.Cu")',
            f'    (tstamp {tid(ref)})',
            f'    (at {x:.2f} {y:.2f} {rot:.1f})',
            '    (attr through_hole)',
            f'    (fp_text reference "{ref}" (at 0 -3.00 {rot:.1f}) (layer "F.SilkS") (effects (font (size 1.00 1.00) (thickness 0.15))))',
            f'    (fp_text value "{value}" (at 0 {rows * 2.54 + 2.00:.2f} {rot:.1f}) (layer "F.Fab") (effects (font (size 0.80 0.80) (thickness 0.10))))',
        ]
        width = max(1, cols - 1) * 2.54 + 2.8
        height = max(1, rows - 1) * 2.54 + 2.8
        x0, x1 = -1.4, -1.4 + width
        y0, y1 = -1.4, -1.4 + height
        lines += [
            f'    (fp_line (start {x0:.2f} {y0:.2f}) (end {x1:.2f} {y0:.2f}) (stroke (width 0.12) (type solid)) (layer "F.SilkS") (tstamp {tid(ref+"-s1")}))',
            f'    (fp_line (start {x1:.2f} {y0:.2f}) (end {x1:.2f} {y1:.2f}) (stroke (width 0.12) (type solid)) (layer "F.SilkS") (tstamp {tid(ref+"-s2")}))',
            f'    (fp_line (start {x1:.2f} {y1:.2f}) (end {x0:.2f} {y1:.2f}) (stroke (width 0.12) (type solid)) (layer "F.SilkS") (tstamp {tid(ref+"-s3")}))',
            f'    (fp_line (start {x0:.2f} {y1:.2f}) (end {x0:.2f} {y0:.2f}) (stroke (width 0.12) (type solid)) (layer "F.SilkS") (tstamp {tid(ref+"-s4")}))',
        ]
        for i, pad in enumerate(pads):
            col = i % cols
            row = i // cols
            px, py = col * 2.54, row * 2.54
            shape = "rect" if i == 0 else "oval"
            label = (pad.label or pad.net).replace('"', "'")
            lines.append(
                f'    (pad "{pad.number}" thru_hole {shape} (at {px:.2f} {py:.2f}) (size 1.70 1.70) (drill 1.00) '
                f'(layers "*.Cu" "*.Mask") {self.net_expr(pad.net)} (pinfunction "{label}") (pintype "passive") (tstamp {tid(ref+"-pad"+pad.number)}))'
            )
        lines.append('  )')
        self.add("\n".join(lines))

    def resistor(self, ref: str, value: str, x: float, y: float, net1: str, net2: str, rot: float = 0) -> None:
        lines = [
            '  (footprint "FollowBox:R_0603_1608Metric" (layer "F.Cu")',
            f'    (tstamp {tid(ref)})',
            f'    (at {x:.2f} {y:.2f} {rot:.1f})',
            '    (attr smd)',
            f'    (fp_text reference "{ref}" (at 0 -1.35 {rot:.1f}) (layer "F.SilkS") (effects (font (size 0.70 0.70) (thickness 0.10))))',
            f'    (fp_text value "{value}" (at 0 1.35 {rot:.1f}) (layer "F.Fab") (effects (font (size 0.60 0.60) (thickness 0.08))))',
            f'    (fp_line (start -1.25 -0.65) (end 1.25 -0.65) (stroke (width 0.10) (type solid)) (layer "F.SilkS") (tstamp {tid(ref+"-s1")}))',
            f'    (fp_line (start -1.25 0.65) (end 1.25 0.65) (stroke (width 0.10) (type solid)) (layer "F.SilkS") (tstamp {tid(ref+"-s2")}))',
            f'    (pad "1" smd roundrect (at -0.80 0.00) (size 0.80 0.95) (layers "F.Cu" "F.Paste" "F.Mask") (roundrect_rratio 0.20) {self.net_expr(net1)} (pintype "passive") (tstamp {tid(ref+"-1")}))',
            f'    (pad "2" smd roundrect (at 0.80 0.00) (size 0.80 0.95) (layers "F.Cu" "F.Paste" "F.Mask") (roundrect_rratio 0.20) {self.net_expr(net2)} (pintype "passive") (tstamp {tid(ref+"-2")}))',
            '  )',
        ]
        self.add("\n".join(lines))

    def capacitor(self, ref: str, value: str, x: float, y: float, net1: str, net2: str, rot: float = 0) -> None:
        self.resistor(ref, value, x, y, net1, net2, rot)

    def mounting_hole(self, ref: str, x: float, y: float) -> None:
        self.add(
            f'  (footprint "FollowBox:MountingHole_3.2mm" (layer "F.Cu")\n'
            f'    (tstamp {tid(ref)})\n'
            f'    (at {x:.2f} {y:.2f})\n'
            f'    (attr exclude_from_pos_files exclude_from_bom)\n'
            f'    (fp_text reference "{ref}" (at 0 -4.20) (layer "F.SilkS") (effects (font (size 1.00 1.00) (thickness 0.15))))\n'
            f'    (fp_circle (center 0 0) (end 3.20 0) (stroke (width 0.12) (type solid)) (fill none) (layer "F.SilkS") (tstamp {tid(ref+"-circle")}))\n'
            f'    (pad "1" np_thru_hole circle (at 0 0) (size 3.20 3.20) (drill 3.20) (layers "*.Cu" "*.Mask") (tstamp {tid(ref+"-pad")}))\n'
            f'  )'
        )

    def render(self) -> str:
        net_lines = [f'  (net {idx} "{name}")' for name, idx in sorted(self.nets.items(), key=lambda item: item[1])]
        return "\n".join(
            [
                '(kicad_pcb (version 20211014) (generator "followbox_kicad_generator")',
                '  (general (thickness 1.6))',
                '  (paper "A4")',
                '  (layers',
                '    (0 "F.Cu" signal)',
                '    (31 "B.Cu" signal)',
                '    (32 "B.Adhes" user)',
                '    (33 "F.Adhes" user)',
                '    (34 "B.Paste" user)',
                '    (35 "F.Paste" user)',
                '    (36 "B.SilkS" user)',
                '    (37 "F.SilkS" user)',
                '    (38 "B.Mask" user)',
                '    (39 "F.Mask" user)',
                '    (44 "Edge.Cuts" user)',
                '    (45 "Margin" user)',
                '    (46 "B.CrtYd" user)',
                '    (47 "F.CrtYd" user)',
                '    (48 "B.Fab" user)',
                '    (49 "F.Fab" user)',
                '  )',
                '  (setup',
                '    (pad_to_mask_clearance 0)',
                '    (pcbplotparams (layerselection 0x00010fc_ffffffff) (plot_on_all_layers_selection 0x0000000_00000000) (disableapertmacros false) (usegerberextensions false) (usegerberattributes true) (usegerberadvancedattributes true) (creategerberjobfile true) (dashed_line_dash_ratio 12.000000) (dashed_line_gap_ratio 3.000000) (svgprecision 4) (plotframeref false) (viasonmask false) (mode 1) (useauxorigin false) (hpglpennumber 1) (hpglpenspeed 20) (hpglpendiameter 15.000000) (dxfpolygonmode true) (dxfimperialunits true) (dxfusepcbnewfont true) (psnegative false) (psa4output false) (plotreference true) (plotvalue true) (plotinvisibletext false) (sketchpadsonfab false) (subtractmaskfromsilk false) (outputformat 1) (mirror false) (drillshape 1) (scaleselection 1) (outputdirectory "gerbers"))',
                '  )',
                *net_lines,
                *self.items,
                ')',
                '',
            ]
        )


def pad(number: int, net: str, label: str | None = None) -> PadDef:
    return PadDef(str(number), net, label)


def build() -> PcbBuilder:
    pcb = PcbBuilder()

    # Board outline: 160 mm x 100 mm, matching the signal-control-box class in CURRENT-BOX-DESIGN.md.
    pcb.gr_rect("edge", 0, 0, 160, 100, "Edge.Cuts", 0.10)
    pcb.gr_text("title", "FollowBox low-voltage interface PCB v1 - NO 36V/MOTOR MAIN CURRENT", 6, 6, 1.6)
    pcb.gr_text("front", "FRONT / SENSOR SIDE", 55, 96, 1.3)
    pcb.gr_text("rear", "REAR / POWER + CONTROLLER SIGNAL SIDE", 40, 10, 1.3)

    # Functional zones.
    pcb.gr_rect("zone-sensor", 4, 76, 156, 97, "F.SilkS", 0.12)
    pcb.gr_text("zone-sensor-text", "Sensor interfaces: TOF / ultrasonic / UWB / IMU", 7, 80, 1.0)
    pcb.gr_rect("zone-mcu", 42, 28, 118, 70, "F.SilkS", 0.12)
    pcb.gr_text("zone-mcu-text", "MCU signal header to ESP32-S3 DevKitC-1", 45, 32, 1.0)
    pcb.gr_rect("zone-drive", 4, 4, 156, 25, "F.SilkS", 0.12)
    pcb.gr_text("zone-drive-text", "Power sense + throttle module + MOS/opto input area", 7, 18, 1.0)

    for ref, x, y in [("H1", 5, 5), ("H2", 155, 5), ("H3", 5, 95), ("H4", 155, 95)]:
        pcb.mounting_hole(ref, x, y)

    # Central MCU wiring header. This avoids guessing the exact DevKitC physical pin order.
    mcu_pads = [
        pad(1, "GND"), pad(2, "+5V"),
        pad(3, "+3V3"), pad(4, "GPIO1_BAT_ADC"),
        pad(5, "GPIO2_FAULT_IN"), pad(6, "GPIO4_RC_CH1"),
        pad(7, "GPIO5_RC_CH2"), pad(8, "GPIO6_RC_CH3"),
        pad(9, "GPIO7_RC_CH4"), pad(10, "GPIO8_RC_CH5"),
        pad(11, "GPIO9_US_TRIG"), pad(12, "GPIO10_I2C_SDA"),
        pad(13, "GPIO11_I2C_SCL"), pad(14, "GPIO12_PWM_L"),
        pad(15, "GPIO13_PWM_R"), pad(16, "GPIO14_BRAKE_OUT"),
        pad(17, "GPIO15_REV_L"), pad(18, "GPIO16_REV_R"),
        pad(19, "GPIO17_UWB_TX"), pad(20, "GPIO18_UWB_RX"),
        pad(21, "GPIO21_ESTOP_FB"), pad(22, "GPIO39_ENABLE_OUT"),
        pad(23, "GPIO40_US_ECHO_L"), pad(24, "GPIO41_US_ECHO_R"),
        pad(25, "GPIO42_IMU_RX"), pad(26, "GND"),
    ]
    pcb.pin_header("J_MCU", "ESP32-S3 signal harness", 62, 35, mcu_pads, cols=2)

    # RC receiver input and dividers: 10k high / 20k low.
    pcb.pin_header("J_RC", "DS600 CH1-CH5 + power", 8, 34, [
        pad(1, "+5V"), pad(2, "GND"),
        pad(3, "RC_CH1_RAW"), pad(4, "RC_CH2_RAW"), pad(5, "RC_CH3_RAW"),
        pad(6, "RC_CH4_RAW"), pad(7, "RC_CH5_RAW"),
    ])
    for idx, gpio in enumerate([4, 5, 6, 7, 8], start=1):
        y = 35 + idx * 3.5
        pcb.resistor(f"R_RC{idx}A", "10k", 28, y, f"RC_CH{idx}_RAW", f"GPIO{gpio}_RC_CH{idx}")
        pcb.resistor(f"R_RC{idx}B", "20k", 38, y, f"GPIO{gpio}_RC_CH{idx}", "GND")

    # Sensor connectors.
    pcb.pin_header("J_UWB", "GC-P2304 UWB", 8, 80, [pad(1, "+3V3"), pad(2, "GND"), pad(3, "GPIO17_UWB_TX", "UWB_RX"), pad(4, "GPIO18_UWB_RX", "UWB_TX")])
    pcb.pin_header("J_IMU", "JY61P IMU", 30, 80, [pad(1, "+5V"), pad(2, "GND"), pad(3, "IMU_TX_RAW"), pad(4, "")])
    pcb.resistor("R_IMU1", "10k", 30, 72, "IMU_TX_RAW", "GPIO42_IMU_RX")
    pcb.resistor("R_IMU2", "20k", 40, 72, "GPIO42_IMU_RX", "GND")

    # TCA9548A module and three VL53L1X downstream ports.
    pcb.pin_header("J_TCA", "TCA9548A module", 52, 80, [
        pad(1, "+3V3"), pad(2, "GND"), pad(3, "GPIO10_I2C_SDA", "SDA"), pad(4, "GPIO11_I2C_SCL", "SCL"),
        pad(5, "TCA_SD0"), pad(6, "TCA_SC0"), pad(7, "TCA_SD1"), pad(8, "TCA_SC1"), pad(9, "TCA_SD2"), pad(10, "TCA_SC2"),
    ], cols=2)
    pcb.resistor("R_I2C1", "4.7k", 56, 72, "+3V3", "GPIO10_I2C_SDA")
    pcb.resistor("R_I2C2", "4.7k", 66, 72, "+3V3", "GPIO11_I2C_SCL")
    pcb.pin_header("J_TOF_C", "TOF center", 86, 80, [pad(1, "+3V3"), pad(2, "GND"), pad(3, "TCA_SD0", "SDA"), pad(4, "TCA_SC0", "SCL")])
    pcb.pin_header("J_TOF_L", "TOF left", 108, 80, [pad(1, "+3V3"), pad(2, "GND"), pad(3, "TCA_SD1", "SDA"), pad(4, "TCA_SC1", "SCL")])
    pcb.pin_header("J_TOF_R", "TOF right", 130, 80, [pad(1, "+3V3"), pad(2, "GND"), pad(3, "TCA_SD2", "SDA"), pad(4, "TCA_SC2", "SCL")])

    # Ultrasonic connectors and Echo dividers.
    pcb.pin_header("J_US_L", "HC-SR04 left", 126, 34, [pad(1, "+5V"), pad(2, "GND"), pad(3, "GPIO9_US_TRIG"), pad(4, "US_ECHO_L_RAW")])
    pcb.pin_header("J_US_R", "HC-SR04 right", 148, 34, [pad(1, "+5V"), pad(2, "GND"), pad(3, "GPIO9_US_TRIG"), pad(4, "US_ECHO_R_RAW")])
    pcb.resistor("R_USL1", "10k", 126, 58, "US_ECHO_L_RAW", "GPIO40_US_ECHO_L")
    pcb.resistor("R_USL2", "20k", 136, 58, "GPIO40_US_ECHO_L", "GND")
    pcb.resistor("R_USR1", "10k", 126, 64, "US_ECHO_R_RAW", "GPIO41_US_ECHO_R")
    pcb.resistor("R_USR2", "20k", 136, 64, "GPIO41_US_ECHO_R", "GND")

    # Power input/sense and estop feedback.
    pcb.pin_header("J_PWR", "5V bus + battery sense", 16, 10, [pad(1, "+5V"), pad(2, "GND"), pad(3, "+3V3"), pad(4, "BAT_SENSE_RAW")], rot=90)
    pcb.resistor("R_BAT1", "220k", 42, 10, "BAT_SENSE_RAW", "GPIO1_BAT_ADC")
    pcb.resistor("R_BAT2", "10k", 52, 10, "GPIO1_BAT_ADC", "GND")
    pcb.capacitor("C_BAT1", "0.1uF", 62, 10, "GPIO1_BAT_ADC", "GND")
    pcb.pin_header("J_ESTOP", "ESTOP second NC", 80, 10, [pad(1, "GPIO21_ESTOP_FB"), pad(2, "GND")], rot=90)
    pcb.resistor("R_ESTOP", "10k", 92, 10, "+3V3", "GPIO21_ESTOP_FB")

    # Throttle PWM module interfaces and controller signal connectors.
    pcb.pin_header("J_PWM_L", "Left PWM-0/5V module", 108, 8, [pad(1, "GPIO12_PWM_L"), pad(2, "GND"), pad(3, "CTRL_L_5V"), pad(4, "THR_L_OUT")], rot=90)
    pcb.pin_header("J_PWM_R", "Right PWM-0/5V module", 130, 8, [pad(1, "GPIO13_PWM_R"), pad(2, "GND"), pad(3, "CTRL_R_5V"), pad(4, "THR_R_OUT")], rot=90)
    pcb.resistor("R_PWML_PD", "10k", 110, 22, "GPIO12_PWM_L", "GND")
    pcb.resistor("R_PWMR_PD", "10k", 130, 22, "GPIO13_PWM_R", "GND")
    pcb.pin_header("J_CTRL_L", "CTRL L signals", 112, 48, [pad(1, "CTRL_L_5V"), pad(2, "CTRL_L_GND"), pad(3, "THR_L_OUT"), pad(4, "BRAKE_SW"), pad(5, "REV_L_SW"), pad(6, "ENABLE_SW")])
    pcb.pin_header("J_CTRL_R", "CTRL R signals", 148, 48, [pad(1, "CTRL_R_5V"), pad(2, "CTRL_R_GND"), pad(3, "THR_R_OUT"), pad(4, "BRAKE_SW"), pad(5, "REV_R_SW"), pad(6, "ENABLE_SW")])

    # MOS/opto input header. Output side is intentionally external/isolated.
    pcb.pin_header("J_SW_IN", "MOS/opto input module", 70, 18, [
        pad(1, "GND"), pad(2, "GPIO14_BRAKE_OUT"), pad(3, "GPIO15_REV_L"), pad(4, "GPIO16_REV_R"), pad(5, "GPIO39_ENABLE_OUT"), pad(6, "+5V"),
    ], rot=90)
    for idx, gpio_net in enumerate(["GPIO14_BRAKE_OUT", "GPIO15_REV_L", "GPIO16_REV_R", "GPIO39_ENABLE_OUT"]):
        pcb.resistor(f"R_SW{idx + 1}_PD", "10k", 74 + idx * 8, 22, gpio_net, "GND")

    pcb.gr_text("warn1", "BAT+/BAT-/motor phase wires stay OFF this PCB", 8, 92, 1.1)
    pcb.gr_text("warn2", "Verify ESP32 DevKitC physical pin order before replacing J_MCU with a carrier footprint", 45, 67, 0.9)

    return pcb


def main() -> None:
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    pcb = build()
    BOARD.write_text(pcb.render(), encoding="utf-8", newline="\n")
    SCHEMATIC.write_text(render_minimal_schematic(), encoding="utf-8", newline="\n")
    PROJECT_FILE.write_text(render_minimal_project(), encoding="utf-8", newline="\n")
    with zipfile.ZipFile(ZIP, "w", compression=zipfile.ZIP_DEFLATED) as archive:
        archive.write(PROJECT_FILE, PROJECT_FILE.name)
        archive.write(SCHEMATIC, SCHEMATIC.name)
        archive.write(BOARD, BOARD.name)
    print(f"Wrote {BOARD}")
    print(f"Wrote {SCHEMATIC}")
    print(f"Wrote {PROJECT_FILE}")
    print(f"Wrote {ZIP}")


def render_minimal_schematic() -> str:
    return f'''(kicad_sch (version 20211123) (generator "followbox_kicad_generator")
    (uuid {tid("schematic-root")})
    (paper "A4")
    (title_block
        (title "{PROJECT}")
        (comment 1 "PCB-only starter generated from Follow the Box pin map")
        (comment 2 "Use the PCB document for connector placement, nets, and silkscreen zones")
    )
    (lib_symbols)
    (text "PCB-only starter. Final schematic symbols can be rebuilt in EasyEDA Pro after KiCad import." (at 20.00 20.00 0)
        (effects (font (size 2.00 2.00)) (justify left bottom))
        (uuid {tid("schematic-note")})
    )
    (sheet_instances
        (path "/" (page "1"))
    )
)
'''


def render_minimal_project() -> str:
    return '''{
    "board": {
        "design_settings": {
            "defaults": {},
            "rules": {}
        }
    },
    "libraries": {
        "pinned_footprint_libs": [],
        "pinned_symbol_libs": []
    },
    "meta": {
        "version": 1
    },
    "net_settings": {
        "classes": [],
        "meta": {
            "version": 0
        }
    },
    "project": {
        "files": []
    }
}
'''


if __name__ == "__main__":
    main()
