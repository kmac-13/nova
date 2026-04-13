#!/usr/bin/env python3
"""
Tests for flare_reader.py

Builds synthetic .flare binary records directly (no C++ binary required)
and verifies that flare_reader.py parses and displays them correctly.

Usage:
    python3 test_flare_reader.py
    python3 test_flare_reader.py -v          # verbose
    python3 test_flare_reader.py FlareReaderTests.test_crash_context  # single test
"""

import io
import json
import struct
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

# ---------------------------------------------------------------------------
# Locate flare_reader.py relative to this file so the tests work regardless
# of the current working directory
# ---------------------------------------------------------------------------
SCRIPT = Path(__file__).parent / 'flare_reader.py'

# ---------------------------------------------------------------------------
# TLV constants (must match tlv.h)
# ---------------------------------------------------------------------------
FLARE_MAGIC         = 0x4B4D41435F464C52

TLV_RECORD_STATUS   = 3
TLV_SEQUENCE_NUMBER = 4
TLV_TIMESTAMP_NS    = 10
TLV_TAG_ID          = 11
TLV_FILE_NAME       = 12
TLV_LINE_NUMBER     = 13
TLV_FUNCTION_NAME   = 14
TLV_PROCESS_ID      = 15
TLV_THREAD_ID       = 16
TLV_MESSAGE_BYTES   = 20
TLV_MESSAGE_TRUNCATED = 21
TLV_FAULT_ADDRESS   = 30
TLV_LOAD_BASE_ADDRESS = 31
TLV_ASLR_OFFSET     = 32
TLV_STACK_FRAMES    = 33
TLV_REGISTER_LAYOUT = 34
TLV_CPU_REGISTERS   = 35
TLV_RECORD_END      = 0xFFFF

STATUS_COMPLETE   = 2
STATUS_TRUNCATED  = 3

LAYOUT_X86_64 = 1
LAYOUT_ARM64  = 2
LAYOUT_ARM32  = 3

# ---------------------------------------------------------------------------
# Binary builder helpers
# ---------------------------------------------------------------------------

def tlv(type_id: int, payload: bytes) -> bytes:
    """Encode a single TLV field."""
    return struct.pack('<HH', type_id, len(payload)) + payload

def tlv_u8(type_id: int, value: int) -> bytes:
    return tlv(type_id, struct.pack('<B', value))

def tlv_u32(type_id: int, value: int) -> bytes:
    return tlv(type_id, struct.pack('<I', value))

def tlv_u64(type_id: int, value: int) -> bytes:
    return tlv(type_id, struct.pack('<Q', value))

def tlv_str(type_id: int, value: str) -> bytes:
    encoded = value.encode('utf-8')
    return tlv(type_id, encoded)

def tlv_u64_array(type_id: int, values: list) -> bytes:
    payload = struct.pack(f'<{len(values)}Q', *values)
    return tlv(type_id, payload)

def tlv_end() -> bytes:
    return struct.pack('<HH', TLV_RECORD_END, 0)

def build_record(
    status: int = STATUS_COMPLETE,
    sequence: int = 0,
    timestamp_ns: int = 1704067200000000000,
    tag_id: int = 0xDEADBEEFCAFEBABE,
    file: str = 'test.cpp',
    line: int = 42,
    function: str = 'testFunc',
    message: str = 'test message',
    process_id: int = 0,
    thread_id: int = 0,
    fault_address: int = None,
    load_base_address: int = 0,
    aslr_offset: int = None,
    stack_frames: list = None,
    register_layout: int = None,
    registers: list = None,
    truncated: bool = False,
    extra_tlvs: bytes = b'',
) -> bytes:
    """Build a complete Flare record as bytes."""
    body  = tlv_u8( TLV_RECORD_STATUS,   status )
    body += tlv_u64( TLV_SEQUENCE_NUMBER, sequence )
    body += tlv_u64( TLV_TIMESTAMP_NS,    timestamp_ns )
    body += tlv_u64( TLV_TAG_ID,          tag_id )
    body += tlv_str( TLV_FILE_NAME,       file )
    body += tlv_u32( TLV_LINE_NUMBER,     line )
    body += tlv_str( TLV_FUNCTION_NAME,   function )

    if process_id:
        body += tlv_u32( TLV_PROCESS_ID, process_id )
    if thread_id:
        body += tlv_u32( TLV_THREAD_ID, thread_id )

    if fault_address is not None:
        body += tlv_u64( TLV_FAULT_ADDRESS, fault_address )
    if load_base_address:
        body += tlv_u64( TLV_LOAD_BASE_ADDRESS, load_base_address )
    if aslr_offset is not None:
        body += tlv_u64( TLV_ASLR_OFFSET, aslr_offset )
    if stack_frames:
        body += tlv_u64_array( TLV_STACK_FRAMES, stack_frames )
    if register_layout is not None and registers:
        body += tlv_u8( TLV_REGISTER_LAYOUT, register_layout )
        body += tlv_u64_array( TLV_CPU_REGISTERS, registers )

    body += tlv_str( TLV_MESSAGE_BYTES, message )
    if truncated:
        body += tlv_u8( TLV_MESSAGE_TRUNCATED, 1 )

    body += extra_tlvs
    body += tlv_end()

    total_size = 8 + 4 + len(body)  # magic + size field + body
    header = struct.pack('<Q', FLARE_MAGIC) + struct.pack('<I', total_size)
    return header + body


