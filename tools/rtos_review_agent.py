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


def _parse_json_response(raw_text: str) -> dict | None:
    """Strip markdown fences and parse JSON. Returns None on failure."""
    text = raw_text.strip()
    if text.startswith("```"):
        lines = text.splitlines()
        lines = [l for l in lines if not l.strip().startswith("```")]
        text = "\n".join(lines)
    try:
        return json.loads(text)
    except json.JSONDecodeError:
        return None


# Authoritative severity for each rule — model output is overridden if it disagrees.
RULE_SEVERITY = {
    "ISR_UNSAFE_API": "CRITICAL",
    "LOCK_ORDER": "CRITICAL",
    "PRIORITY_INVERSION": "CRITICAL",
    "SHARED_STATE": "CRITICAL",
    "RACE_CONDITION": "CRITICAL",
    "BLOCKING_IN_CRITICAL": "CRITICAL",
    "CALLBACK_UNDER_LOCK": "WARNING",
    "UNBOUNDED_WAIT": "WARNING",
    "STACK_OVERFLOW": "WARNING",
    "CORE_AFFINITY": "WARNING",
    "MEMORY_LEAK": "WARNING",
    "EVENT_BUS_MISUSE": "WARNING",
    "WATCHDOG_STARVATION": "WARNING",
    "WRAPPER_BYPASS": "WARNING",
}


def _enforce_severity(findings: list[dict]) -> list[dict]:
    """Override severity to match the rule table. Logs corrections."""
    for f in findings:
        rule = f.get("rule", "")
        expected = RULE_SEVERITY.get(rule)
        if expected and f.get("severity") != expected:
            print(
                f"  Severity override: {rule} {f.get('severity')} → {expected}",
                file=sys.stderr,
            )
            f["severity"] = expected
    return findings


def _recount_summary(findings: list[dict]) -> dict:
    """Recompute summary counts from a findings list."""
    return {
        "critical": sum(1 for f in findings if f.get("severity") == "CRITICAL"),
        "warning": sum(1 for f in findings if f.get("severity") == "WARNING"),
        "info": sum(1 for f in findings if f.get("severity") == "INFO"),
    }


def review_file(
    client: anthropic.Anthropic,
    model: str,
    file_path: str,
    verify: bool = True,
) -> dict:
    """Send a file to Claude for RTOS review, optionally verify, return findings."""
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

    # --- Pass 1: Initial review ---
    response = client.messages.create(
        model=model,
        max_tokens=4096,
        temperature=0,
        system=system_prompt,
        messages=[{"role": "user", "content": user_message}],
    )

    raw_text = response.content[0].text.strip()
    result = _parse_json_response(raw_text)

    if result is None:
        print(f"Warning: failed to parse JSON for {file_path}", file=sys.stderr)
        print(f"Raw response:\n{raw_text}", file=sys.stderr)
        return {
            "file": filename,
            "findings": [],
            "summary": {"critical": 0, "warning": 0, "info": 0},
            "parse_error": "JSON parse failed on initial review",
        }

    # --- Pass 2: Self-verification ---
    if verify and result.get("findings"):
        _print_color(
            f"    Verifying {len(result['findings'])} finding(s)...",
            "cyan",
            True,
        )
        result = _verify_findings(client, model, source, filename, result)

    # Enforce severity from rule table (hard override, not model-dependent)
    if result.get("findings"):
        result["findings"] = _enforce_severity(result["findings"])
        result["summary"] = _recount_summary(result["findings"])

    return result


def _verify_findings(
    client: anthropic.Anthropic,
    model: str,
    source: str,
    filename: str,
    result: dict,
) -> dict:
    """Ask the model to self-review its findings and drop false positives."""
    findings_json = json.dumps(result["findings"], indent=2)

    verify_message = f"""You previously reviewed `{filename}` and produced these findings:

```json
{findings_json}
```

Source code:
```c
{source}
```

For each finding, apply these DROP checks:
- DROP if severity does not match rule table (e.g. CALLBACK_UNDER_LOCK must be WARNING, never CRITICAL)
- DROP if it flags a bounded wait (fixed timeout + return value checked) as UNBOUNDED_WAIT
- DROP if the detail contains factual errors about the code (e.g. claiming a leak where free() exists on all paths)
- DROP if it flags a standard RTOS pattern (queue/semaphore OS_WAIT_FOREVER in a consumer task)

Return ONLY a JSON array of findings to KEEP (same schema). Empty array `[]` if none survive.
No new findings. No markdown fences."""

    response = client.messages.create(
        model=model,
        max_tokens=4096,
        temperature=0,
        system="You are verifying RTOS review findings. Be skeptical — your job is to eliminate false positives. Only keep findings where the bug is provably present in the code.",
        messages=[{"role": "user", "content": verify_message}],
    )

    raw_text = response.content[0].text.strip()
    verified = _parse_json_response(raw_text)

    if verified is None:
        print(f"  Warning: verification parse failed, keeping original findings", file=sys.stderr)
        return result

    # Handle both bare array and wrapped {"findings": [...]} responses
    if isinstance(verified, dict):
        verified = verified.get("findings", [])

    dropped = len(result.get("findings", [])) - len(verified)
    if dropped > 0:
        _print_color(f"    Verification dropped {dropped} finding(s)", "yellow", True)

    result["findings"] = verified
    result["summary"] = _recount_summary(verified)
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
    parser.add_argument(
        "--no-verify", action="store_true", default=False,
        help="Skip the self-verification pass (faster but more false positives)"
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
        result = review_file(client, model, file_path, verify=not args.no_verify)
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
