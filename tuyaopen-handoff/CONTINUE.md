# Leffberg AI Speaker — продовження на іншому ПК

## Стан (2026-09-22)

- **Плата:** ESP32-S3-N16R8 (підтверджено на модулі)
- **COM:** раніше був `COM4` (CH343), правий USB‑C порт
- **Проєкт SDK:** [TuyaOpen](https://github.com/tuya/TuyaOpen) → app `apps/tuya.ai/your_chat_bot`
- **Board config:** `ESP32S3_BREAD_COMPACT_WIFI`
- **PID:** `19j7q577p8ljcajg`
- **UUID / AuthKey:** НЕ в цьому репо (публічне). Візьміть з Excel ліцензій Tuya або з локального файлу `secrets.local.md` (не комітити).
- **Збірка на цьому ПК:** не завершена — Windows WDAC блокує pip-`cmake`; є fallback-патч під Espressif cmake.

## Ліцензії Tuya (2 шт.)

Файл: `19j7q577p8ljcajg-*-2-*.xlsx` з Tuya Platform.

| # | UUID | AuthKey |
|---|------|---------|
| 1 | `uuid81371c803243664e` | (з Excel, колонка key) |
| 2 | `uuida64f5d8fccb769e1` | (з Excel, колонка key) |

У прошивку зараз планувалась **ліцензія #1**.

## Hardware (v1, без OLED)

```
Mic MS3526/INMP441:
  VDD→3V3  GND→GND  SCK→GPIO5  WS→GPIO4  SD→GPIO6  L/R→GND

MAX98357A:
  Vin→5Vin  GND→GND  BCLK→GPIO15  LRC→GPIO16  DIN→GPIO7
  SD→3V3  GAIN→GND

Speaker → amp OUT+/OUT−
Talk button → BOOT (GPIO0) на платі
Status LED → вбудований RGB GPIO48
```

Можна спочатку прошити **лише плату** (без peri) і спарити з Tuya App; голос запрацює після підключення mic/amp.

## Wake word

У `your_chat_bot` є режими `ASR_WAKEUP_SINGLE` / `ASR_WAKEUP_FREE` і слова на кшталт «你好涂鸦» / «你好小智». Увімкнути через `tos.py config menu` після першого успішного білду.

## Сетап на новому ПК (Windows)

```powershell
# 1) Клонувати TuyaOpen
git clone --recursive https://github.com/tuya/TuyaOpen.git D:\esp32\TuyaOpen
cd D:\esp32\TuyaOpen
. .\export.ps1
tos.py check

# 2) Застосувати патч cmake (якщо pip cmake блокується WDAC)
git apply path\to\tuyaopen-handoff\patches\cli_prepare_cmake_fallback.patch
# Потрібен встановлений Espressif cmake, напр.:
#   C:\Espressif\tools\cmake\3.30.2\bin\cmake.exe

# 3) Конфіг плати + PID
cd apps\tuya.ai\your_chat_bot
copy path\to\tuyaopen-handoff\config\ESP32S3_BREAD_COMPACT_WIFI.config config\
tos.py config choice -c ESP32S3_BREAD_COMPACT_WIFI.config

# 4) Вписати UUID/AuthKey у include\tuya_config.h
#    (зразковий файл: tuyaopen-handoff\config\tuya_config.h.example)

# 5) Збірка і прошивка
tos.py build
tos.py flash --port COMx
tos.py monitor --port COMx
```

## Спарювання

1. Smart Life / Tuya App → Add device → Auto scan (2.4 GHz Wi‑Fi)
2. Або 3× швидкий power-cycle для режиму pairing (якщо вже було прив’язано)

## Що ще в цьому handoff

- `config/ESP32S3_BREAD_COMPACT_WIFI.config` — board + PID
- `config/tuya_config.h.example` — шаблон без секретів
- `patches/cli_prepare_cmake_fallback.patch` — fallback на Espressif cmake