def write_flare(records: list) -> bytes:
    """Concatenate a list of records into a single .flare byte string."""
    return b''.join(records)


def run_reader(data: bytes, *args) -> subprocess.CompletedProcess:
    """Write data to a temp file and run flare_reader.py against it."""
    with tempfile.NamedTemporaryFile(suffix='.flare', delete=False) as f:
        f.write(data)
        tmp_path = f.name

    try:
        result = subprocess.run(
            [sys.executable, str(SCRIPT), tmp_path] + list(args),
            capture_output=True,
            text=True,
        )
        return result
    finally:
        Path(tmp_path).unlink(missing_ok=True)


# ---------------------------------------------------------------------------
# Import flare_reader as a module for unit-testing internal functions
# ---------------------------------------------------------------------------
import importlib.util
_spec = importlib.util.spec_from_file_location('flare_reader', SCRIPT)
_mod  = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(_mod)

parse_record      = _mod.parse_record
_aslr_offset      = _mod._aslr_offset
record_to_json    = _mod.record_to_json
FlareRecord       = _mod.FlareRecord
LAYOUT_UNKNOWN_PY = _mod.LAYOUT_UNKNOWN


# ===========================================================================
# Tests
# ===========================================================================

class TestBinaryParsing(unittest.TestCase):
    """Unit tests for parse_record() and related helpers."""

    def _parse_one(self, data: bytes):
        """Parse the first record from raw bytes."""
        result = parse_record(data, 0)
        self.assertIsNotNone(result)
        record, _ = result
        return record

    def test_basic_fields(self):
        data   = build_record(sequence=7, timestamp_ns=123456789, line=99, message='hello')
        record = self._parse_one(data)
        self.assertEqual(record.sequence, 7)
        self.assertEqual(record.timestamp_ns, 123456789)
        self.assertEqual(record.line, 99)
        self.assertEqual(record.message, 'hello')
        self.assertEqual(record.file, 'test.cpp')
        self.assertEqual(record.function, 'testFunc')

    def test_status_complete(self):
        record = self._parse_one(build_record(status=STATUS_COMPLETE))
        self.assertEqual(record.status, STATUS_COMPLETE)

    def test_status_truncated(self):
        record = self._parse_one(build_record(status=STATUS_TRUNCATED, truncated=True))
        self.assertEqual(record.status, STATUS_TRUNCATED)
        self.assertTrue(record.message_truncated)

    def test_process_and_thread_id(self):
        record = self._parse_one(build_record(process_id=1234, thread_id=5678))
        self.assertEqual(record.process_id, 1234)
        self.assertEqual(record.thread_id, 5678)

    def test_fault_address_present(self):
        record = self._parse_one(build_record(fault_address=0xDEAD0000))
        self.assertEqual(record.fault_address, 0xDEAD0000)

    def test_fault_address_absent(self):
        record = self._parse_one(build_record())
        self.assertIsNone(record.fault_address)

    def test_fault_address_zero(self):
        # a null-pointer dereference produces fault_address=0; TLV is present
        record = self._parse_one(build_record(fault_address=0))
        self.assertEqual(record.fault_address, 0)

    def test_load_base_address(self):
        record = self._parse_one(build_record(load_base_address=0x555555554000))
        self.assertEqual(record.load_base_address, 0x555555554000)

    def test_aslr_offset_present(self):
        record = self._parse_one(build_record(aslr_offset=0x555555554000))
        self.assertEqual(record.aslr_offset, 0x555555554000)

    def test_aslr_offset_absent(self):
        record = self._parse_one(build_record())
        self.assertIsNone(record.aslr_offset)

    def test_stack_frames(self):
        frames = [0x7fff00001000, 0x7fff00002000, 0x7fff00003000]
        record = self._parse_one(build_record(stack_frames=frames))
        self.assertEqual(record.stack_frames, frames)

    def test_stack_frames_absent(self):
        record = self._parse_one(build_record())
        self.assertEqual(record.stack_frames, [])

    def test_registers_x86_64(self):
        regs = list(range(18))  # 18 x86-64 registers
        record = self._parse_one(build_record(register_layout=LAYOUT_X86_64, registers=regs))
        self.assertEqual(record.register_layout, LAYOUT_X86_64)
        self.assertEqual(record.registers, regs)

    def test_registers_arm64(self):
        regs = list(range(34))  # 34 ARM64 registers
        record = self._parse_one(build_record(register_layout=LAYOUT_ARM64, registers=regs))
        self.assertEqual(record.register_layout, LAYOUT_ARM64)
        self.assertEqual(record.registers, regs)

    def test_registers_arm32(self):
        regs = list(range(17))  # 17 ARM32 registers
        record = self._parse_one(build_record(register_layout=LAYOUT_ARM32, registers=regs))
        self.assertEqual(record.register_layout, LAYOUT_ARM32)
        self.assertEqual(record.registers, regs)

    def test_registers_absent(self):
        record = self._parse_one(build_record())
        self.assertEqual(record.registers, [])
        self.assertEqual(record.register_layout, LAYOUT_UNKNOWN_PY)

    def test_unknown_tlv_skipped(self):
        # TLV type 200 is unknown - should be silently skipped
        extra = tlv(200, b'\x01\x02\x03\x04')
        record = self._parse_one(build_record(message='after unknown', extra_tlvs=extra))
        self.assertEqual(record.message, 'after unknown')

    def test_multiple_records(self):
        data = write_flare([
            build_record(sequence=0, message='first'),
            build_record(sequence=1, message='second'),
            build_record(sequence=2, message='third'),
        ])
        records = []
        offset  = 0
        while offset < len(data):
            result = parse_record(data, offset)
            if result is None:
                break
            rec, offset = result
            records.append(rec)
        self.assertEqual(len(records), 3)
        self.assertEqual([r.sequence for r in records], [0, 1, 2])
        self.assertEqual([r.message  for r in records], ['first', 'second', 'third'])

    def test_empty_data_returns_none(self):
        self.assertIsNone(parse_record(b'', 0))

    def test_bad_magic_returns_none(self):
        self.assertIsNone(parse_record(b'\x00' * 16, 0))

    def test_garbage_between_records_skipped(self):
        good = build_record(sequence=0, message='good')
        data = b'\xFF' * 32 + good
        result = parse_record(data, 32)
        self.assertIsNotNone(result)
        rec, _ = result
        self.assertEqual(rec.message, 'good')


