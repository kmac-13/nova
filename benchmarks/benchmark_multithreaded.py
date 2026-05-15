#!/usr/bin/env python3
# benchmark_multithreaded.py
# Runs the multi-threaded Nova vs spdlog vs Quill benchmark across a range of thread counts.
#
# Nova and Quill are run as separate process invocations per thread count so
# a Quill crash never corrupts Nova output.
#
# Usage:
#   python benchmark_multithreaded.py --exe <path> [options]
#
# Examples:
#   python benchmark_multithreaded.py --exe .\\build\\...\\benchmark_multithreaded.exe
#   python benchmark_multithreaded.py --threads 1 2 4 8 --duration 5 --out results.txt
#   python benchmark_multithreaded.py --mingw-bin C:\\Qt\\Tools\\mingw1310_64\\bin

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


def run_one(exe, n, duration, warmup, env, extra_args=None):
    cmd = [exe, str(n), str(duration), str(warmup)] + (extra_args or [])
    try:
        result = subprocess.run(cmd, capture_output=True, env=env)
    except FileNotFoundError:
        print("Error: executable not found: " + exe, file=sys.stderr)
        sys.exit(1)
    stdout = result.stdout.decode("utf-8", errors="replace")
    stderr = result.stderr.decode("utf-8", errors="replace")
    return result.returncode, stdout, stderr


def is_data_row(line):
    # a data row starts with a library name (no # or -)
    s = line.strip()
    return s and not s.startswith("#") and not s.startswith("-") and not s.startswith("Library")


def main():
    parser = argparse.ArgumentParser(
        description="Multi-threaded Nova vs spdlog vs Quill benchmark runner"
    )
    parser.add_argument(
        "--exe",
        default=(
            ".\\benchmark_multithreaded.exe"
            if sys.platform == "win32"
            else "./benchmark_multithreaded"
        ),
    )
    parser.add_argument("--threads", nargs="+", type=int, default=[1, 2, 4, 8, 16], metavar="N")
    parser.add_argument("--duration", type=float, default=3.0)
    parser.add_argument("--warmup", type=int, default=1)
    parser.add_argument("--out", default="")
    parser.add_argument("--mingw-bin", default="", metavar="DIR")
    parser.add_argument("--nova-only", action="store_true")
    parser.add_argument("--nova-batch-only", action="store_true")
    parser.add_argument("--spdlog-only", action="store_true")
    parser.add_argument("--quill-only", action="store_true")
    parser.add_argument("--null-sink", action="store_true",
                        help="use counting sink instead of file sink (measures queue/format overhead only)")
    parser.add_argument("--pool-kb", type=int, default=256,
                        choices=[256, 512, 1024, 4096, 16384],
                        help="Nova async pool size in KB (default: 256)")
    parser.add_argument("--debug", action="store_true", help="print raw subprocess output")
    args = parser.parse_args()

    env = build_env(args.exe, args.mingw_bin)
    out_file = open(args.out, "w", encoding="utf-8") if args.out else None

    def emit(line=""):
        print(line)
        if out_file:
            out_file.write(line + "\n")

    emit("# Nova vs spdlog vs Quill multi-threaded benchmark")
    emit("# " + datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S"))
    emit("# Exe:      " + args.exe)
    emit("# Duration: {}s per configuration, {}s warmup".format(args.duration, args.warmup))
    emit("# Threads:  " + " ".join(str(n) for n in args.threads))
    emit("# Sink:     " + ("counting (queue/format overhead only)" if args.null_sink else "file"))
    emit("# Nova pool: {}KB".format(args.pool_kb))
    emit("#")
    info = get_hw_info()
    emit("# CPU:      " + info["cpu"])
    emit("# Cores:    " + info["cores"])
    emit("# OS:       " + info["os"])
    emit("# Storage:  " + info["storage"])

    # print table header once
    emit("{:<20}{:<9}{:<10}{:<14}{:<14}{:<12}{:<10}{:<14}{}".format(
        "Library", "Threads", "Dur(s)", "Logged", "Delivered", "Dropped", "Drop%",
        "Logged/s", "Delivered/s"))
    emit("-" * 117)

    for n in args.threads:
        print("--- threads={} ---".format(n), file=sys.stderr)

        for library, flag in [
            ("Nova",       "--nova-only"),
            ("Nova batch", "--nova-batch-only"),
            ("spdlog",     "--spdlog-only"),
            ("Quill",      "--quill-only"),
        ]:
            if library == "Nova"       and args.quill_only:
                continue
            if library == "Nova batch" and ( args.quill_only or args.spdlog_only ):
                continue
            if library == "spdlog"     and ( args.nova_only or args.quill_only or args.nova_batch_only ):
                continue
            if library == "Quill"      and args.nova_only:
                continue

            flags = [flag]
            if args.null_sink:
                flags.append("--null-sink")
            if args.pool_kb != 256:
                flags.extend(["--pool-kb", str(args.pool_kb)])

            rc, stdout, stderr = run_one(
                args.exe, n, args.duration, args.warmup, env, flags
            )

            if args.debug:
                print("[debug] {} rc={} stdout_bytes={} stderr_bytes={}".format(
                    library, rc, len(stdout), len(stderr)), file=sys.stderr)
                if stdout:
                    print("[debug] stdout:\n" + stdout[:500], file=sys.stderr)

            # forward stderr, suppressing internal markers
            for line in stderr.splitlines():
                if not line.startswith(("[quill]", "[main]")):
                    print(line, file=sys.stderr)

            if not stdout.strip():
                print("  {} threads={}: no output (rc=0x{:08X})".format(
                    library, n, rc & 0xFFFFFFFF), file=sys.stderr)
                continue

            # non-zero exit on Quill is expected on MinGW (cleanup crash after
            # data is already printed) — only warn if there's no output at all
            if rc not in (0, 1) and stdout.strip():
                pass  # crash after data printed — acceptable

            # extract just the data rows from the output
            for line in stdout.splitlines():
                if is_data_row(line):
                    emit(line)

    emit()
    if out_file:
        out_file.close()
        print("Results saved to: " + args.out, file=sys.stderr)


if __name__ == "__main__":
    main()
