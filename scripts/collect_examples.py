#!/usr/bin/env python3
"""Download and index ISF fragments from public GitHub repository snapshots."""
import argparse
import concurrent.futures
import datetime as dt
import hashlib
import io
import json
import os
import posixpath
import re
import subprocess
import tempfile
import time
import urllib.error
import urllib.parse
import urllib.request
import zipfile
from pathlib import Path, PurePosixPath

LIMIT = 128 * 1024 * 1024
HEADER = re.compile(rb"^\s*/\*(.*?)\*/", re.S)
REPO = re.compile(r"^[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+$")
KNOWN = ["Vidvox/ISF-Files", "bareimage/ISF", "grigM/ISF-shaders-collection"]
QUERIES = [
    "ISF in:name shader in:description",
    "ISF in:name GLSL in:description",
    "ISF in:name \"interactive shader format\" in:description",
    "ISF in:name topic:isf",
    "ISF in:name topic:shaders",
    "ISF-shaders in:name",
    "ISF in:name shader in:readme",
    "ISF in:name gl_FragColor in:readme",
]

def fetch(url, limit=LIMIT):
    request = urllib.request.Request(url, headers={"User-Agent": "ISFLab-example-collector"})
    with urllib.request.urlopen(request, timeout=25) as response:
        content = response.read(limit + 1)
        if len(content) > limit:
            raise ValueError("download exceeded size limit")
        return content

def search(query, page=1):
    params = urllib.parse.urlencode(dict(q=query, per_page=100, page=page))
    return json.loads(fetch("https://api.github.com/search/repositories?" + params, 10 * 1024 * 1024))

def clean(path):
    if "\\" in path or ":" in path or path.startswith("/"):
        return None
    parts = PurePosixPath(path).parts
    if not parts or ".." in parts or any(p in {".git", ".agents", ".codex", ".aws"} for p in parts):
        return None
    return "/".join(parts)

def snapshot(repo):
    name = repo["full_name"]
    branch = repo.get("default_branch") or "HEAD"
    url = "https://codeload.github.com/" + name + "/zip/" + urllib.parse.quote(branch, safe="/")
    try:
        archive = zipfile.ZipFile(io.BytesIO(fetch(url)))
        members = {}
        for entry in archive.infolist():
            if entry.is_dir() or entry.file_size > 32 * 1024 * 1024 or entry.external_attr >> 28 == 10:
                continue
            relative = clean(entry.filename.partition("/")[2])
            if relative:
                members[relative] = entry
        licenses = [p for p in members if re.match(r"^(licen[sc]e|copying|notice)(\b|[_.-])",
                                                   PurePosixPath(p).name, re.I)]
        files = []
        for path, item in members.items():
            suffix = PurePosixPath(path).suffix.lower()
            if suffix not in {".fs", ".isf", ".frag", ".glsl", ".fsh"} or item.file_size > 2 * 1024 * 1024:
                continue
            code = archive.read(item)
            match = HEADER.match(code.removeprefix(b"\xef\xbb\xbf"))
            if not match or not re.search(rb"\bvoid\s+main\s*\(", code[match.end():]):
                continue
            try:
                metadata = json.loads(match[1])
            except (ValueError, UnicodeError):
                continue
            if not isinstance(metadata, dict) or not isinstance(metadata.get("INPUTS", []), list):
                continue
            if suffix not in {".fs", ".isf"} and not any(k in metadata for k in ("ISFVSN", "INPUTS", "PASSES", "CATEGORIES")):
                continue
            companion = str(PurePosixPath(path).with_suffix(".vs"))
            assets = {}
            if companion in members:
                assets[companion] = archive.read(members[companion])
            imports = metadata.get("IMPORTED", {})
            for spec in imports.values() if isinstance(imports, dict) else []:
                original = spec.get("PATH") if isinstance(spec, dict) else None
                if not isinstance(original, str):
                    continue
                target = clean(posixpath.normpath(posixpath.join(posixpath.dirname(path), original)))
                if target in members:
                    assets[target] = archive.read(members[target])
            relevant = [p for p in licenses if not posixpath.dirname(p) or path.startswith(posixpath.dirname(p) + "/")]
            files.append((path, code, assets, relevant))
        return repo, files, {p: archive.read(members[p]) for p in licenses} if files else {}, ""
    except (OSError, ValueError, zipfile.BadZipFile, RuntimeError) as error:
        return repo, [], {}, str(error)

