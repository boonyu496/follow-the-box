# FollowBox low-voltage PCB starter

This directory contains the simplest import path for drawing the Follow the Box low-voltage interface PCB in JLC EasyEDA Pro:

1. Generate a KiCad PCB file from the project pin map.
2. Import the generated `.kicad_pcb` or `.zip` into JLC EasyEDA Pro with `Import KiCad`.
3. Use EasyEDA Pro for final symbol/footprint replacement, DRC, routing, and ordering.

The generated board is a low-voltage signal/interface starter only. It intentionally excludes motor BAT+/BAT-, controller main current, and motor phase wires.

## Files

- `generate_followbox_kicad_pcb.py` - deterministic generator for a starter PCB layout.
- `generated/FollowBox_LowVoltage_Interface_PCB_v1.kicad_pcb` - generated KiCad PCB file.
- `generated/FollowBox_LowVoltage_Interface_PCB_v1.zip` - import package for EasyEDA Pro.

## Regenerate

```powershell
python .\pcb\generate_followbox_kicad_pcb.py
```
