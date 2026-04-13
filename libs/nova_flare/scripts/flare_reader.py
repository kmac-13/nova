#!/usr/bin/env python3
"""
Flare Crash Log Reader

Reads Flare binary crash logs and displays them in human-readable format.
Optionally uses a tag dictionary to show tag names instead of hashes, and
can symbolicate stack frame addresses using addr2line or llvm-symbolizer.

Usage:
    python3 flare_reader.py crash.flare
    python3 flare_reader.py crash.flare --dict crash.tags
    python3 flare_reader.py crash.flare --dict crash.tags --format json
    python3 flare_reader.py crash.flare --binary ./myapp --addr2line addr2line
    python3 flare_reader.py crash.flare --binary ./myapp --addr2line llvm-symbolizer
"""

import sys
import struct
import argparse
import json
import subprocess
from pathlib import Path
from typing import Dict, List, Optional, Tuple
from datetime import datetime

# TLV Types (must match tlv.h)
TLV_INVALID           = 0
TLV_RECORD_BEGIN      = 1
TLV_RECORD_SIZE       = 2
TLV_RECORD_STATUS     = 3
TLV_SEQUENCE_NUMBER   = 4
TLV_TIMESTAMP_NS      = 10
TLV_TAG_ID            = 11
TLV_FILE_NAME         = 12
TLV_LINE_NUMBER       = 13
TLV_FUNCTION_NAME     = 14
TLV_PROCESS_ID        = 15
TLV_THREAD_ID         = 16
TLV_MESSAGE_BYTES     = 20
TLV_MESSAGE_TRUNCATED = 21
TLV_FAULT_ADDRESS     = 30  # si_addr at the point of fault (uint64_t); signal records only
TLV_LOAD_BASE_ADDRESS = 31  # runtime load base of main executable (uint64_t)
TLV_ASLR_OFFSET       = 32  # ASLR slide; equals load base for PIE binaries (uint64_t)
TLV_STACK_FRAMES      = 33  # packed uint64_t return addresses, innermost first
TLV_REGISTER_LAYOUT   = 34  # RegisterLayoutId byte identifying CpuRegisters layout
TLV_CPU_REGISTERS     = 35  # packed uint64_t register values; layout per TLV_REGISTER_LAYOUT
TLV_RECORD_END        = 0xFFFF

# Record status values
STATUS_UNKNOWN     = 0
STATUS_IN_PROGRESS = 1
STATUS_COMPLETE    = 2
STATUS_TRUNCATED   = 3

STATUS_NAMES = {
    STATUS_UNKNOWN:     "Unknown",
    STATUS_IN_PROGRESS: "InProgress (Torn Write)",
    STATUS_COMPLETE:    "Complete",
    STATUS_TRUNCATED:   "Truncated",
}

# RegisterLayoutId values (must match tlv.h)
LAYOUT_UNKNOWN = 0
LAYOUT_X86_64  = 1
LAYOUT_ARM64   = 2
LAYOUT_ARM32   = 3

# Register names per layout, in the order they appear in the CpuRegisters TLV.
REGISTER_NAMES = {
    LAYOUT_X86_64: [
        "rax", "rbx", "rcx", "rdx", "rsi", "rdi", "rbp", "rsp",
        "r8",  "r9",  "r10", "r11", "r12", "r13", "r14", "r15",
        "rip", "rflags",
    ],
    LAYOUT_ARM64: [
        "x0",  "x1",  "x2",  "x3",  "x4",  "x5",  "x6",  "x7",
        "x8",  "x9",  "x10", "x11", "x12", "x13", "x14", "x15",
        "x16", "x17", "x18", "x19", "x20", "x21", "x22", "x23",
        "x24", "x25", "x26", "x27", "x28", "fp",  "lr",  "sp",
        "pc",  "pstate",
    ],
    LAYOUT_ARM32: [
        "r0",  "r1",  "r2",  "r3",  "r4",  "r5",  "r6",  "r7",
        "r8",  "r9",  "r10", "fp",  "ip",  "sp",  "lr",  "pc",
        "cpsr",
    ],
}