class TestAslrOffsetHelper(unittest.TestCase):
    """Unit tests for _aslr_offset()."""

    def _make(self, aslr_offset=None, load_base=0):
        r = FlareRecord()
        r.aslr_offset       = aslr_offset
        r.load_base_address = load_base
        return r

    def test_explicit_aslr_offset_preferred(self):
        r = self._make(aslr_offset=0x1000, load_base=0x2000)
        self.assertEqual(_aslr_offset(r), 0x1000)

    def test_falls_back_to_load_base_when_aslr_absent(self):
        r = self._make(aslr_offset=None, load_base=0x555555554000)
        self.assertEqual(_aslr_offset(r), 0x555555554000)

    def test_zero_aslr_offset_used_not_load_base(self):
        # aslr_offset=0 is valid (non-PIE binary); should not fall back
        r = self._make(aslr_offset=0, load_base=0x555555554000)
        self.assertEqual(_aslr_offset(r), 0)

    def test_both_zero(self):
        r = self._make(aslr_offset=None, load_base=0)
        self.assertEqual(_aslr_offset(r), 0)


class TestJsonOutput(unittest.TestCase):
    """Tests for record_to_json()."""

    def _json_one(self, **kwargs):
        data   = build_record(**kwargs)
        result = parse_record(data, 0)
        self.assertIsNotNone(result)
        rec, _ = result
        return record_to_json(rec)

    def test_basic_fields_present(self):
        j = self._json_one(sequence=3, message='hello json', line=77)
        self.assertEqual(j['sequence'], 3)
        self.assertEqual(j['message'], 'hello json')
        self.assertEqual(j['line'], 77)

    def test_status_name(self):
        j = self._json_one(status=STATUS_COMPLETE)
        self.assertEqual(j['status'], 'Complete')

    def test_fault_address_hex_string(self):
        j = self._json_one(fault_address=0xDEAD0000CAFEBABE)
        self.assertEqual(j['fault_address'], '0xdeaD0000cafebabe'.lower()
                         .replace('dead0000cafebabe', 'dead0000cafebabe'))
        # just check it's a hex string starting with 0x
        self.assertTrue(j['fault_address'].startswith('0x'))
        self.assertEqual(int(j['fault_address'], 16), 0xDEAD0000CAFEBABE)

    def test_fault_address_none_when_absent(self):
        j = self._json_one()
        self.assertIsNone(j['fault_address'])

    def test_fault_address_zero_is_string_not_none(self):
        j = self._json_one(fault_address=0)
        self.assertIsNotNone(j['fault_address'])
        self.assertEqual(j['fault_address'], '0x0000000000000000')

    def test_aslr_offset_hex_string(self):
        j = self._json_one(aslr_offset=0x555555554000)
        self.assertIsNotNone(j['aslr_offset'])
        self.assertEqual(int(j['aslr_offset'], 16), 0x555555554000)

    def test_aslr_offset_none_when_absent(self):
        j = self._json_one()
        self.assertIsNone(j['aslr_offset'])

    def test_stack_frames_have_runtime_and_static(self):
        aslr   = 0x555555554000
        frames = [aslr + 0x1000, aslr + 0x2000]
        j = self._json_one(aslr_offset=aslr, stack_frames=frames)
        self.assertEqual(len(j['stack_frames']), 2)
        f0 = j['stack_frames'][0]
        self.assertIn('runtime', f0)
        self.assertIn('static',  f0)
        self.assertEqual(int(f0['runtime'], 16), frames[0])
        self.assertEqual(int(f0['static'],  16), 0x1000)

    def test_stack_frames_no_static_without_aslr(self):
        frames = [0xDEAD1000, 0xDEAD2000]
        j = self._json_one(stack_frames=frames)
        # aslr_offset absent and load_base_address=0, so no static field
        for f in j['stack_frames']:
            self.assertNotIn('static', f)

    def test_registers_x86_64_named(self):
        regs = list(range(18))
        j = self._json_one(register_layout=LAYOUT_X86_64, registers=regs)
        self.assertEqual(j['register_layout'], 'x86-64')
        names = [r['name'] for r in j['registers']]
        self.assertIn('rax', names)
        self.assertIn('rip', names)
        self.assertIn('rflags', names)

    def test_registers_arm64_named(self):
        regs = list(range(34))
        j = self._json_one(register_layout=LAYOUT_ARM64, registers=regs)
        self.assertEqual(j['register_layout'], 'ARM64')
        names = [r['name'] for r in j['registers']]
        self.assertIn('pc', names)
        self.assertIn('sp', names)
        self.assertIn('lr', names)

    def test_registers_arm32_named(self):
        regs = list(range(17))
        j = self._json_one(register_layout=LAYOUT_ARM32, registers=regs)
        self.assertEqual(j['register_layout'], 'ARM32')
        names = [r['name'] for r in j['registers']]
        self.assertIn('pc', names)
        self.assertIn('cpsr', names)

    def test_address_registers_get_static_annotation(self):
        aslr = 0x555555554000
        # rip is an address register for x86-64; set it above aslr
        rip_val = aslr + 0xABC
        regs = [0] * 18
        regs[16] = rip_val  # rip is index 16
        j = self._json_one(
            register_layout=LAYOUT_X86_64,
            registers=regs,
            aslr_offset=aslr,
        )
        rip_entry = next(r for r in j['registers'] if r['name'] == 'rip')
        self.assertIn('static', rip_entry)
        self.assertEqual(int(rip_entry['static'], 16), 0xABC)

    def test_non_address_registers_have_no_static(self):
        aslr = 0x555555554000
        regs = [aslr + i for i in range(18)]  # all above aslr
        j = self._json_one(register_layout=LAYOUT_X86_64, registers=regs, aslr_offset=aslr)
        rax_entry = next(r for r in j['registers'] if r['name'] == 'rax')
        self.assertNotIn('static', rax_entry)

    def test_tag_name_used_when_set(self):
        data   = build_record()
        result = parse_record(data, 0)
        rec, _ = result
        rec.tag_name = 'MyTag'
        j = record_to_json(rec)
        self.assertEqual(j['tag'], 'MyTag')

    def test_tag_id_hex_used_when_no_name(self):
        j = self._json_one(tag_id=0xABCD1234)
        self.assertTrue(j['tag'].startswith('0x'))
        self.assertEqual(int(j['tag'], 16), 0xABCD1234)

    def test_message_truncated_flag(self):
        j = self._json_one(truncated=True, status=STATUS_TRUNCATED)
        self.assertTrue(j['message_truncated'])

    def test_empty_registers_list(self):
        j = self._json_one()
        self.assertEqual(j['registers'], [])
        self.assertEqual(j['register_layout'], 'unknown')

    def test_empty_stack_frames_list(self):
        j = self._json_one()
        self.assertEqual(j['stack_frames'], [])


