"""COM4 monitor focused on mic / ASR / chat state. noreset by default."""
import re
import sys
import time

import serial

port = "COM4"
log_path = sys.argv[1] if len(sys.argv) > 1 else r"C:\Users\user\AppData\Local\Temp\com4_mic.log"
secs = int(sys.argv[2]) if len(sys.argv) > 2 else 120
do_reset = len(sys.argv) > 3 and sys.argv[3] == "reset"

pat = re.compile(
    r"mic loopback|asr|ASR|vad|VAD|LISTEN|SPEAK|HOLD|button|BOOT|key|"
    r"upload|audio.*send|nlg|NLG|text ->|content:|"
    r"I2S0|I2S RX|sample_rate|chat_bot|ai agent|"
    r"downstream|tts stream|eof|error|Guru|abort",
    re.I,
)
ansi = re.compile(r"\x1b\[[0-9;]*m")

ser = serial.Serial(port, 115200, timeout=0.2)
if do_reset:
    ser.setDTR(False)
    ser.setRTS(True)
    time.sleep(0.1)
    ser.setRTS(False)
ser.reset_input_buffer()

print(f"MONITOR COM4 {secs}s log={log_path} reset={do_reset}", flush=True)
print("Hold BOOT and speak now...", flush=True)

end = time.time() + secs
hits = 0
with open(log_path, "w", encoding="utf-8", errors="replace") as f:
    while time.time() < end:
        data = ser.read(4096)
        if not data:
            continue
        text = data.decode("utf-8", errors="replace")
        f.write(text)
        f.flush()
        for line in text.splitlines():
            clean = ansi.sub("", line)
            if pat.search(clean):
                hits += 1
                print(clean, flush=True)

ser.close()
print(f"DONE hits={hits}", flush=True)
