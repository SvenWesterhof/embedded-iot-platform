#!/usr/bin/env python3
"""
RTOS Review Merger — combines CodeQL static analysis with Claude agent findings.

Architecture: Option C (clear rule ownership)
- CodeQL owns structural/syntactic rules (always run against full DB)
- Agent owns semantic/reasoning rules (runs on changed files)
- No correlation between them — each tool owns its rules completely

Usage:
    python tools/rtos_review_correlator.py \
        --agent-results /tmp/rtos_review.json \
        --codeql-db ./codeql-db \
        --query-dir tools/codeql/queries/rtos/ \
        --output /tmp/merged_review.json

Exit codes:
    0   No critical findings
    1   One or more CRITICAL findings
    2   Merger error (CodeQL failure, file not found, etc.)
"""

import argparse
import json
import os
import subprocess
import sys
import tempfile
from pathlib import Path

# CodeQL-owned rules: structural/syntactic checks that CodeQL handles precisely.
# These are always run as standalone queries against the full database.
CODEQL_RULES = {
    "WRAPPER_BYPASS": "WrapperBypass.ql",
    "ISR_UNSAFE_API": "IsrUnsafeApi.ql",
    "BLOCKING_IN_CRITICAL": "BlockingInCritical.ql",
    "WATCHDOG_STARVATION": "WatchdogStarvation.ql",
    "CORE_AFFINITY": "CoreAffinity.ql",
}

# Agent-owned rules: semantic/reasoning checks that need LLM understanding.
# CodeQL queries for these exist but are not used for verification.
AGENT_RULES = {
    "SHARED_STATE", "RACE_CONDITION", "LOCK_ORDER", "PRIORITY_INVERSION",
    "CALLBACK_UNDER_LOCK", "UNBOUNDED_WAIT", "MEMORY_LEAK", "STACK_OVERFLOW",
    "EVENT_BUS_MISUSE",
}

# Map CodeQL rule IDs (from SARIF) to our rule IDs.
CODEQL_RULE_MAP = {
    "rtos/wrapper-bypass": "WRAPPER_BYPASS",
    "rtos/isr-unsafe-api": "ISR_UNSAFE_API",
    "rtos/blocking-in-critical": "BLOCKING_IN_CRITICAL",
    "rtos/watchdog-starvation": "WATCHDOG_STARVATION",
    "rtos/core-affinity": "CORE_AFFINITY",
}

# Severity for CodeQL-owned rules (matches agent rule table).
CODEQL_SEVERITY = {
    "WRAPPER_BYPASS": "WARNING",
    "ISR_UNSAFE_API": "CRITICAL",
    "BLOCKING_IN_CRITICAL": "CRITICAL",
    "WATCHDOG_STARVATION": "WARNING",
    "CORE_AFFINITY": "WARNING",
}


def run_codeql_queries(
    codeql_db: str,
    query_files: list[str],
    sarif_output: str,
) -> bool:
    """Run CodeQL analysis and output SARIF. Returns True on success."""
    cmd = [
        "codeql", "database", "analyze",
        codeql_db,
        *query_files,
        "--format=sarifv2.1.0",
        f"--output={sarif_output}",
        "--threads=0",
    ]
    print(f"  Running CodeQL: {len(query_files)} queries...", file=sys.stderr)
    try:
        result = subprocess.run(
            cmd, capture_output=True, text=True, timeout=300
        )
        if result.returncode != 0:
            print(f"  CodeQL stderr: {result.stderr[:500]}", file=sys.stderr)
            return False
        return True
    except FileNotFoundError:
        print("  Error: 'codeql' CLI not found in PATH", file=sys.stderr)
        return False
    except subprocess.TimeoutExpired:
        print("  Error: CodeQL analysis timed out (300s)", file=sys.stderr)
        return False


def parse_sarif(sarif_path: str) -> list[dict]:
    """Parse SARIF file and extract results as simplified dicts."""
    try:
        with open(sarif_path) as f:
            sarif = json.load(f)
    except (OSError, json.JSONDecodeError) as e:
        print(f"  Warning: could not parse SARIF: {e}", file=sys.stderr)
        return []

    results = []
    for run in sarif.get("runs", []):
        for result in run.get("results", []):
            rule_id = result.get("ruleId", "")
            message = result.get("message", {}).get("text", "")

            locations = result.get("locations", [])
            if not locations:
                continue

            physical = locations[0].get("physicalLocation", {})
            artifact = physical.get("artifactLocation", {}).get("uri", "")
            region = physical.get("region", {})
            line = region.get("startLine", 0)

            results.append({
                "rule_id": rule_id,
                "message": message,
                "file": artifact,
                "line": line,
            })

    return results


def normalize_path(path: str) -> str:
    """Normalize a file path for comparison."""
    return path.replace("\\", "/").lower().strip("/")


