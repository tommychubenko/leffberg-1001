# Leffberg AI Speaker (ESP32-S3 + TuyaOpen)

Документ фіксує ідею проєкту, зроблено, результати, висновки й поточний стан (оновлено **2026-09-29**).

---

## ГОЛОВНЕ на зараз (2026-09-29, вечір) — читати першим

1. **MVP голосу працює** (підтверджено користувачем ~22:10): BOOT → ASR → TTS **чути рівно**, без скрипу і без ривків; ding-dong теж. Цифровий шлях був доведений 24-го; живий звук з’явився після пайки **+/−** і **GAIN 3V3 (6 dB)**.
2. **Скрип** = занадто великий GAIN (12 dB на GND). **Ривки** = дві програмні причини, обидві виправлені: UART-дамп TTS гальмував feed (~425 мс dump на 216 мс звуку) + у `svc_ai_player` не було jitter-буфера (старт з першого байта, DMA ~90 мс). Pre-buffer **3 КБ / 1.5 с**.
3. **Не чіпати** без нової скарги: I2S 16-bit Philips, MP3 reservoir/frame-skip, GAIN 3V3, SD GPIO17.
4. **Далі — продукт, не «чи є звук»:** закомітити uncommitted TuyaOpen (`svc_ai_player`, prebuf, макроси=0); «чиста» прошивка (floor volume, connect-alert); DP 206; корпус/BOM/wake word за бажанням.
5. Діагностика (silence/selftest/loopback/TTS dump) **вже вимкнена** в поточній збірці на платі.

Деталі фіксів ривків — блок нижче в цьому розділі; ніч 24-го — «Тести 2026-09-24».

**Оновлення 2026-09-29 (пізно) — ривки: дві причини знайдено в коді, обидві виправлено, прошито:**

- **`ENABLE_TTS_UART_DUMP`** робив hex‑дамп кожного TTS‑chunk (~864 B → 54 рядки × ~90 символів ≈ 4.9 КБ по UART 115200 ≈ **425 мс**) у потоці прийому **до** `tuya_ai_player_feed`, тоді як сам chunk = 216 мс звуку. Подача в плеєр була вдвічі повільніша за відтворення → ривки гарантовано. Вимкнено разом з іншою діагностикою.
- **У `svc_ai_player.c` не було jitter‑буфера**: декодування починалось з першого байта в ring‑буфері; кожен мережевий chunk відігравався і плеєр чекав наступного (DMA I2S тримає лише 90 мс). Додано pre‑buffer для `AI_PLAYER_SRC_MEM`: старт після **3 КБ** (~0.75 с при 32 kbps) або EOF, або 1.5 с таймауту. Лог: `ai player FG prebuffer ready: N B queued after M ms`.
- Новий детектор: `I2S16 play: stall N ms between chunks (DMA underrun -> dropout)` — якщо між викликами play() пауза 100–500 мс. **Немає `stall` у лозі = немає дірок у PCM.** Якщо ривки лишаються без `stall` — це вже не потік, а живлення/динамік.
- Побічно: `tos.py build` зависав, бо питав про оновлення `platform/ESP32` до коміту з yaml (інший коміт `a934eed`). Створено маркер `D:\esp32\TuyaOpen\.cache\.dont_prompt_update_platform` → більше не питає.

Деталі логів TTS 2026-09-24 — у розділі «Тести 2026-09-24 (ніч)» нижче.

---

## Нотатки наступному агенту (2026-09-29, після фіксу)

Аудіо-MVP **закрито**. Не відкривати знову «empty TTS», «32-bit I2S», «бітрейт 32k», «GAIN на GND».

### Що вважати готовим

| Шар | Стан |
|-----|------|
| Mic → ASR → NLG | Працює |
| Хмара → MP3 16 kHz ~32 kbps | Працює (COM4 + `tts_dumps`) |
| minimp3 + reservoir/frame-skip | Працює |
| I2S Philips 16-bit L=R | Працює |
| Amp MAX98357, GAIN 3V3, SD GPIO17, динамік паяний | Працює |
| Рівний TTS (без ривків) | Працює після вимкнення dump + pre-buffer |

### Не чіпати

I2S формат, MP3-фікси, GAIN 3V3, UUID у git. Pre-buffer (`AI_PLAYER_STREAM_PREBUF_BYTES` 3072) — лишити, поки звук рівний.

### Наступний етап (продукт)

