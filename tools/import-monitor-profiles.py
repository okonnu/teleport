#!/usr/bin/env python3
# SPDX-FileCopyrightText: (C) 2026 Teleport Developers
# SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception

"""Build Teleport's bundled input-source profile database."""

from __future__ import annotations

import argparse
import json
import re
import unicodedata
import xml.etree.ElementTree as ET
from pathlib import Path


DDCCONTROL_COMMIT = "da647356dcff711743c372dd2b3703c7fe4259f6"
MONITOR_SWITCH_COMMIT = "8d80e7f9ff3854555958aaab46bf1a543539845c"

STANDARD_INPUTS = {
    0x01: ("vga-1", "VGA 1"),
    0x02: ("vga-2", "VGA 2"),
    0x03: ("dvi-1", "DVI 1"),
    0x04: ("dvi-2", "DVI 2"),
    0x05: ("composite-1", "Composite 1"),
    0x06: ("composite-2", "Composite 2"),
    0x07: ("s-video-1", "S-Video 1"),
    0x08: ("s-video-2", "S-Video 2"),
    0x09: ("tuner-1", "Tuner 1"),
    0x0A: ("tuner-2", "Tuner 2"),
    0x0B: ("tuner-3", "Tuner 3 or USB-C"),
    0x0C: ("component-1", "Component 1"),
    0x0D: ("component-2", "Component 2"),
    0x0E: ("component-3", "Component 3"),
    0x0F: ("displayport-1", "DisplayPort 1"),
    0x10: ("displayport-2", "DisplayPort 2"),
    0x11: ("hdmi-1", "HDMI 1"),
    0x12: ("hdmi-2", "HDMI 2"),
    0x1B: ("usb-c-1", "USB-C 1"),
    0x1C: ("usb-c-2", "USB-C 2"),
}


def ascii_text(value: str) -> str:
    return unicodedata.normalize("NFKD", value).encode("ascii", "ignore").decode("ascii").strip()


def slug(value: str) -> str:
    result = re.sub(r"[^a-z0-9]+", "-", ascii_text(value).lower()).strip("-")
    return result or "input"


def parse_number(value: str) -> int:
    return int(value, 16) if value.lower().startswith("0x") else int(value, 10)


def option_labels(options_path: Path) -> dict[str, str]:
    root = ET.parse(options_path).getroot()
    labels: dict[str, str] = {}
    for value in root.iter("value"):
        value_id = value.get("id")
        name = value.get("name")
        if value_id and name and value_id not in labels:
            labels[value_id] = ascii_text(name)
    return labels


def explicit_inputs(root: ET.Element, labels: dict[str, str]) -> list[dict]:
    for control in root.iter("control"):
        if control.get("address", "").lower() != "0x60":
            continue
        result: list[dict] = []
        used_ids: set[str] = set()
        used_values: set[int] = set()
        for child in control.findall("value"):
            raw_value = child.get("value")
            if not raw_value:
                continue
            try:
                write_value = parse_number(raw_value)
            except ValueError:
                continue
            if not 0 <= write_value <= 65535 or write_value in used_values:
                continue
            source_id = child.get("id", "")
            label = ascii_text(child.get("name", "") or labels.get(source_id, "") or source_id)
            input_id = slug(source_id or label)
            suffix = 2
            base_id = input_id
            while input_id in used_ids:
                input_id = f"{base_id}-{suffix}"
                suffix += 1
            used_ids.add(input_id)
            used_values.add(write_value)
            result.append(
                {
                    "id": input_id,
                    "label": label or f"Monitor input {write_value}",
                    "writeValue": write_value,
                    "readValues": [write_value],
                }
            )
        if result:
            return result
    return []


def capability_inputs(root: ET.Element) -> list[dict]:
    for caps in root.iter("caps"):
        match = re.search(r"\b60\s*\(\s*((?:[0-9a-fA-F]{2}\s*)+)\)", caps.get("add", ""))
        if not match:
            continue
        result = []
        used: set[int] = set()
        for raw_value in re.findall(r"[0-9a-fA-F]{2}", match.group(1)):
            write_value = int(raw_value, 16)
            if write_value in used:
                continue
            used.add(write_value)
            input_id, label = STANDARD_INPUTS.get(
                write_value, (f"input-{write_value}", f"Monitor input {write_value}")
            )
            result.append(
                {
                    "id": input_id,
                    "label": label,
                    "writeValue": write_value,
                    "readValues": [write_value],
                }
            )
        if result:
            return result
    return []


def import_ddccontrol(database_root: Path) -> list[dict]:
    labels = option_labels(database_root / "options.xml.in")
    profiles = []
    for path in sorted((database_root / "monitor").glob("*.xml")):
        profile_id = path.stem.upper()
        if len(profile_id) != 7 or not re.fullmatch(r"[A-Z0-9]{3}[0-9A-F]{4}", profile_id):
            continue
        try:
            root = ET.parse(path).getroot()
        except ET.ParseError:
            continue
        inputs = explicit_inputs(root, labels) or capability_inputs(root)
        if not inputs:
            continue
        model_name = ascii_text(root.get("name", ""))
        profiles.append(
            {
                "id": profile_id,
                "manufacturerId": profile_id[:3],
                "productId": int(profile_id[3:], 16),
                "modelNames": [model_name] if model_name else [],
                "revision": 1,
                "source": f"ddccontrol-db@{DDCCONTROL_COMMIT}",
                "inputs": inputs,
            }
        )
    return profiles


def samsung_lc49g95t() -> dict:
    return {
        "id": "SAM-7052",
        "manufacturerId": "SAM",
        "productId": 0x7052,
        "modelNames": ["LC49G95T"],
        "revision": 1,
        "source": f"monitor-switch@{MONITOR_SWITCH_COMMIT}",
        "inputs": [
            {"id": "hdmi", "label": "HDMI", "writeValue": 0x11, "readValues": [0x01]},
            {"id": "displayport-1", "label": "DisplayPort 1", "writeValue": 0x0F, "readValues": [0x03]},
            {"id": "displayport-2", "label": "DisplayPort 2", "writeValue": 0x10, "readValues": [0x04]},
        ],
    }


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("ddccontrol_database", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()

    profiles = import_ddccontrol(args.ddccontrol_database)
    profiles = [profile for profile in profiles if profile["id"] != "SAM-7052"]
    profiles.append(samsung_lc49g95t())
    profiles.sort(key=lambda profile: profile["id"])
    database = {
        "schemaVersion": 1,
        "databaseVersion": "2026.09.16.1",
        "sources": [
            {
                "name": "ddccontrol-db",
                "commit": DDCCONTROL_COMMIT,
                "license": "GPL-2.0-only",
                "url": "https://github.com/ddccontrol/ddccontrol-db",
            },
            {
                "name": "monitor-switch",
                "commit": MONITOR_SWITCH_COMMIT,
                "license": "MIT",
                "url": "https://github.com/DimpiM/monitor-switch",
            },
        ],
        "profiles": profiles,
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(database, indent=2, ensure_ascii=True) + "\n", encoding="ascii")


if __name__ == "__main__":
    main()