# Registers that hold addresses - annotated with static equivalents when ASLR is known
ADDRESS_REGISTERS = {
    LAYOUT_X86_64: {"rip", "rsp", "rbp"},
    LAYOUT_ARM64:  {"pc", "sp", "lr", "fp"},
    LAYOUT_ARM32:  {"pc", "sp", "lr", "fp"},
}

FLARE_MAGIC = 0x4B4D41435F464C52  # "KMAC_FLR"


class FlareRecord:
    """Parsed Flare record"""

    def __init__(self):
        self.sequence          = 0
        self.status            = STATUS_UNKNOWN
        self.timestamp_ns      = 0
        self.tag_id            = 0
        self.tag_name          = None
        self.file              = ""
        self.line              = 0
        self.function          = ""
        self.process_id        = 0
        self.thread_id         = 0
        self.message           = ""
        self.message_truncated = False
        self.fault_address     = None   # int or None when TLV absent
        self.load_base_address = 0
        self.aslr_offset       = None   # int or None when TLV absent
        self.stack_frames      = []     # list of int (raw runtime addresses)
        self.register_layout   = LAYOUT_UNKNOWN
        self.registers         = []     # list of int


def load_tag_dictionary(filepath: str) -> Dict[int, str]:
    """Load tag dictionary from file"""
    tags = {}
    try:
        with open(filepath, 'r') as f:
            for line in f:
                line = line.strip()
                if not line or line.startswith('#'):
                    continue
                parts = line.split(',')
                if len(parts) == 2:
                    name     = parts[0].strip()
                    hash_str = parts[1].strip()
                    hash_val = int(hash_str, 16) if hash_str.startswith('0x') else int(hash_str)
                    tags[hash_val] = name
        return tags
    except Exception as e:
        print(f"Warning: Could not load tag dictionary: {e}", file=sys.stderr)
        return {}


def parse_record(data: bytes, offset: int) -> Optional[Tuple[FlareRecord, int]]:
    """Parse a single record from data starting at offset"""

    # check for magic
    if offset + 8 > len(data):
        return None

    magic = struct.unpack('<Q', data[offset:offset + 8])[0]
    if magic != FLARE_MAGIC:
        return None
    offset += 8

    # read size
    if offset + 4 > len(data):
        return None

    size = struct.unpack('<I', data[offset:offset + 4])[0]
    offset += 4

    # validate size
    if size == 0 or size > 64 * 1024:
        return None

    # create record
    record = FlareRecord()

    # parse TLVs
    record_end = offset - 12 + size  # -12: magic+size already consumed

    while offset < record_end:
        if offset + 4 > len(data):
            break

        tlv_type, tlv_length = struct.unpack('<HH', data[offset:offset + 4])
        offset += 4

        if offset + tlv_length > len(data):
            break

        value = data[offset:offset + tlv_length]
        offset += tlv_length

        # parse based on type
        if tlv_type == TLV_RECORD_STATUS and tlv_length == 1:
            record.status = value[0]

        elif tlv_type == TLV_SEQUENCE_NUMBER and tlv_length == 8:
            record.sequence = struct.unpack('<Q', value)[0]

        elif tlv_type == TLV_TIMESTAMP_NS and tlv_length == 8:
            record.timestamp_ns = struct.unpack('<Q', value)[0]

        elif tlv_type == TLV_TAG_ID and tlv_length == 8:
            record.tag_id = struct.unpack('<Q', value)[0]

        elif tlv_type == TLV_FILE_NAME:
            record.file = value.decode('utf-8', errors='replace')

        elif tlv_type == TLV_LINE_NUMBER and tlv_length == 4:
            record.line = struct.unpack('<I', value)[0]

        elif tlv_type == TLV_FUNCTION_NAME:
            record.function = value.decode('utf-8', errors='replace')

        elif tlv_type == TLV_PROCESS_ID and tlv_length == 4:
            record.process_id = struct.unpack('<I', value)[0]

        elif tlv_type == TLV_THREAD_ID and tlv_length == 4:
            record.thread_id = struct.unpack('<I', value)[0]

        elif tlv_type == TLV_MESSAGE_BYTES:
            record.message = value.decode('utf-8', errors='replace')

        elif tlv_type == TLV_MESSAGE_TRUNCATED and tlv_length == 1:
            record.message_truncated = (value[0] != 0)

        elif tlv_type == TLV_FAULT_ADDRESS and tlv_length == 8:
            record.fault_address = struct.unpack('<Q', value)[0]

        elif tlv_type == TLV_LOAD_BASE_ADDRESS and tlv_length == 8:
            record.load_base_address = struct.unpack('<Q', value)[0]

        elif tlv_type == TLV_ASLR_OFFSET and tlv_length == 8:
            record.aslr_offset = struct.unpack('<Q', value)[0]

        elif tlv_type == TLV_STACK_FRAMES and tlv_length > 0 and tlv_length % 8 == 0:
            frame_count = tlv_length // 8
            record.stack_frames = list(struct.unpack(f'<{frame_count}Q', value))

        elif tlv_type == TLV_REGISTER_LAYOUT and tlv_length == 1:
            record.register_layout = value[0]

        elif tlv_type == TLV_CPU_REGISTERS and tlv_length > 0 and tlv_length % 8 == 0:
            reg_count = tlv_length // 8
            record.registers = list(struct.unpack(f'<{reg_count}Q', value))

        elif tlv_type == TLV_RECORD_END:
            break

    return record, offset


