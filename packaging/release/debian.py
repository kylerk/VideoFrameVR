#!/usr/bin/env python3
"""Resolve the reviewed Debian supplement from checksum-pinned snapshot indices.

No apt upgrade, maintainer scripts or host package manager is involved. The SDK
supplies Platform dependencies. The bundle audit rejects any undeclared gaps.
"""
import json
import lzma
from pathlib import Path
import re

from artifacts import check_age, fetch


def paragraphs(path):
    records = {}
    for paragraph in lzma.decompress(path.read_bytes()).decode().split("\n\n"):
        record = {}
        field = None
        for line in paragraph.splitlines():
            if line.startswith(" ") and field:
                record[field] += "\n" + line[1:]
            elif ":" in line:
                field, value = line.split(":", 1)
                record[field] = value.lstrip()
        if "Package" in record:
            records[record["Package"]] = record
    return records


def supplement(cache, architecture, offline=False, lock_path=None):
    lock = json.loads((lock_path or Path(__file__).parent / "debian.json").read_text())
    check_age(lock["published_before"])
    records = {}
    for arch in {architecture, "amd64", "source"}:
        index = lock["indices"][arch]
        file = fetch(lock["snapshot"] + index["path"],
                     cache / "downloads" / (index["sha256"] + ".xz"),
                     index["sha256"], offline=offline)
        records[arch] = paragraphs(file)
    binaries = []
    for role, arch in [("runtime", architecture), ("development", architecture), ("build_tools", "amd64")]:
        for name in lock[role]:
            record = records[arch][name]
            filename = Path(record["Filename"]).name
            fetch(lock["snapshot"] + record["Filename"], cache / "downloads" / filename,
                  record["SHA256"], offline=offline)
            source = re.sub(r" \(.*", "", record.get("Source", name))
            binaries.append({"name": name, "version": record["Version"], "architecture": arch,
                             "file": filename, "sha256": record["SHA256"], "source": source,
                             "role": role, "homepage": record.get("Homepage", "Debian snapshot")})
    sources = []
    names = set(lock["static_sources"]) | {binary["source"] for binary in binaries}
    for name in sorted(names):
        record = records["source"][name]
        files = []
        for line in record["Checksums-Sha256"].splitlines():
            if not line.strip():
                continue
            digest, size, filename = line.split()
            url = lock["snapshot"] + record["Directory"] + "/" + filename
            fetch(url, cache / "downloads" / filename, digest, offline=offline)
            files.append({"file": filename, "sha256": digest, "url": url})
        sources.append({"name": name, "version": record["Version"], "files": files})
    return {"snapshot": lock["snapshot"], "binaries": binaries, "sources": sources}