1. Закомітити TuyaOpen поверх `a0f1a516`: `svc_ai_player.c/.h`, `ai_player.h`, макроси діагностики=0, stall-лог. Оновити `patches/`. `tuya_config.h` з ключами — **ні**. Platform I2S уже в `a934eed` (detached HEAD — оформити гілку, якщо треба push).
2. «Чиста» прошивка: прибрати floor `volume<50→50`; обрати один connect-sound (PCM chime vs MP3 dingdong); `default_vol` як у продукті.
3. Платформа Tuya: DP **206** Delete, якщо не BT.
4. Залізо на потім: конденсатори на Vin, коротші I2S, корпус, BOM, wake word.
5. Якщо знову ривки — спочатку лог `stall` / `prebuffer ready`, не зміна бітності I2S.

Критерій «можна продавати як демо»: тиша в простої, ding-dong, BOOT → рівна відповідь, пара Smart Life жива.

---

## Ідея проєкту

Зібрати **розумну AI‑колонку** на базі плати **ESP32‑S3‑N16R8**:

- мікрофон (I2S) → хмара Tuya AI → відповідь голосом через підсилювач **MAX98357A** і динамік;
- керування кнопкою **BOOT** (push‑to‑talk);
- пара з додатком **Smart Life**;
- білінг через **Subscription** (платить кінцевий користувач).

