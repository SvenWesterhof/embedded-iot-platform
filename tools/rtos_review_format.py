#!/usr/bin/env python3
"""
Format rtos_review_agent.py JSON output as GitHub-flavored markdown.

Usage:
    python tools/rtos_review_format.py <review_output.json> >> "$GITHUB_STEP_SUMMARY"

Exit codes:
    0   No critical findings
    1   One or more critical findings (so the workflow step can propagate failure)
    2   Could not read or parse the JSON file
"""

import io
import json
import sys

# Force UTF-8 output — needed on Windows when redirecting to a file or pipe.
sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding="utf-8", errors="replace")


SEVERITY_ICON = {"CRITICAL": "🔴", "WARNING": "⚠️", "INFO": "ℹ️"}
VERIFY_ICON = {"CONFIRMED": "✅", "AGENT_ONLY": "—", "DROPPED": "❌"}


def format_result(result: dict) -> list[str]:
    filename = result.get("file", "unknown")
    findings = result.get("findings", [])
    summary = result.get("summary", {})

    lines = [f"### `{filename}`"]

    if result.get("parse_error"):
        lines.append(f"> ⚠️ Could not parse agent response: {result['parse_error']}")
        return lines

    if not findings:
        lines.append("No findings.")
        return lines

    # Separate active findings from dropped ones
    active = [f for f in findings if f.get("verification_status") != "DROPPED"]
    dropped = [f for f in findings if f.get("verification_status") == "DROPPED"]

    c = sum(1 for f in active if f.get("severity") == "CRITICAL")
    w = sum(1 for f in active if f.get("severity") == "WARNING")
    i = sum(1 for f in active if f.get("severity") == "INFO")
    lines.append(f"**{c} critical &nbsp;·&nbsp; {w} warning &nbsp;·&nbsp; {i} info**\n")

    # Check if any finding has verification data
    has_verification = any(f.get("verification_status") for f in findings)

    # Summary table
    if has_verification:
        lines.append("| Severity | Confidence | Verified | Line | Rule | Title |")
        lines.append("|---|---|---|---|---|---|")
    else:
        lines.append("| Severity | Confidence | Line | Rule | Title |")
        lines.append("|---|---|---|---|---|")

    for f in active:
        sev = f.get("severity", "INFO")
        icon = SEVERITY_ICON.get(sev, "")
        conf = f.get("confidence", "—")
        vstatus = f.get("verification_status", "")
        vicon = VERIFY_ICON.get(vstatus, "—")
        if has_verification:
            lines.append(
                f"| {icon}&nbsp;{sev} | {conf} | {vicon} | {f.get('line', '?')} "
                f"| `{f.get('rule', '?')}` | {f.get('title', '')} |"
            )
        else:
            lines.append(
                f"| {icon}&nbsp;{sev} | {conf} | {f.get('line', '?')} "
                f"| `{f.get('rule', '?')}` | {f.get('title', '')} |"
            )

    # Detailed findings (active only)
    lines.append("")
    for f in active:
        sev = f.get("severity", "INFO")
        icon = SEVERITY_ICON.get(sev, "")
        conf = f.get("confidence", "—")
        vstatus = f.get("verification_status", "")
        vicon = VERIFY_ICON.get(vstatus, "")
        vsuffix = f" · {vicon}&nbsp;{vstatus}" if vstatus else ""
        lines.append(
            f"#### {icon} {f.get('title', '')} "
            f"<sup>line&nbsp;{f.get('line', '?')} · `{f.get('rule', '?')}` · confidence:&nbsp;{conf}{vsuffix}</sup>"
        )
        lines.append(f"{f.get('detail', '')}\n")
        lines.append(f"> **Suggested fix:** {f.get('suggestion', '')}\n")

    # Collapsed section for dropped findings (transparency/audit)
    if dropped:
        lines.append("")
        lines.append("<details>")
        lines.append(f"<summary>🔍 {len(dropped)} finding(s) disproved by CodeQL (click to expand)</summary>\n")
        for f in dropped:
            sev = f.get("severity", "INFO")
            source = f.get("verification_source", "CodeQL")
            lines.append(
                f"- ~~**{sev}** `{f.get('rule', '?')}` line {f.get('line', '?')}: "
                f"{f.get('title', '')}~~ — *{source}*"
            )
        lines.append("\n</details>")

    return lines


def main() -> int:
    if len(sys.argv) < 2:
        print("Usage: rtos_review_format.py <review_output.json>", file=sys.stderr)
        return 2

    try:
        with open(sys.argv[1]) as fh:
            data = json.load(fh)
    except (OSError, json.JSONDecodeError) as e:
        print(f"Error reading {sys.argv[1]}: {e}", file=sys.stderr)
        return 2

    results = data if isinstance(data, list) else [data]

    total = {"critical": 0, "warning": 0, "info": 0}
    output_lines = ["## RTOS Vulnerability Review\n"]

    for result in results:
        output_lines.extend(format_result(result))
        output_lines.append("")
        # Count only active findings (not DROPPED)
        active = [f for f in result.get("findings", [])
                  if f.get("verification_status") != "DROPPED"]
        total["critical"] += sum(1 for f in active if f.get("severity") == "CRITICAL")
        total["warning"] += sum(1 for f in active if f.get("severity") == "WARNING")
        total["info"] += sum(1 for f in active if f.get("severity") == "INFO")

    c, w, i = total["critical"], total["warning"], total["info"]
    if len(results) > 1:
        output_lines.append(
            f"---\n**Total across {len(results)} files: "
            f"{c} critical &nbsp;·&nbsp; {w} warning &nbsp;·&nbsp; {i} info**"
        )

    print("\n".join(output_lines))
    return 1 if c > 0 else 0


if __name__ == "__main__":
    sys.exit(main())