class TestCommandLineTextOutput(unittest.TestCase):
    """End-to-end tests running flare_reader.py as a subprocess, text format."""

    def test_single_record_text(self):
        data   = build_record(message='hello world', line=55, sequence=0)
        result = run_reader(data)
        self.assertEqual(result.returncode, 0)
        self.assertIn('hello world', result.stdout)
        self.assertIn('55',          result.stdout)
        self.assertIn('Complete',    result.stdout)
        self.assertIn('Total records: 1', result.stdout)

    def test_multiple_records_total_count(self):
        data = write_flare([build_record(sequence=i) for i in range(4)])
        result = run_reader(data)
        self.assertEqual(result.returncode, 0)
        self.assertIn('Total records: 4', result.stdout)

    def test_fault_address_shown(self):
        data   = build_record(fault_address=0)
        result = run_reader(data)
        self.assertEqual(result.returncode, 0)
        self.assertIn('Fault:', result.stdout)

    def test_no_fault_section_when_absent(self):
        data   = build_record()
        result = run_reader(data)
        self.assertEqual(result.returncode, 0)
        self.assertNotIn('Fault:', result.stdout)

    def test_load_base_shown(self):
        data   = build_record(load_base_address=0x555555554000)
        result = run_reader(data)
        self.assertEqual(result.returncode, 0)
        self.assertIn('Load base:', result.stdout)

    def test_aslr_shown(self):
        data   = build_record(aslr_offset=0x555555554000)
        result = run_reader(data)
        self.assertEqual(result.returncode, 0)
        self.assertIn('ASLR:', result.stdout)

    def test_stack_frames_shown_with_static(self):
        aslr   = 0x555555554000
        frames = [aslr + 0x1000, aslr + 0x2000]
        data   = build_record(aslr_offset=aslr, stack_frames=frames)
        result = run_reader(data)
        self.assertEqual(result.returncode, 0)
        self.assertIn('Stack:', result.stdout)
        self.assertIn('static', result.stdout)
        self.assertIn('0x0000000000001000', result.stdout)  # static addr of frame 0

    def test_stack_frames_no_static_without_aslr(self):
        frames = [0xDEAD1000, 0xDEAD2000]
        data   = build_record(stack_frames=frames)
        result = run_reader(data)
        self.assertEqual(result.returncode, 0)
        self.assertIn('Stack:', result.stdout)
        # no ASLR so no static address column
        self.assertNotIn('->  static', result.stdout)

    def test_registers_x86_64_shown(self):
        regs   = list(range(18))
        data   = build_record(register_layout=LAYOUT_X86_64, registers=regs)
        result = run_reader(data)
        self.assertEqual(result.returncode, 0)
        self.assertIn('Registers: x86-64', result.stdout)
        self.assertIn('rax',    result.stdout)
        self.assertIn('rip',    result.stdout)
        self.assertIn('rflags', result.stdout)

    def test_registers_arm64_shown(self):
        regs   = list(range(34))
        data   = build_record(register_layout=LAYOUT_ARM64, registers=regs)
        result = run_reader(data)
        self.assertEqual(result.returncode, 0)
        self.assertIn('Registers: ARM64', result.stdout)
        self.assertIn('pc',     result.stdout)
        self.assertIn('pstate', result.stdout)

    def test_registers_arm32_shown(self):
        regs   = list(range(17))
        data   = build_record(register_layout=LAYOUT_ARM32, registers=regs)
        result = run_reader(data)
        self.assertEqual(result.returncode, 0)
        self.assertIn('Registers: ARM32', result.stdout)
        self.assertIn('cpsr', result.stdout)

    def test_truncated_flag_shown(self):
        data   = build_record(truncated=True, status=STATUS_TRUNCATED)
        result = run_reader(data)
        self.assertEqual(result.returncode, 0)
        self.assertIn('[TRUNCATED]', result.stdout)

    def test_process_and_thread_shown(self):
        data   = build_record(process_id=9999, thread_id=8888)
        result = run_reader(data)
        self.assertEqual(result.returncode, 0)
        self.assertIn('9999', result.stdout)
        self.assertIn('8888', result.stdout)

    def test_tag_name_from_dict(self):
        import os
        tag_id = 0xABCD1234ABCD1234
        data   = build_record(tag_id=tag_id)
        with tempfile.NamedTemporaryFile(
            mode='w', suffix='.tags', delete=False
        ) as df:
            df.write(f'MyTestTag, 0x{tag_id:016x}\n')
            dict_path = df.name
        try:
            result = run_reader(data, '--dict', dict_path)
            self.assertEqual(result.returncode, 0)
            self.assertIn('MyTestTag', result.stdout)
        finally:
            os.unlink(dict_path)

    def test_nonexistent_file_exits_nonzero(self):
        result = subprocess.run(
            [sys.executable, str(SCRIPT), '/nonexistent/path/crash.flare'],
            capture_output=True, text=True,
        )
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('not found', result.stderr.lower())

    def test_empty_file_zero_records(self):
        result = run_reader(b'')
        self.assertEqual(result.returncode, 0)
        self.assertIn('Total records: 0', result.stdout)

    def test_garbage_data_zero_records(self):
        result = run_reader(b'\x00\xFF\xAB\xCD' * 16)
        self.assertEqual(result.returncode, 0)
        self.assertIn('Total records: 0', result.stdout)


