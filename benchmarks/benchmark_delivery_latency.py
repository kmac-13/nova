#!/usr/bin/env python3
# benchmark_delivery_latency.py
# Runs the delivery latency benchmark across thread counts, collecting
# results from Nova, spdlog, and Quill in separate process invocations
# so a Quill crash never corrupts other results.
#
# Usage:
#   python benchmark_delivery_latency.py --exe <path> [options]
#
# Examples:
#   python benchmark_delivery_latency.py --exe .\\build\\...\\benchmark_delivery_latency.exe
#   python benchmark_delivery_latency.py --threads 1 2 4 8 --messages 1000000
#   python benchmark_delivery_latency.py --out results.txt
#   python benchmark_delivery_latency.py --mingw-bin C:\\Qt\\Tools\\mingw1310_64\\bin

import argparse
import datetime
import glob
import os
import subprocess
import sys


def get_hw_info():
    """Gather CPU and OS info for benchmark result headers."""
    import os as _os
    cpu_name = "unknown"
    if sys.platform == "win32":
        try:
            import winreg
            key = winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE,
                r"HARDWARE\DESCRIPTION\System\CentralProcessor\0")
            cpu_name = winreg.QueryValueEx(key, "ProcessorNameString")[0].strip()
            winreg.CloseKey(key)
        except Exception:
            pass
    elif sys.platform == "linux":
        try:
            with open("/proc/cpuinfo") as f:
                for line in f:
                    if line.startswith("model name"):
                        cpu_name = line.split(":", 1)[1].strip()
                        break
        except Exception:
            pass
    elif sys.platform == "darwin":
        try:
            import subprocess as _sp
            cpu_name = _sp.check_output(
                ["sysctl", "-n", "machdep.cpu.brand_string"], text=True).strip()
        except Exception:
            pass

    # storage type for the drive containing the benchmark output
    storage = "unknown"
    try:
        if sys.platform == "win32":
            import subprocess as _sp
            # PowerShell query — gets MediaType for the C: drive or equivalent
            ps = (
                "Get-PhysicalDisk | "
                "Select-Object -First 1 MediaType,FriendlyName | "
                "ConvertTo-Json"
            )
            out = _sp.check_output(
                ["powershell", "-NoProfile", "-Command", ps],
                text=True, stderr=_sp.DEVNULL, timeout=10
            )
            import json as _json
            disk = _json.loads(out)
            media = disk.get("MediaType", "")
            name  = disk.get("FriendlyName", "")
            storage = (name + " (" + media + ")").strip()
        elif sys.platform == "linux":
            import subprocess as _sp
            result = _sp.check_output(["df", "-P", "."], text=True)
            device = result.splitlines()[1].split()[0].replace("/dev/", "")
            base = device.rstrip("0123456789p")
            rot = open(f"/sys/block/{base}/queue/rotational").read().strip()
            storage = "HDD" if rot == "1" else "SSD/NVMe"
        elif sys.platform == "darwin":
            import subprocess as _sp
            out = _sp.check_output(
                ["system_profiler", "SPStorageDataType", "-json"],
                text=True, stderr=_sp.DEVNULL)
            import json as _json
            data = _json.loads(out)
            items = data.get("SPStorageDataType", [{}])
            if items:
                storage = items[0].get("physical_drive", {}).get("medium_type", "unknown")
    except Exception:
        pass
    return {
        "cpu":     cpu_name,
        "cores":   str(_os.cpu_count()) + " logical",
        "os":      sys.platform + " / " + __import__("platform").platform(),
        "storage": storage,
    }



def find_mingw_bin(exe_path):
    candidates = []
    if sys.platform != "win32":
        return candidates
    path = os.path.dirname(os.path.abspath(exe_path))
    for _ in range(10):
        for pattern in ["Tools\\mingw*\\bin", "Tools\\mingw*_64\\bin"]:
            candidates.extend(glob.glob(os.path.join(path, pattern)))
        parent = os.path.dirname(path)
        if parent == path:
            break
        path = parent
    for fixed in [
        r"C:\Qt\Tools\mingw1310_64\bin",
        r"C:\Qt\Tools\mingw1120_64\bin",
        r"C:\Qt\Tools\mingw900_64\bin",
        r"C:\msys64\mingw64\bin",
        r"C:\mingw64\bin",
    ]:
        if os.path.isdir(fixed):
            candidates.append(fixed)
    return candidates


def build_env(exe_path, extra_bin=""):
    env = os.environ.copy()
    dirs = []
    if extra_bin:
        dirs.append(extra_bin)
    dirs.extend(find_mingw_bin(exe_path))
    if dirs:
        env["PATH"] = os.pathsep.join(dirs) + os.pathsep + env.get("PATH", "")
    return env


