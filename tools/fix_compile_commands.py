#!/usr/bin/env python3
"""Preprocess an ARM GCC compile_commands.json for host clang-tidy analysis.

Two problems this script solves:

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

Usage:
    python3 tools/fix_compile_commands.py <input.json> <output.json>

The output file is used only for analysis, never for building.
"""

import json
import os
import re
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


def _normalise_include(flag: str, build_dir: str) -> str:
    """Resolve a -I<path> flag to a canonical absolute path."""
    if not flag.startswith("-I"):
        return flag

    raw = flag[2:]  # strip leading -I

    # Handle -I with a space before the path (shouldn't happen here but be safe)
    if not raw:
        return flag

    # If the path is already absolute, just normalise it
    if os.path.isabs(raw):
        return "-I" + os.path.normpath(raw)

    # Relative path — resolve against the compilation directory
    return "-I" + os.path.normpath(os.path.join(build_dir, raw))


def fix_entry(entry: dict) -> dict:
    """Return a new entry with cleaned flags and normalised include paths."""
    build_dir = entry.get("directory", "")

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
                fixed.append(_normalise_include("-I" + tokens[i + 1], build_dir))
                skip_next = True
                continue
            fixed.append(_normalise_include(tok, build_dir))
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
                fixed.append(_normalise_include("-I" + args[i + 1], build_dir))
                skip_next = True
                continue
            fixed.append(_normalise_include(arg, build_dir))
        entry = dict(entry, arguments=fixed)

    return entry


def main() -> None:
    if len(sys.argv) != 3:
        print(f"Usage: {sys.argv[0]} <input.json> <output.json>", file=sys.stderr)
        sys.exit(1)

    src, dst = sys.argv[1], sys.argv[2]

    with open(src) as f:
        db = json.load(f)

    fixed = [fix_entry(e) for e in db]

    with open(dst, "w") as f:
        json.dump(fixed, f, indent=2)

    print(f"fix_compile_commands: {len(fixed)} entries written to {dst}")


if __name__ == "__main__":
    main()