def _aslr_offset(record: FlareRecord) -> int:
    """Return the best available ASLR offset for address relocation"""
    if record.aslr_offset is not None:
        return record.aslr_offset
    return record.load_base_address


def symbolicate_addresses(
    addrs: List[int],
    binary: str,
    addr2line_tool: str,
) -> Dict[int, str]:
    """
    Resolve static (file-relative) addresses to function/line strings using
    addr2line or llvm-symbolizer.  Returns a dict mapping address -> symbol
    string; unresolvable addresses map to an empty string.
    """
    if not addrs or not binary:
        return {}

    hex_addrs = [hex(a) for a in addrs]
    try:
        result = subprocess.run(
            [addr2line_tool, '-e', binary, '-f', '-C', '-p'] + hex_addrs,
            capture_output=True,
            text=True,
            timeout=10,
        )
        lines   = result.stdout.strip().splitlines()
        symbols = {}
        for addr, line in zip(addrs, lines):
            stripped = line.strip()
            # addr2line emits "?? ??:0" for unknown addresses
            symbols[addr] = stripped if stripped and '??' not in stripped else ''
        return symbols
    except (FileNotFoundError, subprocess.TimeoutExpired, OSError) as e:
        print(f"Warning: symbolication failed ({addr2line_tool}): {e}", file=sys.stderr)
        return {}


def format_timestamp(timestamp_ns: int) -> str:
    """Format nanosecond timestamp as a human-readable string"""
    dt = datetime.fromtimestamp(timestamp_ns / 1_000_000_000)
    return dt.strftime('%Y-%m-%d %H:%M:%S.%f')[:-3]  # trim to milliseconds


def _layout_name(layout_id: int) -> str:
    return {
        LAYOUT_UNKNOWN: "unknown",
        LAYOUT_X86_64:  "x86-64",
        LAYOUT_ARM64:   "ARM64",
        LAYOUT_ARM32:   "ARM32",
    }.get(layout_id, f"layout-{layout_id}")


