#!/usr/bin/env python3
"""Write a copy of a flatpak manifest with the browser source pinned to a commit.

The manifest in the tree builds the browser from main, which is what you want
when building by hand. A release has to build the commit that was tagged, so
the branch is replaced by a commit.
"""

import json
import sys


def main():
    if len(sys.argv) != 4:
        sys.exit("usage: pin-manifest.py MANIFEST OUTPUT COMMIT")

    manifest_path, output_path, commit = sys.argv[1:]

    with open(manifest_path, encoding="utf-8") as f:
        manifest = json.load(f)

    for module in manifest.get("modules", []):
        for source in module.get("sources", []):
            if source.get("type") != "git":
                continue
            if "sparkybin" not in source.get("url", "").lower():
                continue
            source.pop("branch", None)
            source["commit"] = commit

    with open(output_path, "w", encoding="utf-8") as f:
        json.dump(manifest, f, indent=4)
        f.write("\n")


if __name__ == "__main__":
    main()