class TestCommandLineJsonOutput(unittest.TestCase):
    """End-to-end tests running flare_reader.py with --format json."""

    def _json_output(self, data: bytes, extra_args=None) -> dict:
        args   = ['--format', 'json'] + (extra_args or [])
        result = run_reader(data, *args)
        self.assertEqual(result.returncode, 0, msg=result.stderr)
        return json.loads(result.stdout)

    def test_total_records_key(self):
        data = write_flare([build_record(sequence=i) for i in range(3)])
        out  = self._json_output(data)
        self.assertEqual(out['total_records'], 3)
        self.assertEqual(len(out['records']), 3)

    def test_crash_context_in_json(self):
        aslr   = 0x555555554000
        frames = [aslr + 0x100, aslr + 0x200]
        regs   = list(range(18))
        data   = build_record(
            fault_address=0,
            load_base_address=aslr,
            aslr_offset=aslr,
            stack_frames=frames,
            register_layout=LAYOUT_X86_64,
            registers=regs,
        )
        out = self._json_output(data)
        rec = out['records'][0]

        self.assertEqual(rec['fault_address'],     '0x0000000000000000')
        self.assertIsNotNone(rec['load_base_address'])
        self.assertIsNotNone(rec['aslr_offset'])
        self.assertEqual(len(rec['stack_frames']),  2)
        self.assertIn('static', rec['stack_frames'][0])
        self.assertEqual(rec['register_layout'],    'x86-64')
        self.assertEqual(len(rec['registers']),     18)

    def test_empty_file_json(self):
        out = self._json_output(b'')
        self.assertEqual(out['total_records'], 0)
        self.assertEqual(out['records'], [])

    def test_sequence_ordering_in_json(self):
        data = write_flare([build_record(sequence=i) for i in range(5)])
        out  = self._json_output(data)
        seqs = [r['sequence'] for r in out['records']]
        self.assertEqual(seqs, list(range(5)))

    def test_message_truncated_in_json(self):
        data = build_record(truncated=True, status=STATUS_TRUNCATED)
        out  = self._json_output(data)
        self.assertTrue(out['records'][0]['message_truncated'])


