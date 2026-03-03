#!/usr/bin/env python3
"""
RTOS Vulnerability Review Agent

Reviews C/H source files for FreeRTOS concurrency bugs, safety violations,
and os_wrapper bypass issues. Uses Claude API for semantic analysis.

Usage:
    python tools/rtos_review_agent.py <file1.c> [file2.c ...]
    python tools/rtos_review_agent.py --help

Environment:
    ANTHROPIC_API_KEY   Required. Your Anthropic API key.
                        Also loaded from a .env file in the repo root if present.
    RTOS_REVIEW_MODEL   Optional. Model to use (default: claude-sonnet-4-20250514).

Exit codes:
    0   No critical findings
    1   One or more CRITICAL findings
    2   Agent error (API failure, file not found, etc.)
"""

import argparse
import json
import os
import sys
from pathlib import Path

import anthropic
from dotenv import load_dotenv

from rtos_review_prompt import get_system_prompt

# Load .env from repo root (two levels up from tools/).
# Does nothing if the file doesn't exist, so CI environments relying on
# real environment variables are unaffected.
_REPO_ROOT = Path(__file__).parent.parent
load_dotenv(_REPO_ROOT / ".env")


def detect_platform(file_path: str) -> str:
    """Detect target platform from file path."""
    normalized = file_path.replace("\\", "/").lower()
    if "/esp32/" in normalized or normalized.startswith("esp32/"):
        return "esp32"
    elif "/stm32/" in normalized or normalized.startswith("stm32/"):
        return "stm32"
    else:
        return "common"


def read_source_file(file_path: str) -> str:
    """Read a source file and return numbered lines."""
    path = Path(file_path)
    if not path.exists():
        print(f"Error: file not found: {file_path}", file=sys.stderr)
        sys.exit(2)
    if not path.suffix in (".c", ".h"):
        print(f"Warning: {file_path} is not a .c/.h file", file=sys.stderr)

    content = path.read_text(encoding="utf-8", errors="replace")
    # Add line numbers so the model can reference specific lines
    lines = content.splitlines()
    numbered = "\n".join(f"{i+1:4d} | {line}" for i, line in enumerate(lines))
    return numbered


def review_file(client: anthropic.Anthropic, model: str, file_path: str) -> dict:
    """Send a file to Claude for RTOS review and return parsed findings."""
    platform = detect_platform(file_path)
    system_prompt = get_system_prompt(platform)
    source = read_source_file(file_path)
    filename = Path(file_path).name

    user_message = f"""Review this file for RTOS vulnerabilities.

File: {filename}
Platform: {platform}
Full path: {file_path}

```c
{source}
```"""

    response = client.messages.create(
        model=model,
        max_tokens=4096,
        system=system_prompt,
        messages=[{"role": "user", "content": user_message}],
    )

    raw_text = response.content[0].text.strip()

    # Strip markdown fences if the model wrapped the JSON
    if raw_text.startswith("```"):
        lines = raw_text.splitlines()
        # Remove first line (```json or ```) and last line (```)
        lines = [l for l in lines if not l.strip().startswith("```")]
        raw_text = "\n".join(lines)

    try:
        result = json.loads(raw_text)
    except json.JSONDecodeError as e:
        print(f"Warning: failed to parse JSON for {file_path}: {e}", file=sys.stderr)
        print(f"Raw response:\n{raw_text}", file=sys.stderr)
        return {
            "file": filename,
            "findings": [],
            "summary": {"critical": 0, "warning": 0, "info": 0},
            "parse_error": str(e),
        }

    return result


def print_findings(result: dict, use_color: bool = True) -> None:
    """Print findings in a human-readable format to stderr, JSON to stdout."""
    findings = result.get("findings", [])
    filename = result.get("file", "unknown")

    if not findings:
        _print_color(f"\n  {filename}: No findings.", "green", use_color)
        return

    summary = result.get("summary", {})
    critical = summary.get("critical", 0)
    warning = summary.get("warning", 0)
    info = summary.get("info", 0)

    color = "red" if critical > 0 else "yellow" if warning > 0 else "green"
    _print_color(
        f"\n  {filename}: {critical} critical, {warning} warning, {info} info",
        color,
        use_color,
    )

    for f in findings:
        severity = f.get("severity", "INFO")
        sev_color = {"CRITICAL": "red", "WARNING": "yellow"}.get(severity, "cyan")
        _print_color(
            f"\n  [{severity}] {f.get('rule', '?')} (line {f.get('line', '?')})",
            sev_color,
            use_color,
        )
        print(f"    {f.get('title', '')}", file=sys.stderr)
        print(f"    {f.get('detail', '')}", file=sys.stderr)
        print(f"    Fix: {f.get('suggestion', '')}", file=sys.stderr)


def _print_color(text: str, color: str, enabled: bool) -> None:
    """Print colored text to stderr."""
    colors = {
        "red": "\033[91m",
        "yellow": "\033[93m",
        "green": "\033[92m",
        "cyan": "\033[96m",
        "reset": "\033[0m",
    }
    if enabled and sys.stderr.isatty():
        print(f"{colors.get(color, '')}{text}{colors['reset']}", file=sys.stderr)
    else:
        print(text, file=sys.stderr)


def main():
    parser = argparse.ArgumentParser(
        description="Review C/H files for FreeRTOS vulnerabilities using Claude API"
    )
    parser.add_argument(
        "files", nargs="+", help="C/H source files to review"
    )
    parser.add_argument(
        "--json", action="store_true", default=False,
        help="Output raw JSON to stdout (default: human-readable to stderr + JSON to stdout)"
    )
    parser.add_argument(
        "--model", default=None,
        help="Claude model to use (default: env RTOS_REVIEW_MODEL or claude-sonnet-4-20250514)"
    )
    args = parser.parse_args()

    api_key = os.environ.get("ANTHROPIC_API_KEY")
    if not api_key:
        print("Error: ANTHROPIC_API_KEY environment variable not set", file=sys.stderr)
        sys.exit(2)

    model = args.model or os.environ.get("RTOS_REVIEW_MODEL", "claude-sonnet-4-20250514")
    client = anthropic.Anthropic(api_key=api_key)

    all_results = []
    has_critical = False

    for file_path in args.files:
        _print_color(f"\n  Reviewing: {file_path} ...", "cyan", True)
        result = review_file(client, model, file_path)
        all_results.append(result)

        if not args.json:
            print_findings(result)

        if result.get("summary", {}).get("critical", 0) > 0:
            has_critical = True

    # Always output JSON to stdout for piping/parsing
    if len(all_results) == 1:
        print(json.dumps(all_results[0], indent=2))
    else:
        print(json.dumps(all_results, indent=2))

    sys.exit(1 if has_critical else 0)


if __name__ == "__main__":
    main()
