#!/usr/bin/env python3
# verify_benchmark_logs.py
# Verifies that benchmark log files contain well-formed records.
#
# Checks the first N lines, last N lines, and a random sample from each file.
# Reports malformed lines and overall line count.
#
# Usage:
#   python verify_benchmark_logs.py [--dir DIR] [--sample N] [--full]
#
# Examples:
#   python verify_benchmark_logs.py
#   python verify_benchmark_logs.py --dir C:\SWDev\github\kmac-13\nova\benchmarks
#   python verify_benchmark_logs.py --sample 1000 --full

import argparse
import glob
import os
import random
import re
import sys

# ============================================================================
# Expected formats
# ============================================================================
#
# All libraries use ISO 8601 full timestamp after format fixes:
#
# Nova ISO8601Formatter (systemNanosecs = wall clock):
#   2026-05-05T21:52:23.123456789Z [ASYNC_MT] benchmark_multithreaded.cpp:182 operator() - message
#
# spdlog (source location unavailable via logger->info() on MinGW, pattern omits file/func):
#   2026-05-05T21:52:23.123456 [info] - message
#
# Quill (timestamp_pattern = "%Y-%m-%dT%H:%M:%S.%Qns"):
#   2026-05-05T21:52:23.123456789 [I] benchmark_multithreaded.cpp:388 operator() - message

NOVA_PATTERN = re.compile(
    r"^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}\.\d+Z? \[[A-Z_]+\] .+:\d+ \S+ - .*$"
)

SPDLOG_PATTERN = re.compile(
    r"^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}\.\d+ \[\w+\] - .*$"
)

QUILL_PATTERN = re.compile(
    r"^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}\.\d+ \[[A-Z]\] .+:\d+ \S+ - .*$"
)

# file glob patterns to check — thread count is embedded in filename
LOG_FILE_PATTERNS = [
    ("nova_mt_bench_*.log",         "Nova",   NOVA_PATTERN),
    ("nova_batch_mt_bench_*.log",   "Nova",   NOVA_PATTERN),
    ("nova_delivery_*.log",         "Nova",   NOVA_PATTERN),
    ("spdlog_mt_bench_*.log",       "spdlog", SPDLOG_PATTERN),
    ("spdlog_sync_delivery_*.log",  "spdlog", SPDLOG_PATTERN),
    ("spdlog_async_delivery_*.log", "spdlog", SPDLOG_PATTERN),
    ("quill_mt_bench_*.log",        "Quill",  QUILL_PATTERN),
    ("quill_delivery_*.log",        "Quill",  QUILL_PATTERN),
]


def count_lines_fast(path):
    """Count newlines in file using buffered reads."""
    count = 0
    with open(path, "rb") as f:
        while True:
            buf = f.read(1 << 20)  # 1 MB chunks
            if not buf:
                break
            count += buf.count(b"\n")
    return count


def read_lines_at_offsets(path, offsets):
    """Read specific byte offsets and return the line starting at or after each."""
    lines = []
    size = os.path.getsize(path)
    with open(path, "rb") as f:
        for offset in offsets:
            seek_pos = min(offset, size - 1)
            f.seek(seek_pos)
            if seek_pos > 0:
                f.readline()  # skip partial line
            line = f.readline()
            if line:
                lines.append(line.decode("utf-8", errors="replace").rstrip("\n\r"))
    return lines


def sample_lines(path, n_head, n_tail, n_random, full=False):
    """Return a representative sample of lines from the file."""
    size = os.path.getsize(path)
    if size == 0:
        return []

    if full:
        with open(path, "r", encoding="utf-8", errors="replace") as f:
            return [l.rstrip("\n\r") for l in f]

    lines = []

    # head: read first n_head lines directly
    with open(path, "r", encoding="utf-8", errors="replace") as f:
        for _ in range(n_head):
            line = f.readline()
            if not line:
                break
            lines.append(line.rstrip("\n\r"))

    # tail: seek near end
    tail_seek = max(0, size - n_tail * 200)  # estimate ~200 bytes per line
    with open(path, "rb") as f:
        f.seek(tail_seek)
        if tail_seek > 0:
            f.readline()  # skip partial
        tail_lines = []
        for raw in f:
            tail_lines.append(raw.decode("utf-8", errors="replace").rstrip("\n\r"))
    lines.extend(tail_lines[-n_tail:])

    # random sample from middle
    if n_random > 0 and size > 1024:
        offsets = [random.randint(0, size - 1) for _ in range(n_random)]
        lines.extend(read_lines_at_offsets(path, offsets))

    return lines


def verify_file(path, library, pattern, sample_n, full):
    if not os.path.exists(path):
        return None  # file doesn't exist, skip silently

    size = os.path.getsize(path)
    if size == 0:
        print("  {:40s} EMPTY".format(os.path.basename(path)))
        return False

    total_lines = count_lines_fast(path)
    sampled = sample_lines(path, n_head=sample_n, n_tail=sample_n,
                           n_random=sample_n, full=full)
    checked = len(sampled)

    bad = []
    for line in sampled:
        if not line:
            continue
        if not pattern.match(line):
            bad.append(line)

    size_mb = size / (1024 * 1024)
    status = "OK" if not bad else "FAIL ({} bad/{} checked)".format(len(bad), checked)

    print("  {:<40s} {:>10,} lines  {:>8.1f} MB  {}".format(
        os.path.basename(path), total_lines, size_mb, status))

    if bad:
        print("    First bad line:")
        print("      " + bad[0][:120])

    return len(bad) == 0


def main():
    parser = argparse.ArgumentParser(
        description="Verify benchmark log file format correctness"
    )
    parser.add_argument(
        "--dir", default=".",
        help="directory containing log files (default: current directory)",
    )
    parser.add_argument(
        "--sample", type=int, default=200,
        help="lines to sample from head, tail, and random positions each (default: 200)",
    )
    parser.add_argument(
        "--full", action="store_true",
        help="check every line (slow for large files)",
    )
    args = parser.parse_args()

    print("Verifying benchmark log files in: {}".format(os.path.abspath(args.dir)))
    if args.full:
        print("Mode: full (checking every line)")
    else:
        print("Mode: sample ({} head + {} tail + {} random per file)".format(
            args.sample, args.sample, args.sample))
    print()

    any_found = False
    all_ok = True

    for pattern_glob, library, pattern in LOG_FILE_PATTERNS:
        matched = sorted(glob.glob(os.path.join(args.dir, pattern_glob)))
        if not matched:
            continue
        for path in matched:
            result = verify_file(path, library, pattern, args.sample, args.full)
            if result is not None:
                any_found = True
                if not result:
                    all_ok = False

    if not any_found:
        print("No log files found.")
        sys.exit(0)

    print()
    if all_ok:
        print("All checked files: OK")
    else:
        print("Some files have format errors.")
        sys.exit(1)


if __name__ == "__main__":
    main()
