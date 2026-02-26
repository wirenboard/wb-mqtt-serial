#!/usr/bin/env python3
"""
Dump wb-mqtt-serial templates with the same structure printed by
TDeviceTemplatesTest.Validate.
"""

import argparse
import json
import sys
from pathlib import Path
from typing import Dict, Iterable, List, Sequence


def strip_json_comments(text: str) -> str:
    """Remove // comments while keeping string literals intact."""
    result: List[str] = []
    i = 0
    length = len(text)
    in_string = False
    escape = False

    while i < length:
        ch = text[i]

        if in_string:
            result.append(ch)
            if escape:
                escape = False
            elif ch == "\\":
                escape = True
            elif ch == '"':
                in_string = False
            i += 1
            continue

        if ch == '"':
            in_string = True
            result.append(ch)
            i += 1
            continue

        if ch == "/" and i + 1 < length and text[i + 1] == "/":
            i += 2
            while i < length and text[i] not in ("\n", "\r"):
                i += 1
            continue

        result.append(ch)
        i += 1

    return "".join(result)


class SubdeviceMap:
    def __init__(self, device_type: str, device_schema: Dict) -> None:
        self._device_type = device_type
        self._templates: Dict[str, Dict] = {}
        subdevices = device_schema.get("subdevices")
        if isinstance(subdevices, list):
            for entry in subdevices:
                if not isinstance(entry, dict):
                    continue
                sub_type = entry.get("device_type")
                sub_schema = entry.get("device")
                if not sub_type or not isinstance(sub_schema, dict):
                    continue
                if sub_type in self._templates:
                    print(
                        f"[warn] Device type '{device_type}': duplicate subdevice '{sub_type}'",
                        file=sys.stderr,
                    )
                self._templates[sub_type] = sub_schema

    def get(self, sub_type: str) -> Dict:
        try:
            return self._templates[sub_type]
        except KeyError as exc:
            raise RuntimeError(
                f"Device type '{self._device_type}'. Can't find template for subdevice '{sub_type}'"
            ) from exc


class DeviceTemplate:
    def __init__(self, template_type: str, schema: Dict, path: Path) -> None:
        self.type = template_type
        self.schema = schema
        self.path = path
        self.subdevices = SubdeviceMap(template_type, schema)


def iter_template_files(directory: Path) -> Iterable[Path]:
    for file_path in sorted(directory.iterdir()):
        if file_path.is_file() and file_path.suffix == ".json":
            yield file_path


def load_templates(directories: Sequence[Path]) -> Dict[str, DeviceTemplate]:
    templates: Dict[str, DeviceTemplate] = {}
    loaded_any = False
    for directory in directories:
        for template_file in iter_template_files(directory):
            loaded_any = True
            try:
                with template_file.open("r", encoding="utf-8") as src:
                    payload = strip_json_comments(src.read())
                data = json.loads(payload)
            except json.JSONDecodeError as exc:
                raise RuntimeError(f"Failed to parse {template_file}: {exc}") from exc
            device_type = data.get("device_type")
            device_schema = data.get("device")
            if not device_type or not isinstance(device_schema, dict):
                raise RuntimeError(f"{template_file} does not look like a device template")
            templates[device_type] = DeviceTemplate(device_type, device_schema, template_file)
    if not loaded_any:
        raise RuntimeError("No templates found in the provided directories")
    return templates


def build_mqtt_prefix(channel: Dict, prefix: str) -> str:
    channel_id = channel.get("id", channel.get("name", ""))
    channel_id = "" if channel_id is None else str(channel_id)
    if not channel_id:
        return prefix
    if not prefix:
        return channel_id
    return f"{prefix} {channel_id}"


def print_device(device_schema: Dict, mqtt_prefix: str, subdevices: SubdeviceMap, level: int) -> None:
    channels = device_schema.get("channels")
    if not isinstance(channels, list):
        return
    deduped_channels = {}
    for channel in channels:
        if not isinstance(channel, dict):
            continue
        name = str(channel.get("name", ""))
        deduped_channels.setdefault(name, channel)
    for _, channel in sorted(deduped_channels.items(), key=lambda item: item[0]):
        print_channel(channel, mqtt_prefix, subdevices, level)


def print_channel(channel: Dict, mqtt_prefix: str, subdevices: SubdeviceMap, level: int) -> None:
    name = "\t" * level + str(channel.get("name", ""))
    new_prefix = build_mqtt_prefix(channel, mqtt_prefix)

    one_of = channel.get("oneOf")
    if isinstance(one_of, list):
        for candidate in one_of:
            if not isinstance(candidate, str):
                continue
            print(f"{name}: {candidate}")
            sub_schema = subdevices.get(candidate)
            print_device(sub_schema, new_prefix, subdevices, level + 1)
        return

    subdevice_type = channel.get("device_type")
    if isinstance(subdevice_type, str):
        print(f"{name}: {subdevice_type}")
        sub_schema = subdevices.get(subdevice_type)
        print_device(sub_schema, new_prefix, subdevices, level + 1)
        return

    print(f"{name}  =>  {new_prefix}")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Print device template structure in the same format as the Validate test."
    )
    parser.add_argument(
        "directories",
        nargs="*",
        help="Template directories to scan (repeat the argument for multiple paths).",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()

    if not args.directories:
        print("Please provide at least one template directory path.", file=sys.stderr)
        return 1

    directories: List[Path] = []
    for directory in args.directories:
        path = Path(directory).expanduser()
        if not path.exists() or not path.is_dir():
            print(f"[skip] {path} is not a directory", file=sys.stderr)
            continue
        directories.append(path)

    if not directories:
        print("None of the provided template directories exist or are accessible.", file=sys.stderr)
        return 1

    try:
        templates = load_templates(directories)
    except RuntimeError as exc:
        print(exc, file=sys.stderr)
        return 1

    for template in sorted(templates.values(), key=lambda item: item.type):
        print(template.type)
        print_device(template.schema, "", template.subdevices, 1)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
