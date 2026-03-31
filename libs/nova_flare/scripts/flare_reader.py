#!/usr/bin/env python3
"""
Flare Crash Log Reader

Reads Flare binary crash logs and displays them in human-readable format.
Optionally uses tag dictionary to show tag names instead of hashes.

Usage:
    python3 flare_reader.py crash.flare
    python3 flare_reader.py crash.flare --dict crash.tags
    python3 flare_reader.py crash.flare --dict crash.tags --format json
"""

import sys
import struct
import argparse
import json
from pathlib import Path
from typing import Dict, List, Optional, Tuple
from datetime import datetime

# TLV Types (must match tlv.h)
TLV_INVALID = 0
TLV_RECORD_BEGIN = 1
TLV_RECORD_SIZE = 2
TLV_RECORD_STATUS = 3
TLV_SEQUENCE_NUMBER = 4
TLV_TIMESTAMP_NS = 10
TLV_TAG_ID = 11
TLV_FILE_NAME = 12
TLV_LINE_NUMBER = 13
TLV_FUNCTION_NAME = 14
TLV_PROCESS_ID = 15
TLV_THREAD_ID = 16
TLV_MESSAGE_BYTES = 20
TLV_MESSAGE_TRUNCATED = 21
TLV_LOAD_BASE_ADDRESS = 30  # runtime load base of main executable (uint64_t)
TLV_STACK_FRAMES = 31       # packed uint64_t return addresses, innermost first
TLV_RECORD_END = 0xFFFF

# Record status values
STATUS_UNKNOWN = 0
STATUS_IN_PROGRESS = 1
STATUS_COMPLETE = 2
STATUS_TRUNCATED = 3

STATUS_NAMES = {
    STATUS_UNKNOWN: "Unknown",
    STATUS_IN_PROGRESS: "InProgress (Torn Write)",
    STATUS_COMPLETE: "Complete",
    STATUS_TRUNCATED: "Truncated"
}

FLARE_MAGIC = 0x4B4D41435F464C52  # "KMAC_FLR"

class FlareRecord:
    """Parsed Flare record"""
    def __init__(self):
        self.sequence = 0
        self.status = STATUS_UNKNOWN
        self.timestamp_ns = 0
        self.tag_id = 0
        self.tag_name = None
        self.file = ""
        self.line = 0
        self.function = ""
        self.process_id = 0
        self.thread_id = 0
        self.message = ""
        self.message_truncated = False
        self.load_base_address = 0
        self.stack_frames = []  # list of int (raw runtime addresses)

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
                    name = parts[0].strip()
                    hash_str = parts[1].strip()
                    
                    # Parse hex hash
                    if hash_str.startswith('0x'):
                        hash_val = int(hash_str, 16)
                    else:
                        hash_val = int(hash_str)
                    
                    tags[hash_val] = name
        
        return tags
    except Exception as e:
        print(f"Warning: Could not load tag dictionary: {e}", file=sys.stderr)
        return {}

def parse_record(data: bytes, offset: int) -> Optional[Tuple[FlareRecord, int]]:
    """Parse a single record from data starting at offset"""
    
    # Check for magic
    if offset + 8 > len(data):
        return None
    
    magic = struct.unpack('<Q', data[offset:offset+8])[0]
    if magic != FLARE_MAGIC:
        return None
    
    offset += 8
    
    # Read size
    if offset + 4 > len(data):
        return None
    
    size = struct.unpack('<I', data[offset:offset+4])[0]
    offset += 4
    
    # Validate size
    if size == 0 or size > 64*1024:
        return None
    
    # Create record
    record = FlareRecord()
    
    # Parse TLVs
    record_end = offset - 12 + size  # -12 for magic+size already read
    
    while offset < record_end:
        if offset + 4 > len(data):
            break

        tlv_type, tlv_length = struct.unpack('<HH', data[offset:offset+4])
        offset += 4

        if offset + tlv_length > len(data):
            break

        value = data[offset:offset+tlv_length]
        offset += tlv_length

        # Parse based on type
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

        elif tlv_type == TLV_LOAD_BASE_ADDRESS and tlv_length == 8:
            record.load_base_address = struct.unpack('<Q', value)[0]

        elif tlv_type == TLV_STACK_FRAMES and tlv_length > 0 and tlv_length % 8 == 0:
            frame_count = tlv_length // 8
            record.stack_frames = list(struct.unpack(f'<{frame_count}Q', value))

        elif tlv_type == TLV_RECORD_END:
            break
    
    return record, offset

