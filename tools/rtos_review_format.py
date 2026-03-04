#!/usr/bin/env python3
"""
Format RTOS review JSON output as GitHub-flavored markdown.

Supports two finding sources (Option C architecture):
- source="codeql" — structural findings from CodeQL static analysis
- source="agent"  — semantic findings from Claude agent

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
SOURCE_ICON = {"codeql": "🔬", "agent": "🤖"}


def format_finding_table(findings: list[dict], show_source: bool = False) -> list[str]:
    """Format a list of findings as a markdown table."""
    if not findings:
        return ["No findings.\n"]

    lines = []
    if show_source:
        lines.append("| Severity | Source | Confidence | Line | Rule | Title |")
        lines.append("|---|---|---|---|---|---|")
    else:
        lines.append("| Severity | Confidence | Line | Rule | Title |")
        lines.append("|---|---|---|---|---|")

    for f in findings:
        sev = f.get("severity", "INFO")
        icon = SEVERITY_ICON.get(sev, "")
        conf = f.get("confidence", "—")
        source = f.get("source", "agent")
        src_icon = SOURCE_ICON.get(source, "")
        if show_source:
            lines.append(
                f"| {icon}&nbsp;{sev} | {src_icon} | {conf} | {f.get('line', '?')} "
                f"| `{f.get('rule', '?')}` | {f.get('title', '')[:80]} |"
            )
        else:
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
        source = f.get("source", "agent")
        src_icon = SOURCE_ICON.get(source, "")
        lines.append(
            f"#### {icon} {f.get('title', '')} "
            f"<sup>line&nbsp;{f.get('line', '?')} · `{f.get('rule', '?')}` "
            f"· confidence:&nbsp;{conf} · {src_icon}&nbsp;{source}</sup>"
        )
        lines.append(f"{f.get('detail', '')}\n")
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

    # Split by source
    codeql_findings = [f for f in findings if f.get("source") == "codeql"]
    agent_findings = [f for f in findings if f.get("source") != "codeql"]

    c = sum(1 for f in findings if f.get("severity") == "CRITICAL")
    w = sum(1 for f in findings if f.get("severity") == "WARNING")
    i = sum(1 for f in findings if f.get("severity") == "INFO")
    lines.append(f"**{c} critical &nbsp;·&nbsp; {w} warning &nbsp;·&nbsp; {i} info**\n")

    # If both sources present, show combined table with source column
    has_both = bool(codeql_findings) and bool(agent_findings)

    if has_both:
        lines.extend(format_finding_table(findings, show_source=True))
        lines.append("")

        # Agent details first (semantic findings are more interesting)
        if agent_findings:
            lines.append("#### 🤖 Agent Findings (semantic analysis)\n")
            lines.extend(format_finding_details(agent_findings))

        if codeql_findings:
            lines.append("#### 🔬 CodeQL Findings (static analysis)\n")
            lines.extend(format_finding_details(codeql_findings))
    else:
        # Single source — simpler format
        lines.extend(format_finding_table(findings, show_source=False))
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
    output_lines.append("🤖 Agent: semantic concurrency analysis &nbsp;|&nbsp; "
                        "🔬 CodeQL: structural static analysis\n")

    for result in results:
        output_lines.extend(format_result(result))
        output_lines.append("")
        for f in result.get("findings", []):
            sev = f.get("severity", "INFO")
            total[sev.lower()] = total.get(sev.lower(), 0) + 1

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
