#!/usr/bin/env python3
"""Check (and with --fetch, download) the pinned third-party assets in assets/ASSETS.lock.

usage: assets_lock.py [--fetch] [ROOT]   exit 1 on a missing file or a hash that differs
Downloads go to a temporary file and are only kept when the sha256 matches the lock.
"""
import hashlib
import os
import sys
import urllib.request


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 16), b""):
            h.update(chunk)
    return h.hexdigest()


def main():
    fetch = "--fetch" in sys.argv
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    root = args[0] if args else os.path.join(os.path.dirname(__file__), "..")
    bad = 0
    with open(os.path.join(root, "assets", "ASSETS.lock")) as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            rel, want, url = line.split()
            path = os.path.join(root, rel)
            if not os.path.exists(path) and fetch:
                tmp = path + ".download"
                urllib.request.urlretrieve(url, tmp)
                if sha256(tmp) != want:
                    os.remove(tmp)
                    print(f"assets_lock: {rel}: the download from {url} does not match the lock", file=sys.stderr)
                    bad += 1
                    continue
                os.replace(tmp, path)
            if not os.path.exists(path):
                print(f"assets_lock: {rel} is missing (tools/assets_lock.py --fetch)", file=sys.stderr)
                bad += 1
            elif sha256(path) != want:
                print(f"assets_lock: {rel}: sha256 {sha256(path)} differs from the lock {want}", file=sys.stderr)
                bad += 1
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
