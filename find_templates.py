import argparse
import fnmatch
import json
import os
import re
import sys
from collections import OrderedDict


# ...existing code...
def strip_json_comments(s: str) -> str:
    # remove single-line // comments and multi-line /* */ comments
    s = re.sub(r"//.*?$", "", s, flags=re.MULTILINE)
    s = re.sub(r"/\*[\s\S]*?\*/", "", s)
    return s


def contains_pattern(obj, regex):
    if isinstance(obj, str):
        return bool(regex.search(obj))
    return False


def process_file(path, regex):
    try:
        with open(path, "r", encoding="utf-8") as f:
            raw = f.read()
    except Exception as e:
        print(f"ERROR reading {path}: {e}", file=sys.stderr)
        return []

    try:
        text = strip_json_comments(raw)
        data = json.loads(text)
    except Exception as e:
        print(f"ERROR parsing {path}: {e}", file=sys.stderr)
        return []

    results = []

    translated_strings = set()

    translations = data.get("device", {}).get("translations", {}).get("en", {})
    for k, v in translations.items():
        if contains_pattern(v, regex):
            translated_strings.add(k)

    translations = data.get("device", {}).get("translations", {}).get("ru", {})
    for k, v in translations.items():
        if contains_pattern(v, regex):
            translated_strings.add(k)

    def traverse(node, cur_path=""):
        # If node is a dict, inspect keys and recurse
        if isinstance(node, dict):
            for k, v in node.items():
                if isinstance(v, str) and v in translated_strings:
                    results.append({"file": path, "path": f"{cur_path}", "node": v})
                else:
                    # Recurse into the value
                    traverse(v, f"{cur_path}/{k}")
        # If node is a list, recurse into elements
        elif isinstance(node, (list, tuple)):
            for idx, el in enumerate(node):
                traverse(el, f"{cur_path}[{idx}]")
        # other types: nothing to do

    traverse(data, "")
    return results


def find_json_files(root):
    for dirpath, dirnames, filenames in os.walk(root):
        for name in filenames:
            if fnmatch.fnmatch(name, "*.json"):
                yield os.path.join(dirpath, name)


def main():
    p = argparse.ArgumentParser(
        description="Find JSON nodes in channels/parameters containing version-like strings (e.g. 1.2.3)"
    )
    p.add_argument(
        "roots",
        nargs="*",
        default=["."],
        help="one or more root folders to search (default: .)",
    )
    args = p.parse_args()

    regex = re.compile(r"\d+\.\d+\.\d+")
    all_results = []

    for root in args.roots:
        for path in find_json_files(root):
            hits = process_file(path, regex)
            all_results.extend(hits)

    grouped = OrderedDict()
    for item in all_results:
        grouped.setdefault(item["file"], []).append(item)

    # print summary: total files and per-file item counts
    print(f"Total files with matches: {len(grouped)}")
    for file, items in grouped.items():
        print(f"{file}: {len(items)} items")
    # print("=" * 80)

    # for file, items in grouped.items():
    #     print(f"File: {file}")
    #     for item in items:
    #         print(item["path"])
    #     print("-" * 80)


if __name__ == "__main__":
    main()
