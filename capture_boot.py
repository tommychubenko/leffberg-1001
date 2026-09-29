import serial, time, re, sys
port = "COM4"
log_path = r"D:\esp32\1003\com4_boot.log"
patterns = re.compile(r"alert|dingdong|mem data|launch|MAX98357|MQTT|tts stream|SPEAK|play alert|Network|volume|I2S|conn|mute|eof|FG", re.I)
ser = serial.Serial(port, 115200, timeout=0.2)
# hard reset via RTS
ser.setDTR(False)
ser.setRTS(True)
time.sleep(0.1)
ser.setRTS(False)
ser.reset_input_buffer()
end = time.time() + 50
hits = []
raw = []
with open(log_path, "w", encoding="utf-8", errors="replace") as f:
    while time.time() < end:
        data = ser.read(4096)
        if not data:
            continue
        text = data.decode("utf-8", errors="replace")
        f.write(text)
        f.flush()
        raw.append(text)
        for line in text.splitlines():
            if patterns.search(line):
                hits.append(line)
ser.close()
print(f"HITS={len(hits)}")
for h in hits[:100]:
    print(h)
print("---END---")
