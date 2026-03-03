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

    c = summary.get("critical", 0)
    w = summary.get("warning", 0)
    i = summary.get("info", 0)
    lines.append(f"**{c} critical &nbsp;·&nbsp; {w} warning &nbsp;·&nbsp; {i} info**\n")

    # Summary table
    lines.append("| Severity | Confidence | Line | Rule | Title |")
    lines.append("|---|---|---|---|---|")
    for f in findings:
        sev = f.get("severity", "INFO")
        icon = SEVERITY_ICON.get(sev, "")
        conf = f.get("confidence", "—")
        lines.append(
            f"| {icon}&nbsp;{sev} | {conf} | {f.get('line', '?')} "
            f"| `{f.get('rule', '?')}` | {f.get('title', '')} |"
        )

    # Detailed findings
    lines.append("")
    for f in findings:
        sev = f.get("severity", "INFO")
        icon = SEVERITY_ICON.get(sev, "")
        conf = f.get("confidence", "—")
        lines.append(
            f"#### {icon} {f.get('title', '')} "
            f"<sup>line&nbsp;{f.get('line', '?')} · `{f.get('rule', '?')}` · confidence:&nbsp;{conf}</sup>"
        )
        lines.append(f"{f.get('detail', '')}\n")
        lines.append(f"> **Suggested fix:** {f.get('suggestion', '')}\n")

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
        s = result.get("summary", {})
        for k in total:
            total[k] += s.get(k, 0)

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
