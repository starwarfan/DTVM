#!/usr/bin/env python3
"""
Performance regression checker for evmone benchmarks.

Usage:
  # Save baseline results
  python check_performance_regression.py --save-baseline baseline.json

  # Check for regressions against baseline
  python check_performance_regression.py --baseline baseline.json

  # Check with custom threshold (default 10%)
  python check_performance_regression.py --baseline baseline.json --threshold 0.15

Exit codes:
  0 - No significant regression detected
  1 - Performance regression detected (> threshold)
  2 - Script error (execution failed, file not found, etc.)
"""

import argparse
import json
import subprocess
import sys
from dataclasses import dataclass
from typing import Dict, List, Optional, Tuple


@dataclass
class BenchmarkResult:
    name: str
    time_ns: float  # Time in nanoseconds
    cpu_time_ns: float
    iterations: int


def run_benchmark(
    lib_path: str,
    mode: str,
    benchmark_dir: str,
    extra_args: Optional[List[str]] = None,
) -> List[BenchmarkResult]:
    """Run benchmark and parse JSON output."""
    env = {"EVMONE_EXTERNAL_OPTIONS": f"{lib_path},mode={mode}"}

    cmd = [
        "./build/bin/evmone-bench",
        benchmark_dir,
        "--benchmark_filter=external/*",
        "--benchmark_format=json",
    ]

    if extra_args:
        cmd.extend(extra_args)

    print(f"Running: {' '.join(cmd)}")
    print(f"Environment: EVMONE_EXTERNAL_OPTIONS={env['EVMONE_EXTERNAL_OPTIONS']}")

    result = subprocess.run(
        cmd,
        capture_output=True,
        text=True,
        env={**subprocess.os.environ, **env},
    )

    if result.returncode != 0:
        print(f"Benchmark execution failed with code {result.returncode}")
        print(f"stderr: {result.stderr}")
        sys.exit(2)

    return parse_benchmark_json(result.stdout)


def parse_benchmark_json(json_output: str) -> List[BenchmarkResult]:
    """Parse Google Benchmark JSON output."""
    try:
        data = json.loads(json_output)
    except json.JSONDecodeError as e:
        print(f"Failed to parse JSON: {e}")
        sys.exit(2)

    results = []
    for benchmark in data.get("benchmarks", []):
        # Skip aggregates like mean, median, stddev
        if benchmark.get("run_type") != "iteration":
            continue

        results.append(
            BenchmarkResult(
                name=benchmark["name"],
                time_ns=benchmark.get("real_time", 0),
                cpu_time_ns=benchmark.get("cpu_time", 0),
                iterations=benchmark.get("iterations", 1),
            )
        )

    return results


def load_baseline(path: str) -> List[BenchmarkResult]:
    """Load baseline results from JSON file."""
    try:
        with open(path, "r") as f:
            data = json.load(f)
    except FileNotFoundError:
        print(f"::error::Baseline file not found: {path}")
        sys.exit(2)
    except json.JSONDecodeError as e:
        print(f"::error::Failed to parse baseline JSON: {e}")
        sys.exit(2)

    results = []
    for item in data:
        results.append(
            BenchmarkResult(
                name=item["name"],
                time_ns=item["time_ns"],
                cpu_time_ns=item["cpu_time_ns"],
                iterations=item["iterations"],
            )
        )

    return results


def save_baseline(results: List[BenchmarkResult], path: str) -> None:
    """Save baseline results to JSON file."""
    data = []
    for r in results:
        data.append({
            "name": r.name,
            "time_ns": r.time_ns,
            "cpu_time_ns": r.cpu_time_ns,
            "iterations": r.iterations,
        })

    with open(path, "w") as f:
        json.dump(data, f, indent=2)

    print(f"Saved {len(results)} benchmark results to {path}")


def compare_benchmarks(
    current: List[BenchmarkResult],
    baseline: List[BenchmarkResult],
    threshold: float,
) -> Tuple[bool, List[dict]]:
    """
    Compare current results against baseline.

    Returns:
        (has_regression, comparison_details)
    """
    baseline_map = {b.name: b for b in baseline}
    current_map = {c.name: c for c in current}

    # Find missing and new benchmarks
    baseline_names = set(baseline_map.keys())
    current_names = set(current_map.keys())

    missing = baseline_names - current_names
    new = current_names - baseline_names

    if missing:
        print(f"::warning::Missing benchmarks (in baseline but not in current): {missing}")
    if new:
        print(f"::notice::New benchmarks (in current but not in baseline): {new}")

    # Compare common benchmarks
    comparisons = []
    has_regression = False

    for name in sorted(baseline_names & current_names):
        b = baseline_map[name]
        c = current_map[name]

        # Calculate percentage change (positive = slower/regression)
        time_change = (c.time_ns - b.time_ns) / b.time_ns
        cpu_change = (c.cpu_time_ns - b.cpu_time_ns) / b.cpu_time_ns

        # Use the worse of real_time or cpu_time change
        max_change = max(time_change, cpu_change)

        is_regression = max_change > threshold
        if is_regression:
            has_regression = True

        comparisons.append({
            "name": name,
            "baseline_time_ns": b.time_ns,
            "current_time_ns": c.time_ns,
            "time_change": time_change,
            "cpu_change": cpu_change,
            "max_change": max_change,
            "is_regression": is_regression,
        })

    return has_regression, comparisons


