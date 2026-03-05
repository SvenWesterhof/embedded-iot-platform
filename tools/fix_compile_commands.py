#!/usr/bin/env python3
"""Preprocess an ARM GCC compile_commands.json for host clang-tidy analysis.

Three problems this script solves:

1. Non-canonical include paths
   CMake's stm32cubemx integration emits paths like:
     -I../../cmake/stm32cubemx/../../Drivers/STM32F7xx_HAL_Driver/Inc
   These contain '..' segments that confuse clang-tidy's include resolver.
   We resolve every -I path relative to the entry's 'directory' field and
   write back a fully-normalised absolute path.

2. GCC-only flags that clang rejects
   Flags like --specs=nano.specs are GCC linker spec files. If they appear
   in a compile command (should not, but occasionally do), clang errors on
   them, which causes it to silently drop all remaining flags — including
   the include paths. We strip them before analysis.

3. Docker container path remapping (--remap OLD:NEW)
   When compile_commands.json is generated inside a Docker container (e.g.
   espressif/esp-idf-ci-action mounts the project at /project), all file
   and include paths use the in-container prefix. Pass --remap /project/:/host/path/
   to translate those paths to where the files actually live on the analysis host.
   Multiple --remap flags are supported.

Usage:
    python3 tools/fix_compile_commands.py <input.json> <output.json> [--remap OLD:NEW ...]

The output file is used only for analysis, never for building.
"""

import json
import os
import sys


# Exact flags to strip
_STRIP_EXACT = frozenset({
    "--specs=nano.specs",
    "--specs=nosys.specs",
    "--specs=rdimon.specs",
})

# Flag prefixes to strip (e.g. any --specs=... variant)
_STRIP_PREFIXES = ("--specs=",)


def _is_gcc_only(flag: str) -> bool:
    return flag in _STRIP_EXACT or any(flag.startswith(p) for p in _STRIP_PREFIXES)


def _remap_path(path: str, remaps: list) -> str:
    """Apply the first matching prefix remap to path."""
    for old, new in remaps:
        if path.startswith(old):
            return new + path[len(old):]
    return path


def _normalise_include(flag: str, build_dir: str, remaps: list) -> str:
    """Resolve a -I<path> flag to a canonical absolute path, then remap."""
    if not flag.startswith("-I"):
        return flag

    raw = flag[2:]  # strip leading -I

    # Handle -I with a space before the path (shouldn't happen here but be safe)
    if not raw:
        return flag

    # Resolve to absolute path
    if os.path.isabs(raw):
        resolved = os.path.normpath(raw)
    else:
        # Relative path — resolve against the compilation directory
        resolved = os.path.normpath(os.path.join(build_dir, raw))

    return "-I" + _remap_path(resolved, remaps)


def fix_entry(entry: dict, remaps: list) -> dict:
    """Return a new entry with cleaned flags, normalised include paths, and remapped paths."""
    build_dir = entry.get("directory", "")

    # Remap the file path itself (Docker container → host path)
    file_path = entry.get("file", "")
    if file_path and remaps:
        entry = dict(entry, file=_remap_path(file_path, remaps))

    if "command" in entry:
        # Shell-style command string — split naively on spaces.
        # This is sufficient because CMake never puts spaces inside flag values
        # for -I paths (it uses -I with a concatenated path, never -I "path").
        tokens = entry["command"].split()
        fixed = []
        skip_next = False
        for i, tok in enumerate(tokens):
            if skip_next:
                skip_next = False
                continue
            if _is_gcc_only(tok):
                continue
            if tok == "-I" and i + 1 < len(tokens):
                # -I <path> with a space — merge and normalise
                fixed.append(_normalise_include("-I" + tokens[i + 1], build_dir, remaps))
                skip_next = True
                continue
            fixed.append(_normalise_include(tok, build_dir, remaps))
        entry = dict(entry, command=" ".join(fixed))

    elif "arguments" in entry:
        args = entry["arguments"]
        fixed = []
        skip_next = False
        for i, arg in enumerate(args):
            if skip_next:
                skip_next = False
                continue
            if _is_gcc_only(arg):
                continue
            if arg == "-I" and i + 1 < len(args):
                fixed.append(_normalise_include("-I" + args[i + 1], build_dir, remaps))
                skip_next = True
                continue
            fixed.append(_normalise_include(arg, build_dir, remaps))
        entry = dict(entry, arguments=fixed)

    return entry


def main() -> None:
    args = sys.argv[1:]
    remaps = []

    # Parse --remap OLD:NEW flags (may appear multiple times)
    positional = []
    i = 0
    while i < len(args):
        if args[i] == "--remap" and i + 1 < len(args):
            old, _, new = args[i + 1].partition(":")
            if old and new:
                remaps.append((old, new))
            i += 2
        else:
            positional.append(args[i])
            i += 1

    if len(positional) != 2:
        print(
            f"Usage: {sys.argv[0]} <input.json> <output.json> [--remap OLD:NEW ...]",
            file=sys.stderr,
        )
        sys.exit(1)

    src, dst = positional

    with open(src) as f:
        db = json.load(f)

    fixed = [fix_entry(e, remaps) for e in db]

    with open(dst, "w") as f:
        json.dump(fixed, f, indent=2)

    print(f"fix_compile_commands: {len(fixed)} entries written to {dst}")


if __name__ == "__main__":
    main()
