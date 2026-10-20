#!/usr/bin/env python3
"""Fetch a set of well known Firefox extensions and unpack them.

The browser loads an extension from a directory, so the packages have to be
unpacked. The add-ons are taken from addons.mozilla.org, which is where the
Firefox versions live.

    tools/fetch-extensions.py [directory]
"""

import io
import json
import pathlib
import shutil
import sys
import urllib.request
import zipfile

OUT = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else "/tmp/extensions")

ADDONS = [
    ("uBlock Origin", "ublock-origin"),
    ("uBlock Origin Lite", "ublock-origin-lite"),
    ("Bitwarden", "bitwarden"),
    ("Dark Reader", "darkreader"),
    ("Privacy Badger", "privacy-badger"),
    ("Cookie AutoDelete", "cookie-autodelete"),
    ("ClearURLs", "clearurls"),
    ("Return YouTube Dislike", "return-youtube-dislike"),
    ("Consent-O-Matic", "consent-o-matic"),
    ("SponsorBlock", "sponsorblock"),
    ("Violentmonkey", "violentmonkey"),
    ("1Password", "1password"),
    ("LastPass", "lastpass"),
    ("Proton Pass", "proton-pass"),
    ("Multi-Account Containers", "multi-account-containers"),
    ("Vimium", "vimium"),
    ("Stylus", "stylus"),
    ("LanguageTool", "languagetool-webextension"),
    ("Tridactyl", "tridactyl"),
    ("Tabby", "tabby"),
    ("Decentraleyes", "decentraleyes"),
    ("Toom-ADS-Blocker", "toom-ads-blocker"),
    ("Bypass Google", "bypass-google"),
]

UA = "Mozilla/5.0 (X11; Linux x86_64; rv:130.0) Gecko/20100101 Firefox/130.0"


def get(url):
    request = urllib.request.Request(url, headers={"User-Agent": UA})
    with urllib.request.urlopen(request, timeout=120) as response:
        return response.read()


def main():
    OUT.mkdir(parents=True, exist_ok=True)

    for name, slug in ADDONS:
        url = f"https://addons.mozilla.org/firefox/downloads/latest/{slug}/latest.xpi"

        try:
            payload = get(url)
            with zipfile.ZipFile(io.BytesIO(payload)) as archive:
                manifest = json.loads(archive.read("manifest.json"))
                target = OUT / slug
                if target.exists():
                    shutil.rmtree(target)
                archive.extractall(target)
        except Exception as error:
            print(f"{name:30} {slug:30} -      failed: {error}")
            continue

        version = manifest.get("version", "?")
        mv = manifest.get("manifest_version", "?")
        background = manifest.get("background", {})
        kind = "page" if "page" in background else (
            "scripts" if "scripts" in background else "-")
        perms = len(manifest.get("permissions", []) or [])
        perms += len(manifest.get("host_permissions", []) or [])

        print(f"{name:30} {slug:30} MV{mv:<4} v{version} background:{kind} "
              f"permissions:{perms}")


if __name__ == "__main__":
    main()
