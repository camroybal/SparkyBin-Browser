#!/usr/bin/env python3
"""The name an extension calls itself.

Firefox lets a manifest use __MSG_name__ to say that the name comes from the
extension's own translations. The browser reads the manifest as it is, so
this is what the name looks like to it, and it is the name that goes in the
webextensions-active setting.
"""

import json
import pathlib
import sys


def resolve(directory):
    path = pathlib.Path(directory)
    manifest = json.loads((path / "manifest.json").read_text())

    name = manifest.get("name", "")
    if not name.startswith("__MSG_"):
        return name

    message = name[len("__MSG_"):-len("__")]
    locale = manifest.get("default_locale", "en")

    for candidate in (locale, "en"):
        messages = path / "_locales" / candidate / "messages.json"
        if not messages.exists():
            continue
        data = json.loads(messages.read_text())
        entry = data.get(message)
        if entry:
            return entry.get("message", name)

    return name


if __name__ == "__main__":
    print(resolve(sys.argv[1]))
