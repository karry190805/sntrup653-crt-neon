#!/usr/bin/env python3
"""pack_results.py - archive the newest run_*/ directory so it can be returned.

The build tree is left out by default.  It is around 40 MB of object files and
-fstack-usage side files, it is reproduced in seconds by build.sh, and the run
stays fully auditable without it.  Set INCLUDE_BUILD=1 to keep it anyway.

The archive lands next to this script:

    <package directory name>-results-<UTC stamp>.zip

usage: python3 pack_results.py
       INCLUDE_BUILD=1 python3 pack_results.py
"""
import datetime
import os
import pathlib
import sys
import zipfile

root = pathlib.Path(__file__).resolve().parent
include_build = os.environ.get("INCLUDE_BUILD", "0") == "1"

runs = sorted((p for p in root.glob("run_*") if p.is_dir()),
              key=lambda p: p.stat().st_mtime)
if not runs:
    print("result_archive=FAIL reason=no_run_directory")
    print("  nothing to pack: run the campaign first, e.g. bash ./run_on_target.sh")
    raise SystemExit(1)

run = runs[-1]
if len(runs) > 1:
    print("result_archive=note runs_present=%d using=%s" % (len(runs), run.name))

stamp = datetime.datetime.now(datetime.timezone.utc).strftime("%Y%m%dT%H%M%SZ")
archive = root / f"{root.name}-results-{stamp}.zip"

extras = ["SOURCE_MANIFEST.sha256", "platform.conf", "protocol.conf",
          "expected_oracle_hashes.txt", "expected_production_core.sha256",
          "README.md", "VERIFICATION.md", "RANGE_AND_SECURITY.md",
          "LOCAL_VALIDATION.md", "REVIEWER_GUIDE.md"]

files = 0
skipped_build = 0
with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as zf:
    for name in extras:
        path = root / name
        if path.is_file():
            zf.write(path, name)
            files += 1
    for path in sorted(run.rglob("*")):
        if not path.is_file():
            continue
        rel = path.relative_to(run)
        if not include_build and any(part == "build" or part.startswith("build_")
                                     for part in rel.parts[:-1]):
            skipped_build += 1
            continue
        zf.write(path, run.name + "/" + rel.as_posix())
        files += 1

print("run_root=%s" % run.name)
print("build_included=%s files_skipped_build=%d"
      % ("yes" if include_build else "no", skipped_build))
print("result_archive=PASS path=%s files=%d bytes=%d"
      % (archive.name, files, archive.stat().st_size))
