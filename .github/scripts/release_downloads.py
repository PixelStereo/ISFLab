"""Refresh the README links from the most recently published complete release."""

import json
from pathlib import Path
import re
import sys


ASSETS = {
    "macOS ARM64": "ISFLab-macOS-arm64.zip",
    "Windows x64": "ISFLab-Windows-x64.zip",
    "Linux x64": "ISFLab-Linux-x64.tar.gz",
}
START = "<!-- release-downloads:start -->"
END = "<!-- release-downloads:end -->"


def download_block(releases, repository):
    base = f"https://github.com/{repository}/releases"
    for release in sorted(releases, key=lambda item: item.get("published_at") or "", reverse=True):
        tag = release["tag_name"]
        if (release.get("draft") or not release.get("published_at")
                or not re.fullmatch(r"v[0-9]+\.[0-9]+(?:\.[0-9]+)?(?:[-+][A-Za-z0-9.-]+)?", tag)):
            continue
        assets = {item["name"]: item["browser_download_url"] for item in release.get("assets", [])
                  if item.get("state") == "uploaded" and item.get("size", 0) > 0}
        urls = {platform: f"{base}/download/{tag}/{name}" for platform, name in ASSETS.items()}
        if any(assets.get(name) != urls[platform] for platform, name in ASSETS.items()):
            continue
        preview = " (preview)" if release.get("prerelease") else ""
        links = " · ".join(f"[{platform}]({url})" for platform, url in urls.items())
        return f"Latest build **[{tag}]({base}/tag/{tag})**{preview}\n\n{links}"
    return "Downloads appear here after the first complete release."


def main():
    path = Path("README.md")
    source = path.read_text(encoding="utf-8")
    if source.count(START) != 1 or source.count(END) != 1:
        raise ValueError("Expected one pair of README download markers")
    prefix, _, tail = source.partition(START)
    _, marker, suffix = tail.partition(END)
    if not marker:
        raise ValueError("README download markers are out of order")
    releases = [release for page in json.load(sys.stdin) for release in page]
    block = download_block(releases, sys.argv[1])
    updated = f"{prefix}{START}\n{block}\n{END}{suffix}"
    if updated != source:
        path.write_text(updated, encoding="utf-8")


if __name__ == "__main__":
    main()