def codeql_results_to_findings(
    codeql_results: list[dict],
    reviewed_files: set[str],
) -> list[dict]:
    """Convert CodeQL SARIF results into finding dicts, scoped to reviewed files.

    Only includes results for files the agent reviewed, so we don't flood
    the report with findings from unrelated files.
    """
    findings_by_file: dict[str, list[dict]] = {}

    for cq in codeql_results:
        rule = CODEQL_RULE_MAP.get(cq["rule_id"])
        if not rule:
            continue

        cq_file = normalize_path(cq["file"])

        # Only include if in a reviewed file
        matched_file = None
        for rf in reviewed_files:
            if cq_file.endswith(rf) or rf.endswith(cq_file) or \
               Path(cq_file).name == Path(rf).name:
                matched_file = rf
                break
        if not matched_file:
            continue

        finding = {
            "severity": CODEQL_SEVERITY.get(rule, "WARNING"),
            "confidence": "HIGH",
            "line": cq["line"],
            "rule": rule,
            "title": cq["message"][:100],
            "detail": cq["message"],
            "suggestion": "See CodeQL static analysis for details.",
            "source": "codeql",
        }

        # Group by the original file path from the reviewed set
        findings_by_file.setdefault(matched_file, []).append(finding)

    return findings_by_file


def merge_results(
    agent_results: list[dict],
    codeql_findings_by_file: dict[str, list[dict]],
) -> list[dict]:
    """Merge agent and CodeQL findings into a single result set.

    Each result dict gets:
    - Agent findings tagged with source="agent"
    - CodeQL findings tagged with source="codeql"
    - Summary counts updated
    """
    # Tag all agent findings
    for result in agent_results:
        for finding in result.get("findings", []):
            finding["source"] = "agent"

    # Add CodeQL findings to existing results (matching by filename)
    reviewed_files_matched = set()
    for result in agent_results:
        agent_file = normalize_path(result.get("file", ""))
        for rf, cq_findings in codeql_findings_by_file.items():
            if agent_file.endswith(rf) or rf.endswith(agent_file) or \
               Path(agent_file).name == Path(rf).name:
                result.get("findings", []).extend(cq_findings)
                reviewed_files_matched.add(rf)

    # Recount summaries
    for result in agent_results:
        findings = result.get("findings", [])
        result["summary"] = {
            "critical": sum(1 for f in findings if f.get("severity") == "CRITICAL"),
            "warning": sum(1 for f in findings if f.get("severity") == "WARNING"),
            "info": sum(1 for f in findings if f.get("severity") == "INFO"),
        }

    return agent_results


def main():
    parser = argparse.ArgumentParser(
        description="Merge RTOS agent findings with CodeQL static analysis"
    )
    parser.add_argument(
        "--agent-results", required=True,
        help="Path to agent JSON output"
    )
    parser.add_argument(
        "--codeql-db", required=True,
        help="Path to CodeQL database directory"
    )
    parser.add_argument(
        "--query-dir", required=True,
        help="Path to directory containing .ql query files"
    )
    parser.add_argument(
        "--output", required=True,
        help="Path to write merged JSON output"
    )
    args = parser.parse_args()

    # Load agent results
    try:
        with open(args.agent_results) as f:
            agent_data = json.load(f)
    except (OSError, json.JSONDecodeError) as e:
        print(f"Error reading agent results: {e}", file=sys.stderr)
        sys.exit(2)

    if isinstance(agent_data, dict):
        agent_results = [agent_data]
    else:
        agent_results = agent_data

    # Collect reviewed files from agent results
    reviewed_files = set()
    for result in agent_results:
        reviewed_files.add(normalize_path(result.get("file", "")))

    # Build list of CodeQL queries to run
    query_dir = Path(args.query_dir)
    queries = []
    for rule, ql_file in CODEQL_RULES.items():
        ql_path = query_dir / ql_file
        if ql_path.exists():
            queries.append(str(ql_path))
        else:
            print(f"  Warning: query not found: {ql_path}", file=sys.stderr)

    # Run CodeQL
    codeql_findings_by_file = {}
    if queries:
        with tempfile.NamedTemporaryFile(suffix=".sarif", delete=False, mode="w") as tmp:
            sarif_path = tmp.name

        try:
            success = run_codeql_queries(args.codeql_db, queries, sarif_path)
            if success:
                codeql_results = parse_sarif(sarif_path)
                print(f"  CodeQL found {len(codeql_results)} result(s)", file=sys.stderr)
                codeql_findings_by_file = codeql_results_to_findings(
                    codeql_results, reviewed_files
                )
            else:
                print("  Warning: CodeQL failed — report contains agent-only results",
                      file=sys.stderr)
        finally:
            try:
                os.unlink(sarif_path)
            except OSError:
                pass
    else:
        print("  No CodeQL queries found — report contains agent-only results",
              file=sys.stderr)

    # Merge
    merged = merge_results(agent_results, codeql_findings_by_file)

    # Write output
    output = merged if len(merged) > 1 else merged[0]
    with open(args.output, "w") as f:
        json.dump(output, f, indent=2)

    # Print summary
    total_codeql = sum(len(fs) for fs in codeql_findings_by_file.values())
    total_agent = sum(
        sum(1 for f in r.get("findings", []) if f.get("source") == "agent")
        for r in merged
    )
    print(f"\n  Merge summary:", file=sys.stderr)
    print(f"    CodeQL findings (in reviewed files): {total_codeql}", file=sys.stderr)
    print(f"    Agent findings:                      {total_agent}", file=sys.stderr)

    # Exit code: fail on any CRITICAL finding
    has_critical = any(
        f.get("severity") == "CRITICAL"
        for r in merged
        for f in r.get("findings", [])
    )
    sys.exit(1 if has_critical else 0)


if __name__ == "__main__":
    main()