def run_one(exe, messages, threads, env, extra_args=None):
    cmd = [exe, str(messages), str(threads)] + (extra_args or [])
    try:
        result = subprocess.run(cmd, capture_output=True, env=env)
    except FileNotFoundError:
        print("Error: executable not found: " + exe, file=sys.stderr)
        sys.exit(1)
    stdout = result.stdout.decode("utf-8", errors="replace")
    stderr = result.stderr.decode("utf-8", errors="replace")
    return result.returncode, stdout, stderr


def is_data_row(line):
    s = line.strip()
    return (s
        and not s.startswith("#")
        and not s.startswith("-")
        and not s.startswith("Library")
        and not s.startswith("benchmark_"))


def main():
    parser = argparse.ArgumentParser(
        description="Delivery latency benchmark runner (Nova vs spdlog vs Quill)"
    )
    parser.add_argument(
        "--exe",
        default=(
            ".\\benchmark_delivery_latency.exe"
            if sys.platform == "win32"
            else "./benchmark_delivery_latency"
        ),
    )
    parser.add_argument(
        "--threads", nargs="+", type=int, default=[1, 2, 4, 8], metavar="N",
        help="thread counts to benchmark (default: 1 2 4 8)",
    )
    parser.add_argument(
        "--messages", type=int, default=1000000,
        help="total messages to deliver per configuration (default: 1000000)",
    )
    parser.add_argument("--out", default="", help="also write results to this file")
    parser.add_argument("--mingw-bin", default="", metavar="DIR")
    parser.add_argument("--null-sink", action="store_true",
                        help="use null sink instead of file sink (measures queue/format overhead only)")
    args = parser.parse_args()

    env = build_env(args.exe, args.mingw_bin)
    out_file = open(args.out, "w", encoding="utf-8") if args.out else None

    def emit(line=""):
        print(line)
        if out_file:
            out_file.write(line + "\n")

    emit("# Delivery latency benchmark (guaranteed delivery, no drops)")
    emit("# " + datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S"))
    emit("# Exe:      " + args.exe)
    emit("# Messages: {:,} per configuration".format(args.messages))
    emit("# Threads:  " + " ".join(str(n) for n in args.threads))
    emit("# Sink:     " + ("null (queue/format overhead only)" if args.null_sink else "file"))
    emit("#")
    info = get_hw_info()
    emit("# CPU:      " + info["cpu"])
    emit("# Cores:    " + info["cores"])
    emit("# OS:       " + info["os"])
    emit("# Storage:  " + info["storage"])

    # print header once
    emit("{:<14}{:<9}{:<12}{:<12}{:<14}{}".format(
        "Library", "Threads", "Messages", "Elapsed(s)", "Msgs/s", "ns/msg"))
    emit("-" * 74)

    # libraries to run and their flags
    # spdlog sync and async are both in the --spdlog-only invocation
    libraries = [
        ("Nova",         ["--nova-only"]),
        ("spdlog",       ["--spdlog-only"]),
        ("Quill",        ["--quill-only"]),
    ]

    for n in args.threads:
        print("--- threads={} ---".format(n), file=sys.stderr)

        for lib_name, flags in libraries:
            print("  {}...".format(lib_name), end="", file=sys.stderr)
            sys.stderr.flush()

            extra_flags = list(flags)
            if args.null_sink:
                extra_flags.append("--null-sink")
            rc, stdout, stderr = run_one(args.exe, args.messages, n, env, extra_flags)

            # forward stderr excluding internal markers
            stderr_lines = [l for l in stderr.splitlines()
                            if not l.startswith(("[quill]", "[main]", "benchmark_delivery"))]
            if stderr_lines:
                print("", file=sys.stderr)  # newline after "  Nova..."
                for line in stderr_lines:
                    print("  " + line, file=sys.stderr)

            if not stdout.strip():
                print(" no output (rc=0x{:08X})".format(rc & 0xFFFFFFFF),
                      file=sys.stderr)
                if sys.platform == "win32" and rc not in (0, 1):
                    print(
                        "  (DLL error? try --mingw-bin C:\\Qt\\Tools\\mingw1310_64\\bin)",
                        file=sys.stderr,
                    )
                continue

            rows = [l for l in stdout.splitlines() if is_data_row(l)]
            for row in rows:
                emit(row)

            print(" done", file=sys.stderr)

    emit()
    if out_file:
        out_file.close()
        print("Results saved to: " + args.out, file=sys.stderr)


if __name__ == "__main__":
    main()
