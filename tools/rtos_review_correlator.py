#!/usr/bin/env python3
"""
RTOS Review Correlator — CodeQL verification of Claude agent findings.

Runs CodeQL queries against a pre-built database and correlates results
with the agent's findings to confirm or disprove code smells.

Usage:
    python tools/rtos_review_correlator.py \
        --agent-results /tmp/rtos_review.json \
        --codeql-db ./codeql-db \
        --query-dir tools/codeql/queries/rtos/ \
        --output /tmp/hybrid_review.json

Verification statuses added to each finding:
    CONFIRMED   - Both agent and CodeQL agree (high confidence)
    AGENT_ONLY  - No CodeQL query applicable, or bad codeql_params
    DROPPED     - Agent flagged, but CodeQL found no evidence

Exit codes:
    0   No critical findings
    1   One or more CRITICAL findings
    2   Correlator error (CodeQL failure, file not found, etc.)
"""

import argparse
import json
import os
import subprocess
import sys
import tempfile
from pathlib import Path

# Rules that have standalone CodeQL queries (always run).
STANDALONE_RULES = {
    "WRAPPER_BYPASS": "WrapperBypass.ql",
    "ISR_UNSAFE_API": "IsrUnsafeApi.ql",
    "BLOCKING_IN_CRITICAL": "BlockingInCritical.ql",
    "WATCHDOG_STARVATION": "WatchdogStarvation.ql",
    "CORE_AFFINITY": "CoreAffinity.ql",
}

# Rules that have parameterized queries (need codeql_params from agent).
PARAMETERIZED_RULES = {
    "MEMORY_LEAK": "MemoryLeakPath.ql",
    "SHARED_STATE": "SharedStateUnprotected.ql",
    "RACE_CONDITION": "RaceConditionToctou.ql",
    "UNBOUNDED_WAIT": "UnboundedWait.ql",
    "EVENT_BUS_MISUSE": "EventBusMisuse.ql",
    "LOCK_ORDER": "LockOrder.ql",
    "PRIORITY_INVERSION": "PriorityInversion.ql",
    "CALLBACK_UNDER_LOCK": "CallbackUnderLock.ql",
    "STACK_OVERFLOW": "StackOverflowEstimate.ql",
}

# All rules with CodeQL coverage (used for matching CodeQL-only findings).
ALL_CODEQL_RULES = {**STANDALONE_RULES, **PARAMETERIZED_RULES}

# Line proximity threshold for matching agent findings to CodeQL results.
LINE_PROXIMITY = 5


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
        "--threads=0",  # Use all available cores
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


def map_codeql_rule_to_agent_rule(codeql_rule_id: str) -> str | None:
    """Map a CodeQL rule ID (e.g. 'rtos/wrapper-bypass') to agent rule ID."""
    mapping = {
        "rtos/wrapper-bypass": "WRAPPER_BYPASS",
        "rtos/isr-unsafe-api": "ISR_UNSAFE_API",
        "rtos/blocking-in-critical": "BLOCKING_IN_CRITICAL",
        "rtos/watchdog-starvation": "WATCHDOG_STARVATION",
        "rtos/memory-leak": "MEMORY_LEAK",
        "rtos/shared-state-unprotected": "SHARED_STATE",
        "rtos/race-condition-toctou": "RACE_CONDITION",
        "rtos/unbounded-wait": "UNBOUNDED_WAIT",
        "rtos/event-bus-misuse": "EVENT_BUS_MISUSE",
        "rtos/lock-order": "LOCK_ORDER",
        "rtos/priority-inversion": "PRIORITY_INVERSION",
        "rtos/callback-under-lock": "CALLBACK_UNDER_LOCK",
        "rtos/stack-overflow-estimate": "STACK_OVERFLOW",
        "rtos/core-affinity": "CORE_AFFINITY",
    }
    return mapping.get(codeql_rule_id)


def normalize_path(path: str) -> str:
    """Normalize a file path for comparison (lowercase, forward slashes)."""
    return path.replace("\\", "/").lower().strip("/")


def match_finding_to_codeql(
    finding: dict,
    codeql_results: list[dict],
    reviewed_file: str,
) -> dict | None:
    """Try to match an agent finding to a CodeQL result.

    Returns the matching CodeQL result, or None if no match.
    """
    rule = finding.get("rule", "")
    line = finding.get("line", 0)

    norm_file = normalize_path(reviewed_file)

    for cq in codeql_results:
        cq_rule = map_codeql_rule_to_agent_rule(cq["rule_id"])
        if cq_rule != rule:
            continue

        cq_file = normalize_path(cq["file"])
        # Check if the file paths match (CodeQL may use relative paths)
        if not (cq_file.endswith(norm_file) or norm_file.endswith(cq_file)):
            # Try just the filename
            if Path(cq_file).name != Path(norm_file).name:
                continue

        # Check line proximity
        if abs(cq["line"] - line) <= LINE_PROXIMITY:
            return cq

    return None