class TestCrashContextIntegration(unittest.TestCase):
    """
    Integration tests combining all crash-context TLVs in a single record,
    verifying both the parsed representation and the rendered output.
    """

    ASLR   = 0x555555554000
    FRAMES = [0x555555554000 + 0xABC, 0x555555554000 + 0x1234]
    REGS   = list(range(18))  # x86-64

    def _build(self):
        return build_record(
            message       = 'SIGSEGV',
            fault_address = 0,                      # null-pointer dereference
            load_base_address = self.ASLR,
            aslr_offset   = self.ASLR,
            stack_frames  = self.FRAMES,
            register_layout = LAYOUT_X86_64,
            registers     = self.REGS,
            process_id    = 1234,
            thread_id     = 5678,
        )

    def test_parse_all_crash_fields(self):
        result = parse_record(self._build(), 0)
        self.assertIsNotNone(result)
        rec, _ = result

        self.assertEqual(rec.message,           'SIGSEGV')
        self.assertEqual(rec.fault_address,     0)
        self.assertEqual(rec.load_base_address, self.ASLR)
        self.assertEqual(rec.aslr_offset,       self.ASLR)
        self.assertEqual(rec.stack_frames,      self.FRAMES)
        self.assertEqual(rec.register_layout,   LAYOUT_X86_64)
        self.assertEqual(rec.registers,         self.REGS)
        self.assertEqual(rec.process_id,        1234)
        self.assertEqual(rec.thread_id,         5678)

    def test_static_address_calculation(self):
        result = parse_record(self._build(), 0)
        rec, _ = result
        aslr   = _aslr_offset(rec)
        static_addrs = [f - aslr for f in rec.stack_frames]
        self.assertEqual(static_addrs, [0xABC, 0x1234])

    def test_text_output_shows_all_sections(self):
        result = run_reader(self._build())
        self.assertEqual(result.returncode, 0)
        out = result.stdout
        self.assertIn('SIGSEGV',        out)
        self.assertIn('Fault:',         out)
        self.assertIn('Load base:',     out)
        self.assertIn('ASLR:',          out)
        self.assertIn('Stack:',         out)
        self.assertIn('static',         out)
        self.assertIn('Registers: x86-64', out)
        self.assertIn('rip',            out)

    def test_json_output_crash_record(self):
        import subprocess as sp
        with tempfile.NamedTemporaryFile(suffix='.flare', delete=False) as f:
            f.write(self._build())
            tmp = f.name
        try:
            r = sp.run(
                [sys.executable, str(SCRIPT), tmp, '--format', 'json'],
                capture_output=True, text=True,
            )
            self.assertEqual(r.returncode, 0)
            out = json.loads(r.stdout)
            rec = out['records'][0]
            self.assertEqual(rec['fault_address'],  '0x0000000000000000')
            self.assertIsNotNone(rec['aslr_offset'])
            self.assertEqual(len(rec['stack_frames']),  2)
            self.assertEqual(rec['stack_frames'][0]['static'], '0x0000000000000abc')
            self.assertEqual(rec['register_layout'], 'x86-64')
            self.assertEqual(len(rec['registers']),  18)
        finally:
            Path(tmp).unlink(missing_ok=True)


if __name__ == '__main__':
    unittest.main(verbosity=2)
