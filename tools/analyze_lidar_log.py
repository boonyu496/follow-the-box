#!/usr/bin/env python3
"""Classify FollowBox LiDAR bring-up logs into a small set of symptoms."""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path


def count(pattern: str, text: str) -> int:
    return len(re.findall(pattern, text, flags=re.MULTILINE))


def classify(text: str) -> tuple[str, dict[str, int]]:
    metrics = {
        "no_rx_line_low": count(
            r"LIDAR diag no_rx.*rx_line=0 h/l/t=0/[0-9]+/0", text
        ),
        "no_rx_line_high": count(
            r"LIDAR diag no_rx.*rx_line=1 h/l/t=[0-9]+/0/0", text
        ),
        "rx_stalled_one": count(
            r"(rx_no_packets rx=1\(\+0\)|rx_stalled restart.*rx=1)", text
        ),
        "rx_unknown_stream": count(
            r"rx_no_packets rx=[2-9][0-9]+\(\+[1-9][0-9]*\).*"
            r"aa55=0 55aa=0 ld54=0",
            text,
        ),
        "packets_or_ok": count(
            r"(LIDAR diag ok|LIDAR packet|packets=[1-9][0-9]*)", text
        ),
        "tof_sensor_nack_lines": count(
            r"TOF (init|reinit) sensor_nack", text
        ),
    }
    if metrics["packets_or_ok"]:
        return "PACKETS_OK", metrics
    if metrics["rx_unknown_stream"]:
        return "RX_UNKNOWN_STREAM", metrics
    if metrics["rx_stalled_one"]:
        return "RX_STALLED_ONE_BYTE", metrics
    if metrics["no_rx_line_low"]:
        return "NO_RX_LINE_LOW", metrics
    if metrics["no_rx_line_high"]:
        return "NO_RX_LINE_HIGH_IDLE", metrics
    return "UNCLASSIFIED", metrics


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("log", type=Path)
    args = parser.parse_args()

    text = args.log.read_text(encoding="utf-8", errors="replace")
    symptom, metrics = classify(text)
    print(f"LIDAR_LOG_CLASS={symptom}")
    print(" ".join(f"{key}={value}" for key, value in metrics.items()))
    return 0 if symptom == "PACKETS_OK" else 2


if __name__ == "__main__":
    raise SystemExit(main())
