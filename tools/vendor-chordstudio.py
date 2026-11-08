#!/usr/bin/env python3
"""Vendor the ChordStudio sources this repository compiles against, and lock them.

    python tools/vendor-chordstudio.py [path-to-ChordStudio-checkout]

The dependency has no git remote, so it cannot be pinned by commit. This copies
the transitive include closure into third_party/chordstudio, preserving relative
paths, and writes lock.cmake next to the copy. CMake refuses to configure when a
vendored file no longer matches its hash, which is the commit-pin equivalent.

Seeds are both sides of the interface: the `${NC_PLUGINS_ROOT}/*.cpp` listed in
app/CMakeLists.txt, and our own sources, because a file like MixRack.cpp includes
ChordStudio headers directly (`fx/Biquad.h`). Walking only the former misses
those, which is a link error the build finds and this tool should not.

Re-run after adopting a newer ChordStudio checkout, then commit
third_party/chordstudio and lock.cmake in the same commit.
"""

import datetime
import hashlib
import os
import re
import shutil
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEST = os.path.join(REPO, "third_party", "chordstudio")
APP_CMAKE = os.path.join(REPO, "app", "CMakeLists.txt")
LOCAL_SOURCE_DIRS = ["app/Source", "engine/include", "engine/src", "plugins"]
LOCAL_EXTS = (".cpp", ".cc", ".cxx", ".h", ".hpp", ".inl")
LICENCE_ROOTS = ["", "plugins/mixology", "plugins/Open Mic/mic-preamp-modeler",
                 "plugins/overlay-fx"]


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as fh:
        for chunk in iter(lambda: fh.read(65536), b""):
            h.update(chunk)
    return h.hexdigest()


def read_mixrack_inputs():
    """Pull the source list and include roots out of app/CMakeLists.txt.

    The build file decides what gets vendored, so a new ChordStudio source
    cannot be compiled without also being copied.
    """
    text = open(APP_CMAKE, encoding="utf-8").read()
    sources = sorted(set(re.findall(r"\$\{NC_PLUGINS_ROOT\}/([^\"\n]+?\.cpp)", text)))

    include_dirs = []
    lines = text.splitlines()
    for i, line in enumerate(lines):
        if "target_include_directories(nc_mixrack" in line:
            for body in lines[i + 1:]:
                include_dirs += re.findall(r"\$\{NC_PLUGINS_ROOT\}/([^\"\n]+?)\"", body)
                if body.strip().endswith(")"):
                    break
            break
    return sources, sorted(set(include_dirs))


def local_seeds():
    seeds = []
    for rel in LOCAL_SOURCE_DIRS:
        base = os.path.join(REPO, rel)
        if not os.path.isdir(base):
            continue
        for dirpath, _, names in os.walk(base):
            seeds += [os.path.join(dirpath, n) for n in names if n.endswith(LOCAL_EXTS)]
    return seeds


def closure(root, seeds, include_dirs):
    """Follow quoted includes from both sides; return files under root."""
    roots = [os.path.join(root, d) for d in include_dirs]
    roots += [os.path.join(REPO, d) for d in ("app/Source", "engine/include")]
    seen, unresolved = set(), set()
    stack = list(seeds)
    while stack:
        path = os.path.normpath(stack.pop())
        if path in seen or not os.path.isfile(path):
            continue
        seen.add(path)
        body = open(path, encoding="utf-8", errors="ignore").read()
        for inc in re.findall(r'#\s*include\s+"([^"]+)"', body):
            hit = None
            for base in [os.path.dirname(path)] + roots:
                cand = os.path.normpath(os.path.join(base, inc))
                if os.path.isfile(cand):
                    hit = cand
                    break
            if hit:
                stack.append(hit)
            elif re.search(r"(^|/)fx/", inc):
                unresolved.add((inc, os.path.relpath(path, REPO)))

    def inside_root(p):
        return os.path.commonpath([os.path.abspath(p), root]) == os.path.abspath(root)

    return {p for p in seen if inside_root(p)}, unresolved


