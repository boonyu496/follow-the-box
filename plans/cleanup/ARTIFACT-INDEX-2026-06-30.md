# FollowBox Artifact Index - 2026-06-30

> Created during cleanup Phase F before pruning `output/`, `v/`, and `zhiliao/`.
> Purpose: preserve what each historical artifact area proved, so stale generated files can be removed from the active tree without losing the debugging trail.

## Pre-Cleanup Inventory

Scan time: 2026-06-30.

| Area | Files | Approx size | What it proved | Phase F action |
|---|---:|---:|---|---|
| `output/` | 41 | 5.2 MB | Old cloud deploy packages, OTA package zip, SSE logs, and Playwright screenshots used by prior H5/cloud reviews. | Pruned from active tree after indexing; historical content remains recoverable from Git history. Future `output/` is ignored. |
| `v/6-3/` | 20 | 75.3 MB | Raw and converted video/image samples from earlier camera/video experiments. | Pruned from active tree after indexing; future `v/` is ignored. |
| `zhiliao/EaiLidarTest-V1.12.3-20241220/` | 92 | 125.7 MB | Vendor LiDAR test package; prior notes used it to cross-check S2 baud/intensity behavior and LD06/LD19 manuals. | Kept `README.md` and vendor PDFs; removed extracted executable/runtime files. |
| `zhiliao/资料/EaiLidarTest-V1.12.3-20241220/` | 89 | 181.7 MB | Duplicate unpacked LiDAR tool runtime. | Removed as duplicate generated/vendor runtime payload. |
| `zhiliao/GC-P2304-GS-2资料包/` | 4 | 6.8 MB | UWB GC-P2304-GS-2 vendor material. | Kept base specification PDF; removed local config executables/driver zip. |
| `zhiliao/pdf_images/`, `zhiliao/pwm_pdf_images/`, `zhiliao/opto_p4_0_IM42.jpg` | 19 | 2.3 MB | Extracted images from PDFs for visual inspection. | Removed because the source PDFs remain in `zhiliao/`. |
| `zhiliao/*.pdf`, `zhiliao/资料/OV2640参考资料/`, camera docs | 10+ | 5.6 MB | Vendor specs for optocoupler, PWM-to-voltage module, ESP32-CAM/OV camera notes. | Kept as source documents. |

## References Found

- `AI-HANDOFF-MEMORY.md` references old `output/` deploy packages, Playwright screenshots, and two SSE logs as validation evidence for past tasks.
- `AI-HANDOFF-MEMORY.md` references `zhiliao/EaiLidarTest-V1.12.3-20241220` and its `config/config.json` as prior LiDAR debugging evidence. The config file contained vendor-tool defaults and was pruned with the executable package; the durable conclusion is already in the handoff records and firmware notes.
- `AI-HANDOFF-MEMORY.md` references `zhiliao/资料/OV2640参考资料`; those PDFs are kept.
- `protocols/UWB-GC-P2304.md` cites a GC-P2304 base spec extraction from older external material. The current repo keeps the GC-P2304 base specification PDF under `zhiliao/GC-P2304-GS-2资料包/`.

## Future Rules

- New screenshots, videos, SSE logs, generated deploy zips, and local experiment media should go under ignored `output/` or `v/`.
- Do not commit unpacked vendor GUI runtimes (`.exe`, `.dll`, `.lib`, `.qm`) unless a future task explicitly requires a redistributable tool snapshot.
- Keep small source-of-truth vendor specs, manuals, and photos only when they support firmware/protocol/wiring decisions.