def format_timestamp(timestamp_ns: int) -> str:
    """Format timestamp in human-readable form"""
    # Convert nanoseconds to seconds
    timestamp_s = timestamp_ns / 1_000_000_000
    dt = datetime.fromtimestamp(timestamp_s)
    return dt.strftime('%Y-%m-%d %H:%M:%S.%f')[:-3]  # Trim to milliseconds

def print_record_text(record: FlareRecord, record_num: int):
    """Print record in text format"""
    print(f"\n{'='*60}")
    print(f"Record {record_num}")
    print(f"{'='*60}")
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

    if record.stack_frames:
        base = record.load_base_address
        base_str = f"0x{base:016x}" if base else "unknown"
        print(f"Stack:     {len(record.stack_frames)} frames  (load base: {base_str})")
        for i, addr in enumerate(record.stack_frames):
            if base:
                static_addr = addr - base
                print(f"  [{i:2d}]  0x{addr:016x}  ->  static 0x{static_addr:016x}")
            else:
                print(f"  [{i:2d}]  0x{addr:016x}")
        if not base:
            print("  (load base address unavailable; cannot compute static addresses)")
        else:
            print("  (pass binary path to addr2line/llvm-symbolizer to resolve symbols)")

def print_record_json(record: FlareRecord) -> dict:
    """Convert record to JSON dict"""
    return {
        "sequence": record.sequence,
        "status": STATUS_NAMES.get(record.status, f"Unknown ({record.status})"),
        "timestamp_ns": record.timestamp_ns,
        "timestamp": format_timestamp(record.timestamp_ns),
        "tag": record.tag_name if record.tag_name else f"0x{record.tag_id:016x}",
        "tag_id": f"0x{record.tag_id:016x}",
        "process_id": record.process_id if record.process_id else None,
        "thread_id": record.thread_id if record.thread_id else None,
        "file": record.file,
        "line": record.line,
        "function": record.function,
        "message": record.message,
        "message_truncated": record.message_truncated,
        "load_base_address": f"0x{record.load_base_address:016x}" if record.load_base_address else None,
        "stack_frames": [f"0x{addr:016x}" for addr in record.stack_frames],
        "stack_frames_static": [
            f"0x{(addr - record.load_base_address):016x}"
            for addr in record.stack_frames
        ] if record.load_base_address and record.stack_frames else [],
    }

def main():
    parser = argparse.ArgumentParser(
        description='Read and display Flare crash logs',
        epilog='Example: python3 flare_reader.py crash.flare --dict crash.tags'
    )

    parser.add_argument(
        'logfile',
        type=str,
        help='Flare crash log file to read'
    )

    parser.add_argument(
        '--dict',
        '-d',
        type=str,
        help='Tag dictionary file (generated by flare_dict_gen.py)'
    )

    parser.add_argument(
        '--format',
        '-f',
        choices=['text', 'json'],
        default='text',
        help='Output format (default: text)'
    )

    args = parser.parse_args()

    # Load tag dictionary if provided
    tag_dict = {}
    if args.dict:
        tag_dict = load_tag_dictionary(args.dict)
        if tag_dict:
            print(f"Loaded {len(tag_dict)} tags from dictionary", file=sys.stderr)

    # Read crash log file
    try:
        with open(args.logfile, 'rb') as f:
            data = f.read()
    except FileNotFoundError:
        print(f"Error: File '{args.logfile}' not found", file=sys.stderr)
        sys.exit(1)
    except IOError as e:
        print(f"Error reading file: {e}", file=sys.stderr)
        sys.exit(1)

    # Parse records
    records = []
    offset = 0
    record_num = 0

    while offset < len(data):
        # Search for next magic number
        magic_found = False
        for i in range(offset, len(data) - 7):
            if struct.unpack('<Q', data[i:i+8])[0] == FLARE_MAGIC:
                offset = i
                magic_found = True
                break

        if not magic_found:
            break

        # Try to parse record
        result = parse_record(data, offset)
        if result is None:
            offset += 1
            continue

        record, new_offset = result

        # Apply tag dictionary
        if record.tag_id in tag_dict:
            record.tag_name = tag_dict[record.tag_id]

        records.append(record)
        offset = new_offset
        record_num += 1

    # Output records
    if args.format == 'json':
        output = {
            "total_records": len(records),
            "records": [print_record_json(r) for r in records]
        }
        print(json.dumps(output, indent=2))
    else:
        for i, record in enumerate(records, 1):
            print_record_text(record, i)
        
        print(f"\n{'='*60}")
        print(f"Total records: {len(records)}")

if __name__ == '__main__':
    main()
