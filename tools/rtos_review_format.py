#!/usr/bin/env python3
"""
Format RTOS review JSON output as GitHub-flavored markdown.

Usage:
    python tools/rtos_review_format.py <review_output.json> >> "$GITHUB_STEP_SUMMARY"

Exit codes:
    0   No critical findings
    1   One or more critical findings
    2   Could not read or parse the JSON file
"""

import io
import json
import sys

sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding="utf-8", errors="replace")

SEVERITY_ICON = {"CRITICAL": "🔴", "WARNING": "⚠️", "INFO": "ℹ️"}


def format_finding_table(findings: list[dict]) -> list[str]:
    """Format a list of findings as a markdown table."""
    if not findings:
        return ["No findings.\n"]

    lines = []
    lines.append("| Severity | Confidence | Line | Rule | Title |")
    lines.append("|---|---|---|---|---|")

    for f in findings:
        sev = f.get("severity", "INFO")
        icon = SEVERITY_ICON.get(sev, "")
        conf = f.get("confidence", "—")
        lines.append(
            f"| {icon}&nbsp;{sev} | {conf} | {f.get('line', '?')} "
            f"| `{f.get('rule', '?')}` | {f.get('title', '')[:80]} |"
        )

    return lines


def format_finding_details(findings: list[dict]) -> list[str]:
    """Format detailed findings with descriptions and suggestions."""
    lines = []
    for f in findings:
        sev = f.get("severity", "INFO")
        icon = SEVERITY_ICON.get(sev, "")
        conf = f.get("confidence", "—")
        lines.append(
            f"#### {icon} {f.get('title', '')} "
            f"<sup>line&nbsp;{f.get('line', '?')} · `{f.get('rule', '?')}` "
            f"· confidence:&nbsp;{conf}</sup>"
        )
        lines.append(f"{f.get('detail', '')}\n")
        if f.get("bug_flow"):
            lines.append("**Bug flow:**")
            for step in f["bug_flow"].split("\\n"):
                step = step.strip()
                if step:
                    lines.append(f"> {step}")
            lines.append("")
        lines.append(f"> **Suggested fix:** {f.get('suggestion', '')}\n")
    return lines


def format_result(result: dict) -> list[str]:
    filename = result.get("file", "unknown")
    findings = result.get("findings", [])

    lines = [f"### `{filename}`"]

    if result.get("parse_error"):
        lines.append(f"> ⚠️ Could not parse agent response: {result['parse_error']}")
        return lines

    if not findings:
        lines.append("No findings.\n")
        return lines

    c = sum(1 for f in findings if f.get("severity") == "CRITICAL")
    w = sum(1 for f in findings if f.get("severity") == "WARNING")
    i = sum(1 for f in findings if f.get("severity") == "INFO")
    lines.append(f"**{c} critical &nbsp;·&nbsp; {w} warning &nbsp;·&nbsp; {i} info**\n")

    lines.extend(format_finding_table(findings))
    lines.append("")
    lines.extend(format_finding_details(findings))

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

    grounded_dropped = []
    for result in results:
        output_lines.extend(format_result(result))
        output_lines.append("")
        for f in result.get("findings", []):
            sev = f.get("severity", "INFO")
            total[sev.lower()] = total.get(sev.lower(), 0) + 1
        # Collect grounded-dropped findings for audit section
        for d in result.get("grounded_dropped", []):
            grounded_dropped.append({**d, "file": result.get("file", "?")})

    c, w, i = total["critical"], total["warning"], total["info"]
    if len(results) > 1:
        output_lines.append(
            f"---\n**Total across {len(results)} files: "
            f"{c} critical &nbsp;·&nbsp; {w} warning &nbsp;·&nbsp; {i} info**"
        )

    # Collapsed section for findings dropped by evidence grounding
    if grounded_dropped:
        output_lines.append("")
        output_lines.append("<details>")
        output_lines.append(f"<summary>🔍 Evidence grounding dropped "
                            f"{len(grounded_dropped)} finding(s)</summary>\n")
        output_lines.append("These agent findings were removed because their claimed "
                            "identifiers could not be found near the reported line number.\n")
        output_lines.append("| File | Line | Rule | Title | Reason |")
        output_lines.append("|---|---|---|---|---|")
        for d in grounded_dropped:
            output_lines.append(
                f"| `{d.get('file', '?')}` | {d.get('line', '?')} "
                f"| `{d.get('rule', '?')}` | {d.get('title', '')[:60]} "
                f"| {d.get('reason', '')} |"
            )
        output_lines.append("\n</details>")

    print("\n".join(output_lines))
    return 1 if c > 0 else 0


if __name__ == "__main__":
    sys.exit(main())