def print_comparison_table(comparisons: List[dict], threshold: float) -> None:
    """Print a formatted comparison table."""
    if not comparisons:
        print("No benchmarks to compare.")
        return

    # GitHub Actions annotation messages
    print("\n" + "=" * 100)
    print(f"{'Benchmark':<60} {'Baseline(μs)':<15} {'Current(μs)':<15} {'Change':<12} {'Status'}")
    print("=" * 100)

    regression_count = 0
    for comp in comparisons:
        name = comp["name"]
        baseline_us = comp["baseline_time_ns"] / 1000
        current_us = comp["current_time_ns"] / 1000
        change_pct = comp["max_change"] * 100
        status = "✓ PASS" if not comp["is_regression"] else "✗ FAIL"

        # Truncate long names
        display_name = name if len(name) < 60 else name[:57] + "..."

        print(f"{display_name:<60} {baseline_us:<15.2f} {current_us:<15.2f} {change_pct:>+10.1f}%  {status}")

        if comp["is_regression"]:
            regression_count += 1
            # GitHub Actions warning annotation
            print(f"::warning title=Performance Regression::{name} regressed by {change_pct:.1f}% (threshold: {threshold*100:.0f}%)")

    print("=" * 100)
    print(f"\nTotal benchmarks: {len(comparisons)}")
    print(f"Regressions (> {threshold*100:.0f}%): {regression_count}")


def main():
    parser = argparse.ArgumentParser(
        description="Check for performance regressions in evmone benchmarks",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  # Save baseline after a known-good commit
  python check_performance_regression.py --save-baseline baseline.json

  # Check current commit against baseline in CI
  python check_performance_regression.py --baseline baseline.json

  # Check with custom threshold (15% instead of default 10%)
  python check_performance_regression.py --baseline baseline.json --threshold 0.15

  # Specify different library or benchmark directory
  python check_performance_regression.py --baseline baseline.json --lib ./other.so --mode jit
""",
    )

    parser.add_argument(
        "--baseline",
        metavar="PATH",
        help="Path to baseline JSON file for comparison",
    )
    parser.add_argument(
        "--save-baseline",
        metavar="PATH",
        help="Run benchmarks and save results to file (use this to create baseline)",
    )
    parser.add_argument(
        "--threshold",
        type=float,
        default=0.10,
        help="Regression threshold as ratio (default: 0.10 = 10%%)",
    )
    parser.add_argument(
        "--lib",
        default="./libdtvmapi.so",
        help="Path to the library to benchmark (default: ./libdtvmapi.so)",
    )
    parser.add_argument(
        "--mode",
        default="interpreter",
        help="Mode for the library (default: interpreter)",
    )
    parser.add_argument(
        "--benchmark-dir",
        default="test/evm-benchmarks/benchmarks",
        help="Path to benchmark directory (default: test/evm-benchmarks/benchmarks)",
    )
    parser.add_argument(
        "--verbose",
        "-v",
        action="store_true",
        help="Verbose output",
    )

    args = parser.parse_args()

    if not args.baseline and not args.save_baseline:
        parser.error("Either --baseline or --save-baseline must be specified")

    # Run benchmarks
    try:
        current_results = run_benchmark(
            lib_path=args.lib,
            mode=args.mode,
            benchmark_dir=args.benchmark_dir,
        )
    except Exception as e:
        print(f"::error::Failed to run benchmarks: {e}")
        sys.exit(2)

    if not current_results:
        print("::error::No benchmark results found")
        sys.exit(2)

    print(f"Collected {len(current_results)} benchmark results")

    # Save baseline mode
    if args.save_baseline:
        save_baseline(current_results, args.save_baseline)
        return 0

    # Compare mode
    baseline_results = load_baseline(args.baseline)
    print(f"Loaded {len(baseline_results)} baseline results from {args.baseline}")

    has_regression, comparisons = compare_benchmarks(
        current_results,
        baseline_results,
        args.threshold,
    )

    print_comparison_table(comparisons, args.threshold)

    # Summary for GitHub Actions
    print("\n" + "=" * 100)
    if has_regression:
        print(f"::error::Performance regression detected! Some benchmarks exceeded {args.threshold*100:.0f}% threshold.")
        print("RESULT: FAIL")
        return 1
    else:
        print("::notice::No significant performance regression detected.")
        print("RESULT: PASS")
        return 0


if __name__ == "__main__":
    sys.exit(main())
