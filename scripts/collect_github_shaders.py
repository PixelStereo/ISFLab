#!/usr/bin/env python3
"""Collect GitHub .fs files and their dependencies into a local folder."""

import argparse
import json
import os
from pathlib import Path, PurePosixPath
import posixpath
import re
import shutil
import subprocess
import tempfile
import time
from urllib.error import HTTPError
from urllib.parse import quote, urlencode
from urllib.request import Request, urlopen
from zipfile import ZipFile, BadZipFile

from collect_examples import HEADER, KNOWN, REPO, clean

SEARCH_TERMS = ("ISFVSN", "gl_FragColor", "IMG_NORM_PIXEL", "RENDERSIZE", "main")
INCLUDE = re.compile(rb'^\s*#\s*include\s+"([^"]+)"', re.M)
NOTICE = re.compile(r"^(licen[sc]e|copying|notice)(\b|[_.-])", re.I)
API = "https://api.github.com"


def valid_repo(name):
    return REPO.fullmatch(name) and all(part not in {".", ".."} for part in name.split("/"))


def request(url, token=None):
    headers = {"User-Agent": "ISFLab-shader-collector"}
    if token:
        headers["Authorization"] = f"Bearer {token}"
    for attempt in range(4):
        try:
            return urlopen(Request(url, headers=headers), timeout=90)
        except HTTPError as error:
            retry = error.headers.get("Retry-After")
            if attempt == 3 or not (retry or error.code in {429, 502, 503, 504}):
                raise
            time.sleep(min(90, int(retry)) if retry and retry.isdigit() else 10 * (attempt + 1))


def api(path, token):
    with request(API + path, token) as response:
        return json.load(response)


def discover(terms, token, issues, log):
    repositories = set()
    for term in terms:
        try:
            for page in range(1, 11):
                params = urlencode(dict(q=f"{term} extension:fs", per_page=100, page=page))
                log(f"Searching GitHub for {term!r}, page {page}…")
                result = api("/search/code?" + params, token)
                log(f"  {len(result['items'])} matches on this page; {result['total_count']} reported.")
                if page == 1 and (result["total_count"] > 1000 or result.get("incomplete_results")):
                    issues.append(f"GitHub returned partial results for {term!r}.")
                for item in result["items"]:
                    name = item["repository"]["full_name"]
                    if valid_repo(name) and not item["repository"].get("private"):
                        repositories.add(name)
                if len(result["items"]) < 100 or page * 100 >= result["total_count"]:
                    break
                time.sleep(7)
        except (OSError, ValueError, KeyError) as error:
            issues.append(f"Search {term!r}: {error}")
        time.sleep(7)
    return repositories


def collect_archive(archive, output, log=lambda message: None):
    members = {}
    for info in archive.infolist():
        path = clean(info.filename.partition("/")[2])
        if path and not info.is_dir() and (info.external_attr >> 28) != 10:
            members[path] = info
    shaders = [path for path in members if PurePosixPath(path).suffix.lower() == ".fs"]
    log(f"  Found {len(shaders)} .fs files in {len(members)} archive entries.")
    vertices = {}
    for path in members:
        file = PurePosixPath(path)
        if file.suffix.lower() == ".vs":
            vertices.setdefault(str(file.with_suffix("")), []).append(path)
    dependencies = set()
    missing = []
    examined = set()
    queue = list(shaders)
    for shader in shaders:
        queue.extend(vertices.get(str(PurePosixPath(shader).with_suffix("")), []))
    while queue:
        path = queue.pop()
        if path in examined:
            continue
        examined.add(path)
        with archive.open(members[path]) as source:
            data = source.read(8 * 1024 * 1024 + 1)
        if len(data) > 8 * 1024 * 1024:
            missing.append(f"{path}: source too large to inspect for dependencies")
            continue
        includes = {match.decode("utf-8", "replace") for match in INCLUDE.findall(data)}
        refs = list(includes)
        header = HEADER.match(data.removeprefix(b"\xef\xbb\xbf"))
        if header:
            try:
                metadata = json.loads(header[1])
            except (ValueError, UnicodeError):
                metadata = {}
            imports = metadata.get("IMPORTED", {}) if isinstance(metadata, dict) else {}
            if isinstance(imports, dict):
                refs.extend(item.get("PATH") for item in imports.values() if isinstance(item, dict))
        for ref in refs:
            if not isinstance(ref, str) or not ref:
                continue
            target = clean(posixpath.normpath(posixpath.join(posixpath.dirname(path), ref)))
            if target not in members:
                missing.append(f"{path}: {ref}")
                log(f"  Missing dependency: {path} → {ref}")
                continue
            # Keep intermediate directories needed by literal paths such as textures/../image.png.
            directory = posixpath.dirname(path)
            for component in ref.split("/")[:-1]:
                directory = posixpath.normpath(posixpath.join(directory, component))
                if directory != "." and clean(directory):
                    (output / directory).mkdir(parents=True, exist_ok=True)
            dependencies.add(target)
            if ref in includes:
                queue.append(target)
    selected = examined | dependencies
    if shaders:
        selected.update(path for path in members if NOTICE.match(PurePosixPath(path).name))
    for index, path in enumerate(sorted(selected), 1):
        log(f"  Copying {index}/{len(selected)}: {path}")
        dest = output / path
        dest.parent.mkdir(parents=True, exist_ok=True)
        with archive.open(members[path]) as source, dest.open("wb") as target:
            shutil.copyfileobj(source, target)
    return len(shaders), len(selected - set(shaders)), sorted(set(missing))