def print_record_text(
    record: FlareRecord,
    record_num: int,
    binary: Optional[str] = None,
    addr2line_tool: Optional[str] = None,
):
    """Print a record in human-readable text format."""
    print(f"\n{'=' * 60}")
    print(f"Record {record_num}")
    print(f"{'=' * 60}")
    print(f"Sequence:  {record.sequence}")
    print(f"Status:    {STATUS_NAMES.get(record.status, f'Unknown ({record.status})')}")
    print(f"Timestamp: {format_timestamp(record.timestamp_ns)} ({record.timestamp_ns} ns)")

    if record.tag_name:
        print(f"Tag:       {record.tag_name} (0x{record.tag_id:016x})")
    else:
        print(f"Tag ID:    0x{record.tag_id:016x}")

    if record.process_id:
        print(f"Process:   {record.process_id}")
    if record.thread_id:
        print(f"Thread:    {record.thread_id}")

    print(f"Location:  {record.file}:{record.line}")
    if record.function:
        print(f"Function:  {record.function}")

    msg_suffix = " [TRUNCATED]" if record.message_truncated else ""
    print(f"Message:   {record.message}{msg_suffix}")

    if record.fault_address is not None:
        print(f"Fault:     0x{record.fault_address:016x}")

    if record.load_base_address:
        print(f"Load base: 0x{record.load_base_address:016x}")
    if record.aslr_offset is not None:
        print(f"ASLR:      0x{record.aslr_offset:016x}")

    aslr = _aslr_offset(record)

    if record.stack_frames:
        will_sym = bool(binary and addr2line_tool)
        symbols: Dict[int, str] = {}
        if will_sym and aslr:
            static_addrs = [addr - aslr for addr in record.stack_frames]
            symbols = symbolicate_addresses(static_addrs, binary, addr2line_tool)

        print(f"Stack:     {len(record.stack_frames)} frame(s)")
        for i, addr in enumerate(record.stack_frames):
            if aslr:
                static_addr = addr - aslr
                sym = symbols.get(static_addr, '')
                sym_str = f"  {sym}" if sym else ""
                print(f"  [{i:2d}]  0x{addr:016x}  ->  static 0x{static_addr:016x}{sym_str}")
            else:
                print(f"  [{i:2d}]  0x{addr:016x}")
        if not aslr:
            print("  (no ASLR offset available; cannot compute static addresses)")
        elif not will_sym:
            print("  (pass --binary and --addr2line to resolve symbols)")

    if record.registers:
        names = REGISTER_NAMES.get(record.register_layout, [])
        addr_set = ADDRESS_REGISTERS.get(record.register_layout, set())
        print(f"Registers: {_layout_name(record.register_layout)}")
        for i, val in enumerate(record.registers):
            name = names[i] if i < len(names) else f"reg{i}"
            if name in addr_set and aslr and val >= aslr:
                static_val = val - aslr
                print(f"  {name:<8}  0x{val:016x}  (static 0x{static_val:016x})")
            else:
                print(f"  {name:<8}  0x{val:016x}")


def record_to_json(
    record: FlareRecord,
    binary: Optional[str] = None,
    addr2line_tool: Optional[str] = None,
) -> dict:
    """Convert a record to a JSON-serialisable dict."""
    aslr = _aslr_offset(record)
    will_sym = bool(binary and addr2line_tool and aslr and record.stack_frames)
    symbols: Dict[int, str] = {}
    if will_sym:
        static_addrs = [addr - aslr for addr in record.stack_frames]
        symbols = symbolicate_addresses(static_addrs, binary, addr2line_tool)

    names = REGISTER_NAMES.get(record.register_layout, [])
    addr_set = ADDRESS_REGISTERS.get(record.register_layout, set())
    reg_list = []
    for i, val in enumerate(record.registers):
        name = names[i] if i < len(names) else f"reg{i}"
        entry: dict = {"name": name, "value": f"0x{val:016x}"}
        if name in addr_set and aslr and val >= aslr:
            entry["static"] = f"0x{(val - aslr):016x}"
        reg_list.append(entry)

    def frame_entry(addr: int) -> dict:
        entry: dict = {"runtime": f"0x{addr:016x}"}
        if aslr:
            static = addr - aslr
            entry["static"] = f"0x{static:016x}"
            sym = symbols.get(static, '')
            if sym:
                entry["symbol"] = sym
        return entry

    return {
        "sequence":          record.sequence,
        "status":            STATUS_NAMES.get(record.status, f"Unknown ({record.status})"),
        "timestamp_ns":      record.timestamp_ns,
        "timestamp":         format_timestamp(record.timestamp_ns),
        "tag":               record.tag_name if record.tag_name else f"0x{record.tag_id:016x}",
        "tag_id":            f"0x{record.tag_id:016x}",
        "process_id":        record.process_id or None,
        "thread_id":         record.thread_id or None,
        "file":              record.file,
        "line":              record.line,
        "function":          record.function,
        "message":           record.message,
        "message_truncated": record.message_truncated,
        "fault_address":     f"0x{record.fault_address:016x}" if record.fault_address is not None else None,
        "load_base_address": f"0x{record.load_base_address:016x}" if record.load_base_address else None,
        "aslr_offset":       f"0x{record.aslr_offset:016x}" if record.aslr_offset is not None else None,
        "stack_frames":      [frame_entry(a) for a in record.stack_frames],
        "register_layout":   _layout_name(record.register_layout),
        "registers":         reg_list,
    }