def probe_large(repo):
    """Look for candidate filenames without fetching the repository's blobs."""
    try:
        with tempfile.TemporaryDirectory(prefix="isflab-git-") as temporary:
            path = Path(temporary) / "repository"
            environment = dict(os.environ, GIT_TERMINAL_PROMPT="0", GIT_CONFIG_NOSYSTEM="1",
                               GIT_CONFIG_GLOBAL=os.devnull)
            result = subprocess.run(
                ["git", "clone", "--quiet", "--depth=1", "--filter=blob:none", "--no-tags", "--no-checkout",
                 "https://github.com/" + repo["full_name"] + ".git", str(path)],
                capture_output=True, timeout=18, env=environment)
            if result.returncode:
                return repo, [], {}, "Could not retrieve the Git tree"
            files = subprocess.run(["git", "-C", str(path), "ls-tree", "-r", "-z", "--name-only", "HEAD"],
                                   capture_output=True, timeout=12, check=True, env=environment).stdout
            if not any(file.lower().endswith((b".fs", b".isf", b".frag", b".glsl", b".fsh"))
                       for file in files.split(b"\0")):
                return repo, [], {}, ""
        return snapshot(repo)
    except (OSError, subprocess.SubprocessError):
        return repo, [], {}, "Could not retrieve the Git tree"

def signature_for(code, assets):
    digest = hashlib.sha256(code)
    for asset, data in sorted(assets.items()):
        digest.update(asset.encode() + b"\0" + hashlib.sha256(data).digest())
    return digest.hexdigest()