def collect_repo(repo, output, token, log):
    if not valid_repo(repo):
        raise ValueError("invalid repository name")
    log(f"  Reading repository metadata for {repo}…")
    metadata = api("/repos/" + repo, token)
    if metadata.get("private"):
        raise ValueError("source repository is private")
    branch = metadata["default_branch"]
    commit = api(f"/repos/{repo}/commits/{quote(branch, safe='')}", token)["sha"]
    if not re.fullmatch(r"[0-9a-f]{40}", commit):
        raise ValueError("invalid GitHub commit identifier")
    log(f"  Resolved {branch} to {commit}. Downloading snapshot…")
    destination = output / repo / commit
    with tempfile.TemporaryDirectory(prefix="isflab-", dir=output) as temp:
        temporary = Path(temp)
        zip_path = temporary / "source.zip"
        with request(f"https://codeload.github.com/{repo}/zip/{commit}") as response, zip_path.open("wb") as file:
            downloaded = 0
            next_report = 8 * 1024 * 1024
            while block := response.read(1024 * 1024):
                file.write(block)
                downloaded += len(block)
                if downloaded >= next_report:
                    log(f"  Downloaded {downloaded / 1024**2:.1f} MiB…")
                    next_report = downloaded + 8 * 1024 * 1024
            log(f"  Snapshot downloaded ({downloaded / 1024**2:.1f} MiB).")
        tree = temporary / "tree"
        tree.mkdir()
        with ZipFile(zip_path) as archive:
            count, dependencies, missing = collect_archive(archive, tree, log)
        if count and not destination.exists():
            destination.parent.mkdir(parents=True, exist_ok=True)
            tree.rename(destination)
            log(f"  Stored in {destination}")
    license_info = metadata.get("license") or {}
    return dict(repository=repo, commit=commit, license=license_info.get("spdx_id"),
                stored=destination.relative_to(output).as_posix() if count else None,
                shaders=count, dependencies=dependencies, missing=missing)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path)
    parser.add_argument("--repo", action="append", default=[], help="also scan this owner/repo; repeatable")
    parser.add_argument("--query", action="append", help="replace the default GitHub code search terms; repeatable")
    parser.add_argument("--repos-only", action="store_true", help="skip code search and scan the supplied repos")
    parser.add_argument("--resume", action="store_true", help="reuse saved discovery and retry failed repos")
    parser.add_argument("-v", "--verbose", action="store_true", help="report search, download and copy progress")
    args = parser.parse_args()

    def log(message):
        if args.verbose:
            print(message, flush=True)

    if any(not valid_repo(repo) for repo in args.repo):
        parser.error("--repo expects owner/repo")
    if args.repos_only and not args.repo and not args.resume:
        parser.error("--repos-only requires at least one --repo")
    token = os.environ.get("GH_TOKEN") or os.environ.get("GITHUB_TOKEN")
    if not token:
        try:
            token = subprocess.check_output(["gh", "auth", "token"], text=True, stderr=subprocess.DEVNULL).strip()
        except (OSError, subprocess.CalledProcessError):
            pass
    if not token and not args.repos_only:
        parser.error("GitHub code search requires authentication. Run gh auth login or set GH_TOKEN.")

    args.output.mkdir(parents=True, exist_ok=True)
    manifest = args.output / "manifest.json"
    if manifest.exists() and not args.resume:
        parser.error("the output already has a manifest; use --resume or choose another folder")
    state = json.loads(manifest.read_text()) if manifest.exists() else {
        "repositories": [], "results": {}, "search_issues": []}
    repositories = set(state["repositories"]) | set(args.repo)
    if not args.repos_only and not (args.resume and state["repositories"]):
        repositories.update(KNOWN)
        repositories.update(discover(args.query or SEARCH_TERMS, token, state["search_issues"], log))
    state["repositories"] = sorted({repo.lower() for repo in repositories})

    def checkpoint():
        temp = manifest.with_suffix(".tmp")
        temp.write_text(json.dumps(state, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
        temp.replace(manifest)

    checkpoint()
    for index, repo in enumerate(state["repositories"], 1):
        progress = f"[{index}/{len(state['repositories'])}] {repo}"
        if repo in state["results"] and "error" not in state["results"][repo]:
            log(f"{progress}: already collected, skipping.")
            continue
        log(f"{progress}: starting…")
        try:
            result = collect_repo(repo, args.output, token, log)
        except (OSError, ValueError, KeyError, BadZipFile, RuntimeError) as error:
            result = {"repository": repo, "error": str(error)}
        state["results"][repo] = result
        checkpoint()
        print(f"{repo}: {result.get('error') or str(result['shaders']) + ' .fs files'}", flush=True)
    for issue in state["search_issues"]:
        print(issue)
    print(f"Manifest: {manifest}")


if __name__ == "__main__":
    main()