def main():
    parser = argparse.ArgumentParser(
        description='Read and display Flare crash logs',
        epilog=(
            'Examples:\n'
            '  python3 flare_reader.py crash.flare\n'
            '  python3 flare_reader.py crash.flare --dict crash.tags\n'
            '  python3 flare_reader.py crash.flare --binary ./myapp --addr2line addr2line\n'
            '  python3 flare_reader.py crash.flare --binary ./myapp --addr2line llvm-symbolizer'
        ),
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )

    parser.add_argument('logfile', type=str, help='Flare crash log file to read')

    parser.add_argument(
        '--dict', '-d',
        type=str,
        help='Tag dictionary file (generated by flare_dict_gen.py)',
    )

    parser.add_argument(
        '--format', '-f',
        choices=['text', 'json'],
        default='text',
        help='Output format (default: text)',
    )

    parser.add_argument(
        '--binary', '-b',
        type=str,
        default=None,
        help='Path to the debug binary for addr2line symbolication',
    )

    parser.add_argument(
        '--addr2line', '-a',
        type=str,
        default='addr2line',
        metavar='TOOL',
        help=(
            'addr2line-compatible tool to use for symbolication '
            '(default: addr2line; llvm-symbolizer also works)'
        ),
    )

    args = parser.parse_args()

    tag_dict: Dict[int, str] = {}
    if args.dict:
        tag_dict = load_tag_dictionary(args.dict)
        if tag_dict:
            print(f"Loaded {len(tag_dict)} tags from dictionary", file=sys.stderr)

    try:
        with open(args.logfile, 'rb') as f:
            data = f.read()
    except FileNotFoundError:
        print(f"Error: File '{args.logfile}' not found", file=sys.stderr)
        sys.exit(1)
    except IOError as e:
        print(f"Error reading file: {e}", file=sys.stderr)
        sys.exit(1)

    records = []
    offset = 0

    while offset < len(data):
        magic_found = False
        for i in range(offset, len(data) - 7):
            if struct.unpack('<Q', data[i:i + 8])[0] == FLARE_MAGIC:
                offset = i
                magic_found = True
                break
        if not magic_found:
            break

        result = parse_record(data, offset)
        if result is None:
            offset += 1
            continue

        record, new_offset = result
        if record.tag_id in tag_dict:
            record.tag_name = tag_dict[record.tag_id]

        records.append(record)
        offset = new_offset

    binary = args.binary
    addr2line = args.addr2line

    if args.format == 'json':
        output = {
            "total_records": len(records),
            "records":       [record_to_json(r, binary, addr2line) for r in records],
        }
        print(json.dumps(output, indent=2))
    else:
        for i, record in enumerate(records, 1):
            print_record_text(record, i, binary, addr2line)
        print(f"\n{'=' * 60}")
        print(f"Total records: {len(records)}")


if __name__ == '__main__':
    main()