def main():
    root = os.path.abspath(sys.argv[1] if len(sys.argv) > 1
                           else os.path.join(REPO, "..", "ChordStudio-master"))
    if not os.path.isdir(root):
        sys.exit("not a directory: %s" % root)
    if not os.path.isfile(os.path.join(root, "LICENSE")):
        sys.exit("no LICENSE in %s - refusing to vendor unlicensed sources" % root)

    mixrack_sources, include_dirs = read_mixrack_inputs()
    if not mixrack_sources:
        sys.exit("no ${NC_PLUGINS_ROOT}/*.cpp found in app/CMakeLists.txt")

    seeds = [os.path.join(root, s) for s in mixrack_sources] + local_seeds()
    files, unresolved = closure(root, seeds, include_dirs)

    previous = set()
    lock_path = os.path.join(DEST, "lock.cmake")
    if os.path.isfile(lock_path):
        for entry in re.findall(r'"([^"=]+)=', open(lock_path, encoding="utf-8").read()):
            previous.add(entry.replace("/", os.sep))

    copied, added, updated = [], set(), set()
    for path in sorted(files):
        rel = os.path.relpath(path, root)
        out = os.path.join(DEST, rel)
        os.makedirs(os.path.dirname(out), exist_ok=True)
        old = sha256(out) if os.path.isfile(out) else None
        shutil.copy2(path, out)
        copied.append(rel)
        if rel not in previous:
            added.add(rel)
        elif old != sha256(out):
            updated.add(rel)

    removed = previous - set(copied)
    for rel in removed:
        stale = os.path.join(DEST, rel)
        if os.path.isfile(stale):
            os.remove(stale)

    # Licence texts travel with the copy: GPL-3.0 requires them to.
    licences = []
    for sub in LICENCE_ROOTS:
        base = os.path.join(root, sub) if sub else root
        if not os.path.isdir(base):
            continue
        for name in sorted(os.listdir(base)):
            if name.upper().startswith("LICENSE") and os.path.isfile(os.path.join(base, name)):
                rel = os.path.join(sub, name) if sub else name
                out = os.path.join(DEST, rel)
                os.makedirs(os.path.dirname(out), exist_ok=True)
                shutil.copy2(os.path.join(base, name), out)
                licences.append(rel)

    entries = "\n".join('    "%s=%s"' % (rel.replace(os.sep, "/"),
                                        sha256(os.path.join(DEST, rel))) for rel in copied)
    with open(lock_path, "w", encoding="utf-8", newline="\n") as fh:
        fh.write("# Generated by tools/vendor-chordstudio.py - do not edit.\n"
                 "# sha256 of every vendored file. cmake/ChordStudioDeps.cmake uses\n"
                 "# this list as both the required-file check and the drift check.\n"
                 "set(NC_CHORDSTUDIO_LOCK\n%s)\n" % entries)

    total = sum(os.path.getsize(os.path.join(DEST, rel)) for rel in copied)
    with open(os.path.join(DEST, "PROVENANCE.md"), "w", encoding="utf-8", newline="\n") as fh:
        fh.write(
            "# Vendored ChordStudio sources\n\n"
            "Copied from `%s` on %s.\n\n"
            "- %d files, %.1f KB: the include closure of the `nc_mixrack` sources\n"
            "  and of this repository's own sources that include ChordStudio headers.\n"
            "- Licence: GPL-3.0 (`LICENSE`); the mix rack links it statically, so the\n"
            "  derivative is GPL-3.0. See `docs/DEPENDENCIES.md`.\n"
            "- `lock.cmake` holds a sha256 per file; CMake fails when one drifts.\n\n"
            "Refresh with `python tools/vendor-chordstudio.py <checkout>`, then commit\n"
            "this directory and `lock.cmake` together. Never edit these files in place:\n"
            "changes belong upstream in ChordStudio.\n"
            % (root.replace("\\", "/"), datetime.date.today().isoformat(),
               len(copied), total / 1024.0))

    print("vendored %d files, %.1f KB -> %s" % (len(copied), total / 1024.0, DEST))
    print("added %d, updated %d, removed %d" % (len(added), len(updated), len(removed)))
    for rel in sorted(added):
        print("  + %s" % rel.replace(os.sep, "/"))
    for rel in sorted(updated):
        print("  ~ %s" % rel.replace(os.sep, "/"))
    for rel in sorted(removed):
        print("  - %s" % rel.replace(os.sep, "/"))
    print("licences: %s" % ", ".join(l.replace(os.sep, "/") for l in licences))
    for inc, where in sorted(unresolved):
        print("UNRESOLVED fx include %s <- %s" % (inc, where))
    if unresolved:
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