def correlate(
    agent_results: list[dict],
    codeql_results: list[dict],
) -> list[dict]:
    """Correlate agent findings with CodeQL results.

    Tags each finding with verification_status and verification_source.
    """
    for result in agent_results:
        filename = result.get("file", "")
        for finding in result.get("findings", []):
            rule = finding.get("rule", "")

            if rule not in ALL_CODEQL_RULES:
                # No CodeQL query for this rule
                finding["verification_status"] = "AGENT_ONLY"
                finding["verification_source"] = f"No CodeQL query for {rule}"
                continue

            match = match_finding_to_codeql(finding, codeql_results, filename)
            if match:
                finding["verification_status"] = "CONFIRMED"
                finding["verification_source"] = f"CodeQL:{match['rule_id']}"
            else:
                # Agent found it but CodeQL didn't — mark as DROPPED
                finding["verification_status"] = "DROPPED"
                finding["verification_source"] = (
                    f"CodeQL:{ALL_CODEQL_RULES[rule]} found no match"
                )

    return agent_results


def add_codeql_only_findings(
    agent_results: list[dict],
    codeql_results: list[dict],
) -> list[dict]:
    """Add CodeQL findings that the agent missed, but ONLY for files
    that were already reviewed by the agent.

    This prevents standalone queries (which scan the entire codebase)
    from flooding the output with hundreds of findings in unrelated files.
    """
    # Collect the set of files the agent reviewed (normalized)
    reviewed_files = set()
    for result in agent_results:
        reviewed_files.add(normalize_path(result.get("file", "")))

    # Build a set of (file, line, rule) already covered by agent
    agent_covered = set()
    for result in agent_results:
        filename = result.get("file", "")
        for finding in result.get("findings", []):
            agent_covered.add((
                normalize_path(filename),
                finding.get("line", 0),
                finding.get("rule", ""),
            ))

    # Check each CodeQL result — only add if in a reviewed file
    for cq in codeql_results:
        cq_rule = map_codeql_rule_to_agent_rule(cq["rule_id"])
        if not cq_rule:
            continue

        cq_file = normalize_path(cq["file"])
        cq_line = cq["line"]

        # Only consider findings in files the agent already reviewed
        in_reviewed_file = False
        for rf in reviewed_files:
            if cq_file.endswith(rf) or rf.endswith(cq_file) or \
               Path(cq_file).name == Path(rf).name:
                in_reviewed_file = True
                break
        if not in_reviewed_file:
            continue

        # Check if any agent finding is close
        already_matched = False
        for (af, al, ar) in agent_covered:
            if ar == cq_rule and abs(al - cq_line) <= LINE_PROXIMITY:
                if cq_file.endswith(af) or af.endswith(cq_file):
                    already_matched = True
                    break

        if not already_matched:
            # This is a CodeQL-only finding in a reviewed file — add it
            target_file = Path(cq_file).name
            # Find the result entry for this file
            target_result = None
            for result in agent_results:
                if normalize_path(result.get("file", "")) == cq_file or \
                   Path(normalize_path(result.get("file", ""))).name == target_file:
                    target_result = result
                    break

            if target_result is None:
                continue  # Don't create new file entries

            from rtos_review_agent import RULE_SEVERITY
            severity = RULE_SEVERITY.get(cq_rule, "WARNING")
            target_result["findings"].append({
                "severity": severity,
                "confidence": "HIGH",
                "line": cq_line,
                "rule": cq_rule,
                "title": f"[CodeQL] {cq['message'][:80]}",
                "detail": cq["message"],
                "suggestion": "See CodeQL analysis for details.",
                "verification_status": "CONFIRMED",
                "verification_source": f"CodeQL-only:{cq['rule_id']}",
            })

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
        description="Correlate RTOS agent findings with CodeQL verification"
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
    parser.add_argument(
        "--baseline-sarif",
        help="Path to pre-computed SARIF from baseline queries (optional, skips re-running)"
    )
    args = parser.parse_args()

    # Load agent results
    try:
        with open(args.agent_results) as f:
            agent_data = json.load(f)
    except (OSError, json.JSONDecodeError) as e:
        print(f"Error reading agent results: {e}", file=sys.stderr)
        sys.exit(2)

    # Normalize to list
    if isinstance(agent_data, dict):
        agent_results = [agent_data]
    else:
        agent_results = agent_data

    # Determine which queries to run
    query_dir = Path(args.query_dir)
    all_queries = []

    # Always run standalone queries
    for rule, ql_file in STANDALONE_RULES.items():
        ql_path = query_dir / ql_file
        if ql_path.exists():
            all_queries.append(str(ql_path))
        else:
            print(f"  Warning: standalone query not found: {ql_path}", file=sys.stderr)

    # Run parameterized queries for rules that the agent flagged
    agent_rules_found = set()
    for result in agent_results:
        for finding in result.get("findings", []):
            agent_rules_found.add(finding.get("rule", ""))

    for rule in agent_rules_found:
        if rule in PARAMETERIZED_RULES:
            ql_file = PARAMETERIZED_RULES[rule]
            ql_path = query_dir / ql_file
            if ql_path.exists():
                all_queries.append(str(ql_path))

    if not all_queries:
        print("  No CodeQL queries to run — all findings marked AGENT_ONLY",
              file=sys.stderr)
        for result in agent_results:
            for finding in result.get("findings", []):
                finding["verification_status"] = "AGENT_ONLY"
                finding["verification_source"] = "No CodeQL queries available"
        with open(args.output, "w") as f:
            json.dump(agent_results if len(agent_results) > 1 else agent_results[0], f, indent=2)
        sys.exit(0)

    # Run CodeQL
    codeql_results = []

    if args.baseline_sarif and os.path.exists(args.baseline_sarif):
        print(f"  Using pre-computed baseline SARIF: {args.baseline_sarif}",
              file=sys.stderr)
        codeql_results.extend(parse_sarif(args.baseline_sarif))

    # Run any queries not covered by the baseline
    with tempfile.NamedTemporaryFile(suffix=".sarif", delete=False, mode="w") as tmp:
        sarif_path = tmp.name

    try:
        # Deduplicate queries
        unique_queries = list(dict.fromkeys(all_queries))
        success = run_codeql_queries(args.codeql_db, unique_queries, sarif_path)
        if success:
            codeql_results.extend(parse_sarif(sarif_path))
            print(f"  CodeQL found {len(codeql_results)} result(s)", file=sys.stderr)
        else:
            print("  Warning: CodeQL analysis failed — all findings marked AGENT_ONLY",
                  file=sys.stderr)
            for result in agent_results:
                for finding in result.get("findings", []):
                    finding["verification_status"] = "AGENT_ONLY"
                    finding["verification_source"] = "CodeQL analysis failed"
            with open(args.output, "w") as f:
                json.dump(
                    agent_results if len(agent_results) > 1 else agent_results[0],
                    f, indent=2,
                )
            sys.exit(0)
    finally:
        try:
            os.unlink(sarif_path)
        except OSError:
            pass

    # Correlate
    agent_results = correlate(agent_results, codeql_results)

    # Add CodeQL-only findings (things CodeQL caught that agent missed)
    agent_results = add_codeql_only_findings(agent_results, codeql_results)

    # Write output
    output = agent_results if len(agent_results) > 1 else agent_results[0]
    with open(args.output, "w") as f:
        json.dump(output, f, indent=2)

    # Print summary
    total_confirmed = 0
    total_dropped = 0
    total_agent_only = 0
    for result in agent_results:
        for finding in result.get("findings", []):
            status = finding.get("verification_status", "AGENT_ONLY")
            if status == "CONFIRMED":
                total_confirmed += 1
            elif status == "DROPPED":
                total_dropped += 1
            else:
                total_agent_only += 1

    print(f"\n  Correlation summary:", file=sys.stderr)
    print(f"    CONFIRMED:  {total_confirmed}", file=sys.stderr)
    print(f"    AGENT_ONLY: {total_agent_only}", file=sys.stderr)
    print(f"    DROPPED:    {total_dropped}", file=sys.stderr)

    # Exit code: check for critical findings (CONFIRMED or AGENT_ONLY)
    has_critical = False
    for result in agent_results:
        for finding in result.get("findings", []):
            status = finding.get("verification_status", "AGENT_ONLY")
            if status == "DROPPED":
                continue
            if finding.get("severity") == "CRITICAL":
                if status == "CONFIRMED" or (
                    status == "AGENT_ONLY" and finding.get("confidence") == "HIGH"
                ):
                    has_critical = True

    sys.exit(1 if has_critical else 0)


if __name__ == "__main__":
    main()
