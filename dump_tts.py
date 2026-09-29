"""Capture COM4 and rebuild cloud TTS MP3 files from ###TTS hex dump lines.

Usage:
  python dump_tts.py [outdir] [seconds]
  Hold BOOT and speak; each reply becomes tts_001.mp3, tts_002.mp3, ...
"""
from __future__ import annotations

import re
import sys
import time
from pathlib import Path

import serial

PORT = "COM4"
BAUD = 115200
OUTDIR = Path(sys.argv[1] if len(sys.argv) > 1 else r"D:\esp32\1003\tts_dumps")
SECS = int(sys.argv[2] if len(sys.argv) > 2 else 180)

ANSI = re.compile(r"\x1b\[[0-9;]*m")
BEGIN = re.compile(r"###TTS_BEGIN")
END = re.compile(r"###TTS_END\s+bytes=(\d+)")
ABORT = re.compile(r"###TTS_ABORT")
LINE = re.compile(r"###TTS\s+(\d+)\s+([0-9A-Fa-f]+)")

OUTDIR.mkdir(parents=True, exist_ok=True)
ser = serial.Serial(PORT, BAUD, timeout=0.2)
ser.reset_input_buffer()

print(f"Dumping TTS from {PORT} -> {OUTDIR} ({SECS}s). Hold BOOT and speak...", flush=True)

buf = bytearray()
active = False
expected = None
n_file = 0
end = time.time() + SECS

while time.time() < end:
    data = ser.read(4096)
    if not data:
        continue
    for raw in data.decode("utf-8", errors="replace").splitlines():
        line = ANSI.sub("", raw)
        if BEGIN.search(line):
            buf = bytearray()
            active = True
            expected = None
            print("TTS begin", flush=True)
            continue
        if ABORT.search(line):
            active = False
            buf = bytearray()
            print("TTS abort", flush=True)
            continue
        m_end = END.search(line)
        if m_end and active:
            expected = int(m_end.group(1))
            n_file += 1
            path = OUTDIR / f"tts_{n_file:03d}.mp3"
            path.write_bytes(buf)
            ok = len(buf) == expected
            print(f"Wrote {path} ({len(buf)} B, expected {expected}) {'OK' if ok else 'MISMATCH'}", flush=True)
            if buf[:2] == b"\xff\xfb" or buf[:2] == b"\xff\xf3" or buf[:3] == b"ID3":
                print(f"  header OK: {buf[:4].hex()}", flush=True)
            active = False
            continue
        m = LINE.search(line)
        if m and active:
            off = int(m.group(1))
            hx = re.sub(r"[^0-9A-Fa-f]", "", m.group(2))
            if len(hx) % 2:
                hx = hx[:-1]  # truncated log line — drop orphan nibble
            if not hx:
                continue
            chunk = bytes.fromhex(hx)
            if off == len(buf):
                buf.extend(chunk)
            elif off > len(buf):
                buf.extend(b"\x00" * (off - len(buf)))
                buf.extend(chunk)
            else:
                # overwrite / fill gap from overlapping lines
                end_off = off + len(chunk)
                if end_off > len(buf):
                    buf.extend(b"\x00" * (end_off - len(buf)))
                buf[off:end_off] = chunk

ser.close()
print(f"Done. files={n_file}", flush=True)