Замість самописної прошивки з нуля обрано офіційний SDK **[TuyaOpen](https://github.com/tuya/TuyaOpen)** і демо **`apps/tuya.ai/your_chat_bot`**.

---

## Залізо (актуальна розпіновка)

Плата: **ESP32‑S3‑N16R8**, USB‑C (**COM4** / CH343). Amp: типовий модуль **MAX98357A** (LRC · BCLK · DIN · GAIN · SD · GND · Vin; динамік на великих **+ / −**).

### MAX98357A → ESP32‑S3

| Amp | ESP | Примітка |
|-----|-----|----------|
| Vin | **5VIN** | |
| GND | GND | будь‑який |
| SD | **GPIO17** | mute, **не 3V3** |
| GAIN | **3V3** (зараз) | **6 dB**. GND=12 dB давав скрип; floating=9 dB без змін. Datasheet: GND=12, 100k→GND=15, floating=9, VDD=6, 100k→VDD=3 dB |
| DIN | **GPIO7** | без резистора |
| BCLK | **GPIO15** | **напряму** (без резистора) |
| LRC | **GPIO16** | без резистора |

Динамік: великі **+** і **−** на amp, не на піни хедера.

> Раніше на BCLK був ~20 Ω. Після зняття резистора зник «писк у простої», але й корисний звук зникав, якщо контакт BCLK поганий. Тримаємо **прямий** BCLK і надійний джампер на GPIO15. **SD раніше був на 3V3** → постійний писк; перенесено на GPIO17.

### Мікрофон (INMP441 / MS3526) → I2S0

| Mic | ESP |
|-----|-----|
| VDD | 3V3 |
| GND | GND |
| SCK | **GPIO5** |
| WS | **GPIO4** |
| SD | **GPIO6** |
| L/R | GND |

### Інше

| Функція | Пін |
|---------|-----|
| Talk | кнопка **BOOT** (GPIO0), режим hold |
| Статус | вбудований RGB **GPIO48** |

> **SD = GPIO17.** У простої прошивка тримає його LOW. HIGH лише на час play / SPEAK.

---

## Ідентифікатори

| Що | Значення |
|----|----------|
| PID | `19j7q577p8ljcajg` (Leffberg Smart AI Speaker) |
| Device ID | `bfeca085931a5a8756aipw` |
| Agent | `aipt_fz0eo7xwkw` (Leffberg AI agent), live **v1.0.2**, Central Europe |
| Region | Central Europe Data Center |

### DP продукту (з карти / xlsx)

| DP | Code | Навіщо нам |
|----|------|------------|
| 203 | `voice_vol` | гучність — **так** |
| 204 | `voice_mic` | мікрофон — **так** |
| 205 | `voice_play` | TTS play — **так** |
| 206 | `voice_bt_play` | BT audio — **ні** (S3 без A2DP) |
| 207 / 208 / 201 / 202 | alarm, group, IR | для MVP **не потрібні** |

У Product Function Definition **немає поля default** для bool/value — початкові значення дає **report з пристрою**. DP 206 краще Delete, якщо продукт у статусі **Developing**.

---

## Що зроблено (прошивка / софт)

### Базове
- TuyaOpen `your_chat_bot`, board `ESP32S3_BREAD_COMPACT_WIFI`.
- OLED вимкнено; чат **AI_CHAT_MODE_HOLD**.
- Flash додатка з `0x10000` (`esptool`), без wipe пари Smart Life (коли можливо).
- **SD‑mute** на GPIO17.

### Оновлення 2026-09-24 (день)

1. DP defaults: `voice_vol` **203 = 50**, mic/play ON; `default_vol = 50`.
2. Connect-alert тимчасово = MP3 **dingdong** (`media_src_dingdong`, 3537 B).
3. Спроби з I2S TX **32‑біт** stereo + PSRAM bounce для TTS‑чанок.

### Оновлення 2026-09-24 (вечір — локальний звук)

1. I2S TX (I2S1 → MAX98357) повернуто / зафіксовано як **Philips 16‑біт stereo L=R** (`tkl_i2s.c` + mono→stereo int16 у `tdd_audio_no_codec.c`). **32‑біт expand на слух давав лише SD‑gated Class‑D писк**, без корисного тону.
2. Play чанками в **internal RAM** (не один великий PSRAM‑буфер під DMA/write).
3. Старт‑звук: обхід MP3 — PCM «дін‑дон» (880→523 Hz) через `tdl_audio_play` у `ai_audio_player.c` (MP3 dingdong на слух **не** виходить).

Файли: `tuya_main.c`, `app_chat_bot.c`, `tdd_audio_no_codec.c`, `tkl_i2s.c`, `ai_audio_player.c`.

### Оновлення 2026-09-24 (ніч — діагностика по логах, прошивка на платі)

**Виправлено в прошивці (TuyaOpen, uncommitted):**

1. **Старт‑chime грав одну ноту** — у логу був `AUDIO DEBUG B done rt=-3` (= `OPRT_MALLOC_FAILED`): кожна нота `tal_malloc`'ила 14–18 КБ *internal* RAM, а вільно було ~32 КБ фрагментованих. Тепер синтез іде чанками по 20 мс на стеку (`__pcm_play_note`), обидві ноти → `rt=0`. Ноти 659→523 Hz, спад до −20 dB.
2. **`decoder_mp3.c`**: коли кадр розпарсено, але PCM нема (bit reservoir ще порожній) — раніше `return 0` викидав **увесь** framebuf (до 4 КБ ≈ 1 с аудіо). Тепер пропускає лише цей кадр.
3. **`minimp3.h` → `L3_restore_reservoir`**: Tuya‑патч робив early return без копіювання main data → резервуар не наповнювався, наступні кадри теж падали. Повернуто поведінку upstream.
4. Діагностичні логи: `I2S16 play samples= vol= peak= rms= zc_pct=` (статистика PCM, що йде в I2S; бюджет 60 рядків на «висловлювання»), `tts stream start/first chunk/stop: N B in M chunks`, `mp3 stream: Hz/ch/kbps`, `ai agent -> downstream audio attr`, **`mic loopback`** (що чує INMP441, коли SD=HIGH: dc / ac_rms / zc_pct / частка енергії на 659, 523 Hz та ×2, ×3).
5. Boot‑діагностика під макросами в `ai_audio_player.c` (обидва = 1, вимкнути → 0): `ENABLE_AUDIO_SILENCE_TEST` (2 с SD=HIGH + нулі — **має бути тихо**), `ENABLE_AUDIO_MP3_SELFTEST` (MP3 dingdong через плеєр після chime). У `tdd_audio_no_codec.c`: `ENABLE_MIC_LOOPBACK_STATS`.

**Що довели логи (COM4, `%TEMP%\com4_*.log`):**

| Ланка | Факт з логу | Висновок |
|------|-------------|----------|
| PCM chime → I2S | 659 Hz: `peak 18231→1969`, `zc_pct=8`; 523 Hz: `peak 18512→2093`, `zc_pct=6`, усі `rt=0` | у I2S йдуть чисті затухаючі синуси |
| MP3 dingdong → декодер → I2S | `mp3 stream: 16000 Hz, 1 ch, 40 kbps`; 24 кадри × 576 = 13 824 семпли; `peak 24→9397→11212→1793`, `zc_pct 6` | minimp3 + плеєр декодують коректно |
| **Хмарний TTS** | `downstream audio attr: codec=109 rate=16000 ch=1`; `tts first chunk 864 B: FF F3 48 C4`; `tts stream stop: 4464 B in 6 chunks`; `mp3 stream: 16000 Hz, 1 ch, 32 kbps`; у I2S 17 856 семплів (=31 кадр), `peak=22934 rms=8744 zc_pct=15`, `peak=18674 zc_pct=13` | **MP3 з хмари приходить, декодується в мову** (шум дав би zc ≈ 50 %). Стара теза «start→stop без DATA ⇒ байтів нема» була хибна — рядок DATA у коді був закоментований |
| Швидкість I2S | 17 856 семплів відіграно за ~1 с (8–9 100‑мс вікон мікрофона) | тактування справді 16 kHz |
| **Мікрофон під час chime** | `tone_pct 659=0 523=0`, і на ×2/×3 теж 0; при цьому ac_rms 500–2300 | динамік **не випромінює** частоти, які отримує |
| **Мікрофон при SD=HIGH + нулі** | `ac_rms≈500 zc_pct=17` стабільно (≈1.3–1.4 kHz), після mute → 60–270 | підсилювач **писчить сам**, без даних |

**Підсумок:** увесь цифровий шлях (хмара → MP3 → minimp3 → плеєр → `tdl_audio_play` → I2S DMA) працює і віддає правильні дані з правильною швидкістю. Писк народжується **між пінами ESP32 і динаміком** — це залізо: проводка I2S, живлення/земля або сам модуль. Справний MAX98357A з валідними BCLK/LRCLK і DIN=0 мовчить абсолютно.

---

## Тести локального звуку (2026-09-24 вечір)

Мета: зрозуміти, чому замість нормального connect‑звуку чути писк / тишу. SD на GPIO17 підтверджено.

| # | Тест | Що робили | Результат на слух | Висновок |
|---|------|-----------|-------------------|----------|
| 1 | Rhythm tone + SD mute | Синус у прошивці, SD HIGH на тон / LOW на паузу | **Біп… тиша… біп** | Amp + SD GPIO17 + BCLK живі (ритм mute) |
| 2 | PCM sine loop через `tdl_audio_play` | 880 Hz, 0.3 с / ~1 с пауза; спочатку 32‑біт I2S | Лише писк/шипіння з паузами | Unmute є, **формат 32‑біт** не дає чистого PCM |
| 3 | Те саме після **16‑біт Philips stereo** | Той самий sine через `tdl_audio_play` | **Чистий тон 880 Hz** з паузами | **I2S 16‑біт + PCM path OK** (DIN/динамік ок) |
| 4 | MP3 dingdong loop | `ai_audio_play_data(MP3, media_src_dingdong)` цикл ~2 с play / 1 с пауза | **Писк ~2 с — пауза 1 с — писк…** (не melodia dingdong) | Софт грає (`len 3537`→EOF на COM), але **декод/шлях MP3** ≠ чутний кліп |
| 5 | Debug seq | A: 880 Hz ×3; B: PCM ding‑dong; C: heartbeat 880 Hz / 4 с | Heartbeat чути (писк—4 с—писк); COM: `I2S16 … peak=28000 rt=0` | PCM‑шлях стабільно підтверджений логами |
| 6 | Один PCM «дін‑дон» при старті | 880 Hz ~0.45 с → пауза → 523 Hz ~0.55 с | На слух: **один писк і тиша** | **Проблема не вирішена**: замість розпізнаваного ding‑dong — знову один писк |

### Що з цього випливає

- **Залізо відтворення (PCM → I2S 16‑біт → MAX98357) працює**, коли шлемо сирий int16 sine.
- **MP3 `media_src_dingdong`**: у логу є `launch alert=dingdong` / `mem data len 3537` / EOF, але на слух **не ding‑dong**, а писк на час play (схоже на unmute + поганий/порожній PCM після декоду).
- Спроба замінити старт на **PCM two‑tone** поки що на слух теж зводиться до **одного писку** — окремо докрутити (огинаюча, дві явні ноти, гучність) **або** чинити MP3‑декодер/плеєр.
- Ритм «писк / пауза» без мелодії часто = **лише SD unmute** (Class‑D шум), а не доказ контенту; доказ контенту — **чистий тон** (тест 3) або COM `peak≠0` + впізнавана мелодія.

> **Переглянуто вночі 2026‑09‑24:** висновки цієї таблиці, зроблені «на слух», частково хибні. Тест 6 («один писк») мав програмну причину (`rt=-3`, malloc). Тести 1/3/5 («чистий тон», «біп—тиша—біп») не доводять справність аналогового шляху — мікрофон показав, що підсилювач видає тон і на цифровій тиші. Актуальні висновки — у розділі нижче.

---

## Тести 2026-09-24 (ніч) — по логах COM4, а не на слух

Методика: кожна збірка → `tos.py build` → `esptool write_flash 0xd000 ota_data_initial.bin 0x10000 your_chat_bot.bin` (пара Smart Life не збивається) → `python %TEMP%\cap_com4.py <log> <сек>` (RTS‑reset + захоплення; `noreset` — без перезапуску, для TTS). Усі логи лежать у `%TEMP%\com4_*.log`.

| # | Тест | Як | Результат у лозі | Висновок |
|---|------|----|------------------|----------|
| N1 | Boot‑лог старої прошивки | `com4_boot.log` | `AUDIO DEBUG B done rt=-3`; `Free heap 32559` | **Chime падав на `tal_malloc`** (OPRT_MALLOC_FAILED) — internal RAM ~32 КБ фрагментованої. Тому «одна нота» |
| N2 | Chime після фіксу (чанки 20 мс на стеку) | `com4_fix.log` | 880 Hz: 18 рядків `I2S16 play … zc_pct=10 rt=0`, peak 17819→619; пауза `peak=0`; 659 Hz: `zc_pct=8`, peak 18014→…; жодного `rt≠0`; `Device Free heap 44195` (було 34351) | обидві ноти йдуть у I2S повністю; ZCR збігається з теорією (2f/fs = 11 % / 8 %) |
| N3 | MP3 dingdong через плеєр (`ENABLE_AUDIO_MP3_SELFTEST`) | `com4_selftest2.log` | `ID3 tag_size=35`; `mp3 stream: 16000 Hz, 1 ch, 40 kbps, layer 3, 576 samples/frame`; `I2S16 play samples=4032 peak=24` → `peak=9397 zc_pct=6` → `peak=11212 zc_pct=6` → `samples=1728 peak=1793`; `FG eof`; разом 13 824 семпли = 24 кадри = 0.86 с | **minimp3 + плеєр декодують коректно** (тиха вставка → два удари → спад; сміття дало б zc≈50 %). Resample не потрібен (16 kHz mono) |
| N4 | Параметри I2S | boot‑лог `tkl_i2s` | RX I2S0: 16000 Hz, 32 bit, SCK5/WS4/SD6. TX I2S1: 16000 Hz, 16 bit, BCLK15/LRC16/DOUT7, `TX Philips 16-bit stereo (L=R)`; TX enable один раз, `auto_clear` | конфігурація коректна для MAX98357A (BCLK = 32·fs = 512 kHz) |
| N5 | Mic loopback v1 (без DC‑фільтра) | `com4_lb.log` | під час chime: `rms=3204 zc_pct=0`, tone 659/523/×2/×3 = 0 | сигнал у мікрофоні — не тон, а зсув нуля/НЧ, що вмикається разом із SD |
| N6 | Mic loopback v2 (DC‑блокер ~20 Hz, dc/min/max окремо) | `com4_lb2.log`, `com4_tts.log` | chime: `dc` до −3079, `ac_rms 500–2300`, `zc_pct 2–7`, **tone_pct 659=0 523=0, ×2=0, ×3=0**. Рівень ≈ −25 dBFS ≈ 95 dB SPL — реально голосно | **динамік не випромінює частоти, які отримує**. Гіпотеза «неправильна частота дискретизації (×2, ×3)» відкинута |
| N7 | Перевірка детектора Goertzel | Python‑модель того ж коду | чистий 659 Hz → 100 %; 659+523+шум → 84/14 %; дзвін із затуханням → 65 %; гул 180 Hz → 0 % | детектор чесний; «0» у N6 — реальний результат |
| N8 | **Хмарний TTS** (користувач: BOOT → питання) | `com4_tts2.log` (`noreset`, 240 с) | `text -> NLG eof: 0, content: 4`; `ai agent -> downstream audio attr: codec=109 rate=16000 ch=1 bits=16`; `tts stream start (codec 0)`; `tts first chunk 864 B: FF F3 48 C4`; `mp3 stream: 16000 Hz, 1 ch, 32 kbps`; `tts stream stop: 4464 B in 6 chunks`; `I2S16 play samples=3456 peak=0` → `4032 peak=459` → `4032 peak=22934 rms=8744 zc_pct=15` → `4032 peak=18674 zc_pct=13` → `2304 peak=5029 zc_pct=8`; `FG eof` | **АУДІО‑ВІДПОВІДЬ ПРИХОДИТЬ І ДЕКОДУЄТЬСЯ В МОВУ.** 17 856 семплів = 31 кадр = 1.116 с, збігається з 4464 B @ 32 kbps |
| N9 | Швидкість I2S по таймінгу | N8 + вікна `mic loopback` по 100 мс | 17 856 семплів відіграно за ~0.9–1.0 с (8–9 вікон) | тактування 16 kHz; при 8 kHz було б ~2.2 с |
| N10 | **Silence test** (`ENABLE_AUDIO_SILENCE_TEST`: 2 с `SD=HIGH` + нулі) | `com4_final2.log` | під час нулів: `ac_rms 458–555, zc_pct 17` стабільно; після mute (`spk=0`): 271 → 62 | **підсилювач видає тон ~1.3–1.4 kHz без даних** — це не може бути прошивка |
| N11 | Старий тест 4 (MP3 loop → «писк 2 с») | ретроспектива | у старій прошивці reservoir‑баг + скидання буфера → PCM майже не було; SPEAK тримав SD HIGH | «писк на вікно play» = той самий idle‑писк підсилювача з N10 |

### Висновки з нічних тестів

- **Сервер шле аудіо** (N8). Проблема «порожнього TTS» не існує — не відкривати тікет у Tuya.
- **Декодер і плеєр справні** (N3, N8), після двох реальних фіксів (frame‑skip, reservoir) — але писк був не через них.
- **PCM у I2S правильний і йде з правильною швидкістю** (N2, N4, N9).
- **Аналоговий вихід не відповідає даним** (N6, N10). Локалізація: провід/контакт DIN‑BCLK‑LRC на breadboard, земля/живлення MAX98357A, або модуль/динамік.
- Всі попередні висновки «залізо працює, бо чутно тон» — скасовано: вухо чуло писк підсилювача, gated SD.

---

## Тести 2026-09-29 — пайка динаміка і GAIN

| # | Що | Результат |
|---|-----|-----------|
| H1 | Пайка динаміка на **+/−** amp (два динаміки) | Голос і ding-dong **є**; замість тиші/одного писку |
| H2 | GAIN → GND (12 dB) | Звук є, але **скрипить / рипить** |
| H3 | GAIN floating (9 dB) | **Без змін** відносно 12 dB |
| H4 | GAIN → **3V3** (6 dB) | **Без скрипу**, звук краще; **йде ривками** |

### Бітрейт vs ривки

Хмарний TTS на COM4: **16 kHz mono MP3 ~32 kbps** (`mp3 stream`, `tts_dumps/*.mp3` на ПК слухаються суцільно). 32 kbps дає «телефонну» зернистість, **не** старт-стоп. Ривки = паузи в PCM на динаміку:

- між викликами `tdl_audio_play` SD mute через **500 мс** (`SPK_SD_MUTE_DELAY_MS`) може клацати, якщо чанки рідші;
- **`ENABLE_TTS_UART_DUMP`** друкує hex усього MP3 по UART **під час** play — COM 115200 не встигає, класичні ривки саме на TTS;
- важкі `I2S16 play` + `mic loopback` теж крадуть CPU;
- голод DMA / Wi‑Fi: у логу `I2S16 play: new utterance (gap N ms)` — великі gap між рядками всередині однієї фрази.

Якщо **ding-dong теж ривками** — не бітрейт і не хмара, а I2S/SD/живлення. Якщо **лише TTS** — першим вимкнути UART-дамп.

---

## Результати на зараз

| Область | Статус |
|--------|--------|
| Плата + Wi‑Fi + MQTT + AI online | Працює |
| Mic → ASR (UA/EN) | Працює |
| Хмара → NLG (текст) | Працює |
| Agent Runtime Log: ASR / LLM | Success (є текст) |
| Agent Runtime Log: «TTS» | Рядки є, але Details часто = **LLM‑дерево** без нод синтезу — на платформі аудіо не видно, **доказ — на COM4** |
| **Хмара → аудіо (TTS MP3) на плату** | **ПРИХОДИТЬ** (2026‑09‑24 ніч): `downstream audio attr codec=109 16000 Hz mono`, `tts stream stop: 4464 B in 6 chunks`, `mp3 stream 16000 Hz mono 32 kbps`, у I2S мова (`peak 22934, zc_pct 13–15`) |
| I2S / декодер / PCM → DMA | **Працює** (16‑біт stereo, 16 kHz, підтверджено статистикою PCM і таймінгом) |
| Локальний **ding‑dong** (PCM chime, MP3 dingdong) | Чути після пайки **+/−** (2026-09-29) |
| Постійний писк у простої | Прибрано (SD‑mute), коли SD на GPIO17 |
| Скрип на корисному звуці | **GAIN 12 dB**; знято **GAIN 3V3 = 6 dB** |
| Потік TTS | **Рівний** після вимкнення dump + pre-buffer (підтверджено 2026-09-29 22:10) |

### Як орієнтуватись (важливо)

**Вухо ≠ доказ.** Докази на COM4 (`python %TEMP%\cap_com4.py <log> <сек> [noreset]`):

- `tts stream stop: N B in M chunks` — скільки MP3 реально прийшло (N≈0 → хмара не прислала);
- `mp3 stream: … Hz, … ch, … kbps` — що бачить декодер;
- `I2S16 play … peak rms zc_pct` — що йде в DMA: мова/chime `zc_pct 5–20`, тиша `peak=0`, сміття з декодера `zc_pct≈50`;
- `mic loopback: spk=1 … tone_pct` — що реально чує мікрофон, коли грає динамік. `tone_pct 659/523 > 30` під час chime = динамік грає ноти; `0` + `zc_pct≈17` = писк підсилювача.

Правило: якщо `I2S16 play` показує нормальний сигнал, а на слух писк — **прошивку не чіпати**, шукати в залізі.

---

## Висновки і думки

1. **Повний ланцюг колонки зібраний:** mic → Tuya → MP3 → I2S → MAX98357 → динамік. Тікет «немає TTS» не потрібен.
2. Проблеми були **шарами**, не однією: (а) контакт динаміка; (б) GAIN 12 dB; (в) UART dump + відсутній jitter-буфер. Кожен шар маскував наступний.
3. **Вухо без логу** двічі брехало; COM4 + файли MP3 на ПК — робочий метод. Class-D на SD=HIGH без валідного I2S пищить сам — це не «контент».
4. Продуктова база: HOLD, DP 203–205, OLED off, SD GPIO17, I2S 16-bit, MP3-фікси, GAIN 6 dB, pre-buffer.
5. Далі не «чи є звук», а **закомітити / чиста збірка / механіка / продукт**.

---

## Труднощі (коротко)

| Тема | Що сталося |
|------|------------|
| Стек | Перехід на TuyaOpen `your_chat_bot` |
| Flash / COM4 | esptool; wipe `0x0` скидає пару |
| SD на 3V3 | постійний писк → GPIO17 mute |
| BCLK + Class‑D | писк при unmute без валідного PCM |
| DP volume | демо DP **3**, продукт — **203** |
| 32‑біт I2S | на слух лише писк; **16‑біт** «дав чистий sine» — вночі з'ясовано, що це також був писк підсилювача; 16‑біт лишаємо як стандартний для MAX98357A |
| Висновки «на слух» | Двічі вели не туди (empty TTS, «залізо ок»). Правило: **лише по логу COM4 + mic loopback** |
| MP3 dingdong | Декод підтверджено статистикою PCM; вухо = писк → залізо |
| Empty TTS | Хибний висновок: DATA‑лог був закоментований. Реально `4464 B in 6 chunks` |
| Chime = одна нота | `rt=-3` malloc internal RAM → синтез чанками на стеку |
| Контакт динаміка | 2026-09-24 пайка **+/−** → з'явились голос і ding-dong |
| GAIN 12 dB | скрип; **6 dB (3V3) ок** |
| Ривки TTS | UART dump + no jitter buffer → вимкнути dump, pre-buffer 3 КБ |

---

## Бажаний кінцевий результат

1. При старті / connect — **впізнаваний** звук, тиша в простої. *(Досягнуто.)*
2. BOOT → питання → **рівна** відповідь без скрипу і ривків. *(Досягнуто 2026-09-29.)*
3. Надійна механіка (конденсатори, короткі I2S, корпус) — **наступний шар заліза**.
4. Опційно wake word, BOM, «чиста» прошивка без test-floor гучності.

---

## Наступні кроки

1. Закомітити TuyaOpen (pre-buffer, макроси=0, stall) **без** `tuya_config.h`; оновити `patches/`.
2. «Чиста» прошивка: floor volume, один connect-alert.
3. DP 206 на платформі; конденсатори на Vin за бажанням.
4. Wake word / корпус — після стабільної чистої збірки.

---

## Висновок і пропозиції (що лишити / що вимкнути)

**Висновок (2026-09-29 вечір):** колонка як AI-спікер **працює**. Скрип і ривки закриті. Далі — коміт, чиста збірка, продукт.

### Залишити назавжди (продукт)

1. PID / UUID / AuthKey, board `ESP32S3_BREAD_COMPACT_WIFI`
2. SD mute на **GPIO17** (idle LOW)
3. OLED вимкнений (без панелі)
4. DP **203 / 204 / 205** + report при online
5. I2S TX **16‑біт Philips stereo L=R** (`tkl_i2s.c`)
6. mono→stereo + play чанками в internal RAM
7. Фікси MP3: **reservoir** (`minimp3.h`) + **frame‑skip** (`decoder_mp3.c`)
8. PCM chime чанками на стеку (без великого `malloc`)
9. `default_vol = 50`, режим HOLD
10. cmake fallback у `cli_prepare.py`
11. **Pre-buffer TTS** у `svc_ai_player` (3 КБ / 1.5 с) — лишити
12. Діагностичні макроси **0** у продуктовій збірці

### Вимкнути перед «чистою» прошивкою (діагностика)

| Макрос / місце | Файл | → |
|----------------|------|---|
| `ENABLE_AUDIO_SILENCE_TEST` | `ai_audio_player.c` | **0** (зроблено 2026‑09‑29) |
| `ENABLE_AUDIO_MP3_SELFTEST` | `ai_audio_player.c` | **0** (зроблено) |
| `ENABLE_TTS_UART_DUMP` | `ai_audio_player.c` | **0** (зроблено; вмикати лише для дампу, не під час слухання) |
| `ENABLE_MIC_LOOPBACK_STATS` | `tdd_audio_no_codec.c` | **0** (зроблено) |
| `ENABLE_AUDIO_DEBUG_SEQ` | `ai_audio_player.c` | лишити **0** |
| `ENABLE_I2S_TONE_TEST` | `tdd_audio_no_codec.c` | лишити **0** |
| `I2S16 play` | `tdd_audio_no_codec.c` | бюджет урізано до 6 рядків на висловлювання; лишено `new utterance` / `stall` / `tts stream stop` / `mp3 stream` / `prebuffer ready` — дешеві, по 1 рядку |
| `AI_PLAYER_STREAM_PREBUF_BYTES` / `_MAX_MS` | `svc_ai_player.h` | 3072 B / 1500 мс — крутити, якщо ривки лишаться при `stall` у лозі (більше = стійкіше, але довша пауза перед відповіддю) |

### На розсуд (не терміново)

1. Connect‑alert: PCM ding‑dong vs стоковий MP3 dingdong — обрати один після живого динаміка
2. DP **206** (BT) — Delete на платформі, якщо не потрібен
3. Floor `volume < 50 → 50` у play — для тестів ок; для продукту краще прибрати
4. Закомітити патчі / оновити handoff після перевірки amp
5. Скрипти `dump_tts.py` / `monitor_mic.py` у `D:\esp32\1003` — інструменти ПК, не частина прошивки

### Порядок після появи звуку з динаміка

Зроблено: GAIN 3V3, dump off, pre-buffer, silence/selftest/loopback off, рівна відповідь. Далі — коміт і чиста збірка.

---

## Корисні шляхи

| Що | Де |
|----|-----|
| TuyaOpen | `D:\esp32\TuyaOpen` |
| App | `apps/tuya.ai/your_chat_bot` |
| DP / volume report | `apps/tuya.ai/your_chat_bot/src/tuya_main.c` |
| Audio play / SD mute | `boards/ESP32/common/audio/tdd_audio_no_codec.c` |
| I2S TX format | `platform/ESP32/tuya_open_sdk/tuyaos_adapter/src/drivers/tkl_i2s.c` |
| Alerts / PCM chime / boot‑діагностика | `src/ai_components/ai_audio/src/ai_audio_player.c` |
| MP3 декодер | `src/audio_player/src/decoder/decoder_mp3.c`, `…/minimp3/minimp3.h` |
| Захоплення COM4 | `%TEMP%\cap_com4.py <log> <сек> [noreset]` (RTS‑reset, фільтр ключових рядків) |
| Дамп TTS → MP3 на ПК | `D:\esp32\1003\dump_tts.py` → `tts_dumps\tts_NNN.mp3` (`ENABLE_TTS_UART_DUMP`) |
| Прошивка (без wipe пари) | `esptool --chip esp32s3 -p COM4 -b 921600 write_flash 0xd000 .build\bin\ota_data_initial.bin 0x10000 .build\bin\your_chat_bot.bin` |
| Board SD pin | `boards/ESP32/ESP32S3_BREAD_COMPACT_WIFI/esp32s3_bread_compact_wifi.c` |
| Agent Runtime Logs | https://developer.tuya.com/en/docs/iot/ai-agent-trace?id=Kfa18iwxmkeh5 |
| Патчі аудіо‑шляху (2026‑09‑24) | `patches/audio_path_2026-09-24.patch`, `patches/platform_esp32_tkl_i2s_2026-09-24.patch` |
| Continue | [`CONTINUE.md`](./CONTINUE.md) |
| Секрети | `secrets.local.md` (не комітити) |

---

## Підсумок одним реченням

**ПРАЦЮЄ (підтверджено користувачем 2026‑09‑29 22:10): mic/ASR/TTS ок, голос і ding-dong чути рівно, без скрипу і ривків. Скрип зняв GAIN 6 dB (3V3); ривки мали дві програмні причини (UART‑дамп TTS удвічі повільніший за real‑time + відсутній jitter‑буфер у плеєрі) — виправлено. Лишилось: закомітити зміни TuyaOpen (без `tuya_config.h`) і зібрати «чисту» прошивку.**