def existing_signature(output, source):
    path = output / source["stored"]
    code = path.read_bytes()
    original = source["source"]
    root = output.joinpath(*source["repository"].split("/"))
    assets = {}
    vertex = str(PurePosixPath(original).with_suffix(".vs"))
    if (root / vertex).is_file():
        assets[vertex] = (root / vertex).read_bytes()
    match = HEADER.match(code.removeprefix(b"\xef\xbb\xbf"))
    metadata = json.loads(match[1])
    imports = metadata.get("IMPORTED", {})
    for spec in imports.values() if isinstance(imports, dict) else []:
        ref = spec.get("PATH") if isinstance(spec, dict) else None
        if isinstance(ref, str):
            relative = clean(posixpath.normpath(posixpath.join(posixpath.dirname(original), ref)))
            if relative and (root / relative).is_file():
                assets[relative] = (root / relative).read_bytes()
    return signature_for(code, assets)

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=Path("examples/community"))
    parser.add_argument("--broad", action="store_true", help="also index public name matches by creation-date buckets")
    parser.add_argument("--scan-all", action="store_true", help="scan name matches beyond the shader-related shortlist")
    parser.add_argument("--resume", action="store_true", help="keep previous results and scan newly discovered repositories")
    parser.add_argument("--date", action="append", metavar="START..END",
                        help="limit broad discovery to this creation-date interval (repeatable)")
    parser.add_argument("--probe-large", action="store_true", help="inspect deferred repository trees with a shallow blobless Git clone")
    parser.add_argument("--retry-errors", action="store_true", help="retry failed snapshots on resume")
    parser.add_argument("--forks", action="store_true", help="search forks instead of the default repository index")
    args = parser.parse_args()
    if args.date and not all(re.fullmatch(r"\d{4}-\d{2}-\d{2}\.\.\d{4}-\d{2}-\d{2}", interval)
                             for interval in args.date):
        parser.error("--date expects YYYY-MM-DD..YYYY-MM-DD")
    args.output.mkdir(parents=True, exist_ok=True)
    repos = {name.lower(): {"full_name": name} for name in KNOWN}
    previous = {}
    manifest_path = args.output / "manifest.json"
    if (args.resume or args.probe_large) and manifest_path.exists():
        previous = json.loads(manifest_path.read_text())
    discovery = list(previous.get("discovery", []))
    queries = [] if args.probe_large or (args.resume and previous) else list(QUERIES)
    if not args.probe_large and (args.broad or args.scan_all):
        year = dt.date.today().year
        intervals = args.date or [f"{a}-01-01..{b}-12-31" for a, b in
                                  [(2008, 2015), (2016, 2019), (2020, 2021), (2022, 2023), (2024, 2024), (2025, 2025)]
                                  if a <= year] + [period for y in range(2026, year + 1)
                                  for period in (f"{y}-01-01..{y}-06-30", f"{y}-07-01..{y}-12-31")]
        queries += [f"ISF in:name created:{interval}" + (" fork:only" if args.forks else "")
                    for interval in intervals]
    priority = set(repos)
    calls = 0
    start = time.monotonic()
    for query in queries:
        result = {"query": query, "indexed": 0}
        try:
            page = 1
            while True:
                if calls >= 9:
                    time.sleep(max(0, 62 - (time.monotonic() - start)))
                    start, calls = time.monotonic(), 0
                response = search(query, page)
                calls += 1
                result["reported"] = response["total_count"]
                result["incomplete"] = response.get("incomplete_results", False)
                items = response["items"]
                for repo in items:
                    full = repo["full_name"]
                    if REPO.fullmatch(full) and "isf" in full.split("/")[1].lower():
                        repos[full.lower()] = repo
                        if query in QUERIES:
                            priority.add(full.lower())
                result["indexed"] += len(items)
                if not items or page >= 10 or page * 100 >= response["total_count"]:
                    break
                page += 1
        except (OSError, ValueError, KeyError) as error:
            result["error"] = str(error)
        discovery.append(result)
        print(json.dumps(result), flush=True)
    # Broad name matches are indexed, while shader-related repositories get archive scans.
    terms = re.compile(r"shader|glsl|vidvox|vdmx|madmapper|resolume|shadertoy|graphics|pixel", re.I)
    targets = [repo for key, repo in repos.items() if args.scan_all or key in priority or terms.search(
        " ".join((repo["full_name"], repo.get("description") or "", " ".join(repo.get("topics", [])))))]
    if args.resume and not args.probe_large:
        pending = previous.get("pending_repositories")
        if pending is None:
            deferred_names = {name.lower() for name in previous.get("deferred_large_repositories", [])}
            pending = [{"full_name": name} for name in previous.get("indexed_names", [])
                       if REPO.fullmatch(name) and name.lower() not in deferred_names]
        queued = {repo["full_name"].lower(): repo for repo in targets}
        for repo in pending:
            queued.setdefault(repo["full_name"].lower(), repo)
        if args.retry_errors:
            for item in previous.get("scanned", []):
                name = item["repository"]
                if item.get("error") and REPO.fullmatch(name):
                    repo = queued.setdefault(name.lower(), {"full_name": name})
                    if not repo.get("default_branch"):
                        repo["default_branch"] = item.get("branch")
        targets = list(queued.values())
    scanned = list(previous.get("scanned", []))
    visited = {item["repository"].lower() for item in scanned
               if not args.retry_errors or not item.get("error")}
    targets = [repo for repo in targets if repo["full_name"].lower() not in visited]
    deferred = []
    if args.scan_all:
        deferred = [repo["full_name"] for repo in targets
                    if repo.get("size", 0) > 24 * 1024 and repo["full_name"].lower() not in priority
                    and not terms.search(repo.get("description") or "")]
        excluded = set(deferred)
        targets = [repo for repo in targets if repo["full_name"] not in excluded]
    if args.probe_large:
        targets = [{"full_name": name} for name in previous.get("deferred_large_repositories", [])
                   if REPO.fullmatch(name) and name.lower() not in visited]
    targets.sort(key=lambda repo: (repo.get("fork", False), -repo.get("stargazers_count", 0), repo["full_name"].lower()))
    print(f"Indexed {len(repos)} repositories; checking {len(targets)} candidate snapshots or trees.", flush=True)
    provenance, seen = list(previous.get("sources", [])), {}
    for source in provenance:
        if source["duplicate"]:
            continue
        signature = source.get("sha256") or existing_signature(args.output, source)
        source["sha256"] = signature
        seen[signature] = source["stored"]
    indexed = set(repos) | {entry["repository"].lower() for entry in scanned} | set(previous.get("indexed_names", []))
    deferred = sorted(set(previous.get("deferred_large_repositories", [])) | set(deferred))
    indexed.update(name.lower() for name in deferred)
    pending = {repo["full_name"].lower(): repo for repo in
               (previous.get("pending_repositories", []) if args.probe_large else targets)}
    def save_manifest():
        manifest = dict(discovery=discovery, indexed_repositories=len(indexed), indexed_names=sorted(indexed), scanned=scanned,
                        deferred_large_repositories=deferred, pending_repositories=list(pending.values()), unique_shaders=len(seen), sources=provenance)
        temporary = manifest_path.with_suffix(".tmp")
        temporary.write_text(json.dumps(manifest, indent=2, ensure_ascii=False) + "\n")
        temporary.replace(manifest_path)
    save_manifest()
    scan = probe_large if args.probe_large else snapshot
    with concurrent.futures.ThreadPoolExecutor(max_workers=14 if args.probe_large else 20) as executor:
        for repo, files, licenses, error in executor.map(scan, targets):
            name = repo["full_name"]
            branch = repo.get("default_branch") or "HEAD"
            destination = args.output.joinpath(*name.split("/"))
            scanned.append(dict(repository=name, branch=branch, found=len(files), error=error,
                                method="git-tree" if args.probe_large else "archive"))
            if args.probe_large and name in deferred:
                deferred.remove(name)
            if files:
                for relative, contents in licenses.items():
                    target = destination / relative
                    target.parent.mkdir(parents=True, exist_ok=True)
                    if not target.exists():
                        target.write_bytes(contents)
            for path, code, assets, relevant in files:
                signature = signature_for(code, assets)
                output = PurePosixPath(path)
                if output.suffix.lower() not in {".fs", ".isf"}:
                    output = output.with_suffix(output.suffix + ".fs")
                stored = str(PurePosixPath(name) / output)
                duplicate = seen.get(signature)
                provenance.append(dict(repository=name, source=path, branch=branch, stored=duplicate or stored,
                                       duplicate=bool(duplicate), licenses=relevant, sha256=signature))
                if duplicate:
                    continue
                seen[signature] = stored
                copied_assets = dict(assets)
                companion = str(PurePosixPath(path).with_suffix(".vs"))
                if companion in assets and str(output) != path:
                    copied_assets[str(output.with_suffix(".vs"))] = assets[companion]
                for relative, data in {**copied_assets, **{p: licenses[p] for p in relevant}, str(output): code}.items():
                    target = destination / relative
                    target.parent.mkdir(parents=True, exist_ok=True)
                    if not target.exists():
                        target.write_bytes(data)
            pending.pop(name.lower(), None)
            if files:
                print(f"{name}: {len(files)} ISF", flush=True)
            if len(scanned) % 100 == 0:
                save_manifest()
                print(f"{len(scanned)} archives attempted, {len(seen)} unique shaders so far.", flush=True)
    save_manifest()
    print(f"{len(seen)} unique shaders, {len(provenance)} source files, {len(scanned)} archives scanned.", flush=True)

if __name__ == "__main__":
    main()
