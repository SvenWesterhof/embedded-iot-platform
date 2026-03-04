#!/usr/bin/env python3
"""
RTOS Vulnerability Review Agent

Reviews C/H source files for FreeRTOS concurrency bugs, safety violations,
and os_wrapper bypass issues. Uses Claude API for semantic analysis.

Handles semantic/reasoning rules (SHARED_STATE, RACE_CONDITION, etc.)
and structural rules (WRAPPER_BYPASS, ISR_UNSAFE_API, etc.).

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
import re
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


# ---------------------------------------------------------------------------
# Pass 3: Evidence grounding — deterministic validation (no LLM)
# ---------------------------------------------------------------------------

# Regex helpers to extract identifiers the model claims are involved.
_C_IDENT = re.compile(r"\b([a-zA-Z_]\w*)\b")

# Common English words that look like C identifiers due to capitalization
# (appear at start of sentences in finding descriptions).
_ENGLISH_NOISE = {
    "the", "this", "that", "these", "those", "from", "with", "without",
    "which", "where", "when", "while", "between", "before", "after",
    "into", "onto", "upon", "above", "below", "since", "until",
    "global", "local", "static", "struct", "volatile", "const",
    "could", "would", "should", "might", "will", "can", "may",
    "not", "but", "and", "for", "are", "has", "had", "was", "were",
    "does", "did", "been", "being", "also", "both", "each", "any",
    "all", "same", "other", "another", "such", "only", "either",
    "however", "therefore", "because", "although", "concurrent",
    "simultaneously", "multiple", "several", "different", "specific",
    "particular", "potential", "possible", "dangerous", "unsafe",
    "function", "functions", "called", "calling", "runs", "running",
    "creates", "causes", "leads", "results", "means", "uses",
    "accesses", "modifies", "updates", "changes", "sets", "gets",
    "incremented", "decremented", "modified", "updated", "accessed",
    "protected", "unprotected", "synchronized", "unsynchronized",
    "context", "handler", "callback", "interrupt", "operation",
    "fields", "members", "values", "types", "data", "buffer",
    "same", "time", "two", "one", "first", "second", "third",
}

# Per-rule extractors: pull key identifiers from "detail" + "title" that MUST
# appear somewhere near the claimed line.
_RULE_KEYWORDS = {
    "SHARED_STATE": {
        # Expect the variable / field name near the line
        "extract": lambda f: _extract_identifiers(f, ignore={
            "task", "isr", "mutex", "shared", "state", "variable", "protect",
            "unprotected", "access", "read", "write", "critical", "section",
        }),
        "window": 15,  # wider: struct defs can span many lines before the var name
    },
    "RACE_CONDITION": {
        "extract": lambda f: _extract_identifiers(f, ignore={
            "race", "condition", "toctou", "atomic", "check", "act", "read",
            "modify", "write", "lock", "without",
        }),
        "window": 5,
    },
    "LOCK_ORDER": {
        "extract": lambda f: _extract_mutex_names(f),
        "window": 10,
    },
    "CALLBACK_UNDER_LOCK": {
        "extract": lambda f: _extract_identifiers(f, ignore={
            "callback", "lock", "mutex", "hold", "invoke", "call", "under",
            "deadlock", "priority", "inversion",
        }),
        "window": 5,
    },
    "UNBOUNDED_WAIT": {
        "extract": lambda f: _extract_identifiers(f, ignore={
            "unbounded", "wait", "forever", "timeout", "blocking", "return",
            "value", "unchecked",
        }),
        "window": 5,
    },
    "MEMORY_LEAK": {
        "extract": lambda f: _extract_alloc_names(f),
        "window": 10,
    },
    "STACK_OVERFLOW": {
        "extract": lambda f: _extract_identifiers(f, ignore={
            "stack", "overflow", "size", "depth", "large", "recursive",
            "allocation", "frame", "bytes",
        }),
        "window": 5,
    },
    "PRIORITY_INVERSION": {
        "extract": lambda f: _extract_identifiers(f, ignore={
            "priority", "inversion", "high", "low", "task", "mutex",
            "inheritance", "resource",
        }),
        "window": 10,
    },
    "EVENT_BUS_MISUSE": {
        "extract": lambda f: _extract_identifiers(f, ignore={
            "event", "bus", "layer", "direct", "call", "publish", "subscribe",
        }),
        "window": 5,
    },
}


def _extract_identifiers(
    finding: dict, ignore: set[str]
) -> list[str]:
    """Extract C identifiers from title+detail+bug_flow, filtering noise words."""
    text = f"{finding.get('title', '')} {finding.get('detail', '')} {finding.get('bug_flow', '')}"
    idents = _C_IDENT.findall(text)
    # Keep identifiers that look like code (contain underscore, or are camelCase),
    # skip English words and short noise.
    combined_ignore = _ENGLISH_NOISE | ignore
    result = []
    for ident in idents:
        low = ident.lower()
        if low in combined_ignore or len(ident) < 3:
            continue
        # Strong signal: underscores are almost always C identifiers
        if "_" in ident:
            result.append(ident)
            continue
        # Weak signal: capitalized single word without underscore.
        # Only keep if it looks like a type/macro (ALL_CAPS) or camelCase.
        if re.match(r"^[A-Z][A-Z0-9]+$", ident):
            # ALL_CAPS like PROTOCOL_MAX or ISR — likely a macro
            result.append(ident)
        elif re.match(r"^[a-z]+[A-Z]", ident):
            # camelCase like txBuffer, seqCounter
            result.append(ident)
        # Skip: plain capitalized English words (Global, Concurrent, etc.)
    return list(dict.fromkeys(result))  # dedupe preserving order


def _extract_mutex_names(finding: dict) -> list[str]:
    """Extract mutex/lock names from a LOCK_ORDER finding."""
    text = f"{finding.get('title', '')} {finding.get('detail', '')}"
    # Look for os_mutex_* handles or *_mutex patterns
    mutex_pat = re.compile(r"\b(\w*mutex\w*)\b", re.IGNORECASE)
    names = mutex_pat.findall(text)
    # Also grab any identifier after "lock" or "acquire"
    names.extend(_extract_identifiers(finding, ignore={
        "lock", "order", "deadlock", "inconsistent", "acquisition", "mutex",
        "across", "function", "risk",
    }))
    return list(dict.fromkeys(names))


def _extract_alloc_names(finding: dict) -> list[str]:
    """Extract allocation-related names from a MEMORY_LEAK finding."""
    text = f"{finding.get('title', '')} {finding.get('detail', '')}"
    # Look for malloc/calloc/pvPortMalloc and variable names
    alloc_pat = re.compile(r"\b((?:pv)?(?:Port)?[Mm]alloc|calloc|free|pvPortFree)\b")
    names = alloc_pat.findall(text)
    names.extend(_extract_identifiers(finding, ignore={
        "memory", "leak", "allocated", "freed", "path", "all", "code",
        "malloc", "calloc", "free", "pvPortMalloc", "pvPortFree",
    }))
    return list(dict.fromkeys(names))


def _ground_findings(
    findings: list[dict], source_lines: list[str]
) -> tuple[list[dict], list[dict]]:
    """Validate findings against actual source. Returns (kept, dropped)."""
    kept = []
    dropped = []
    total_lines = len(source_lines)

    for f in findings:
        line = f.get("line", 0)
        rule = f.get("rule", "")

        # Check 1: Line number in range
        if not isinstance(line, int) or line < 1 or line > total_lines:
            f["grounding_reason"] = f"line {line} out of range (file has {total_lines} lines)"
            dropped.append(f)
            continue

        # Check 2: Rule-specific keyword validation
        rule_config = _RULE_KEYWORDS.get(rule)
        if rule_config is None:
            # Unknown rule — keep (don't drop things we can't validate)
            kept.append(f)
            continue

        keywords = rule_config["extract"](f)
        if not keywords:
            # No extractable keywords — can't validate, keep it
            kept.append(f)
            continue

        window = rule_config["window"]
        start = max(0, line - 1 - window)
        end = min(total_lines, line + window)
        nearby_text = " ".join(source_lines[start:end])

        # At least one keyword must appear in the nearby source
        matched = [kw for kw in keywords if kw in nearby_text]
        if not matched:
            f["grounding_reason"] = (
                f"none of [{', '.join(keywords[:5])}] found near line {line} "
                f"(searched lines {start+1}-{end})"
            )
            dropped.append(f)
            continue

        kept.append(f)

    return kept, dropped


def review_file(
    client: anthropic.Anthropic,
    model: str,
    file_path: str,
    verify: bool = True,
    ground: bool = True,
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

    # --- Pass 3: Evidence grounding (deterministic, no LLM) ---
    if ground and result.get("findings"):
        # Get raw source lines (without line number prefixes)
        raw_lines = Path(file_path).read_text(
            encoding="utf-8", errors="replace"
        ).splitlines()
        kept, dropped = _ground_findings(result["findings"], raw_lines)
        if dropped:
            _print_color(
                f"    Grounding dropped {len(dropped)} finding(s):",
                "yellow",
                True,
            )
            for d in dropped:
                _print_color(
                    f"      - {d.get('rule', '?')} line {d.get('line', '?')}: "
                    f"{d.get('grounding_reason', 'unknown')}",
                    "yellow",
                    True,
                )
        result["findings"] = kept
        if dropped:
            result["grounded_dropped"] = [
                {
                    "rule": d.get("rule"),
                    "line": d.get("line"),
                    "title": d.get("title"),
                    "reason": d.get("grounding_reason"),
                }
                for d in dropped
            ]

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
- DROP CALLBACK_UNDER_LOCK if no os_mutex_take/os_mutex_give pair is visible in the code around the callback call — do not speculate about internal library locks

For each finding you KEEP, you MUST add a "bug_flow" field that describes the concrete execution sequence that triggers the bug. Format:

"bug_flow": "1. Task A calls func_x() and reads var at line N\\n2. Preemption/interrupt occurs here\\n3. Task B / ISR calls func_y() and writes var at line M\\n4. Task A resumes with stale value → consequence"

The bug_flow must reference specific task names (or ISR/callback names), function names, line numbers, and variable names from the code. If you cannot construct a concrete flow with real identifiers from this file, DROP the finding.

Return ONLY a JSON array of findings to KEEP (same schema, plus "bug_flow"). Empty array `[]` if none survive.
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
    parser.add_argument(
        "--no-ground", action="store_true", default=False,
        help="Skip the evidence grounding pass (deterministic keyword validation)"
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
        result = review_file(
            client, model, file_path,
            verify=not args.no_verify,
            ground=not args.no_ground,
        )
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
