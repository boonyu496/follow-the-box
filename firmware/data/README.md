# firmware/data tombstone

`firmware/data/` used to contain a duplicate copy of the embedded H5 assets.
It is intentionally no longer a source directory.

Current source of truth:

- Embedded AP/LAN H5: `firmware/web/`
- PlatformIO LittleFS source: `firmware/platformio.ini` sets `data_dir = web`

Do not add HTML, JavaScript, CSS, or generated LittleFS assets here. Update
`firmware/web/` instead, then run the normal `buildfs` / `uploadfs` flow.
