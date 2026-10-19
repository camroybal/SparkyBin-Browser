#!/usr/bin/env python3
"""Print the section of NEWS that describes a given version.

Used to fill in the body of a GitHub Release. The version may be given with
or without a leading "v", and a version of "latest" takes the first section
in the file.
"""

import re
import sys
from pathlib import Path

NEWS = Path(__file__).resolve().parent.parent.parent / "NEWS"

HEADER = re.compile(
    r"^([0-9][^\n]+?)\s+-\s+([A-Za-z]+ \d{1,2}, \d{4})\s*\n[=]+",
    re.MULTILINE,
)


def main():
    if len(sys.argv) != 2:
        sys.exit("usage: release-notes.py VERSION")

    wanted = sys.argv[1].lstrip("v")

    text = NEWS.read_text(encoding="utf-8")
    matches = list(HEADER.finditer(text))
    if not matches:
        sys.exit("no releases found in %s" % NEWS)

    for index, match in enumerate(matches):
        version = match.group(1).strip()
        if version != wanted and wanted != "latest":
            continue

        end = matches[index + 1].start() if index + 1 < len(matches) else len(text)
        body = text[match.end():end].strip()

        print("SparkyBin Browser %s\n" % version)
        print(body)
        return 0

    sys.exit("no release for version %s in %s" % (wanted, NEWS))


if __name__ == "__main__":
    main()
