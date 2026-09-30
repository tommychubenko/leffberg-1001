# Leffberg AI Speaker (ESP32-S3 + TuyaOpen)

Документ фіксує ідею проєкту, зроблено, результати, висновки й поточний стан (оновлено **2026-09-30 ~21:30**).

---

## СТАБІЛЬНА ВЕРСІЯ 2026-09-30 (wake word працює) — читати першим

### Що працює на платі

1. **Wake word «Hi Lexin»** (Espressif WakeNet9 `wn9_hilexin`, standalone, годується з I2S read task) → `KWS detected -> HEY_TUYA` → **virtual BOOT → LISTEN** → мова → ASR → відповідь TTS. Підтверджено логом COM4 після перевороту мікрофона:
   `tkl_kws: KWS detected -> HEY_TUYA (standalone, r=1 peak_in=11763 peak=23526)` → `ai_wakeup: KWS Hi Lexin word=3 (acts as BOOT)` → `KWS -> LISTEN (virtual BOOT)`.
2. **Голос (діалог):** mic → ASR → хмара → TTS → динамік — ок; TTS рівний (pre-buffer 3 КБ).
3. **BOOT як toggle у `AI_CHAT_MODE_WAKEUP`:** IDLE → BOOT → LISTEN → THINK → SPEAK → знову LISTEN ~30 с (follow-up); BOOT під час сесії → cancel → IDLE.
4. Tuya on-device KWS **не** використовується. «Hey Tuya» ця прошивка **не** розпізнає — у flash лише `wn9_hilexin`; `HEY_TUYA` — лише назва колбека.

### Чому wake не працював раніше (корінь)

**Порт INMP441 дивився в стіл/бредборд.** Спектр мови обрізався ~600 Hz (FFT PCM‑знімків: > 1.5 kHz ≈ 0 %), голосні ще витягував хмарний ASR, а WakeNet без приголосних не детектив нічого. Софт‑тракт (модель, feed, рейт 16 kHz, gain) при цьому був справний — це доведено тестом «динамік → мікрофон». **Після перевороту мікрофона отвором у повітря — детекція запрацювала одразу**, без змін коду.

### Коміти стабільної версії

| Репозиторій | Коміт / гілка / тег | Що всередині |
|-------------|--------------------|--------------|
| `D:\esp32\TuyaOpen` (master, локально; origin = tuya/TuyaOpen, туди не пушимо) | **`760d198a`**, тег `stable-2026-09-30-wakeword` (поверх `46f6e383` аудіо‑фіксів) | `tdd_audio_no_codec.c` (feed KWS, HPF 180 Hz, стек 6144, діагностика off), `ai_mode_wakeup.c`, `ai_chat_main.c`, `app_chat_bot.c` |
| `D:\esp32\TuyaOpen\platform\ESP32` (гілка **`leffberg-audio`**, origin = tuya/TuyaOpen-esp32) | **`29eb874`**, тег `stable-2026-09-30-wakeword` (поверх `a934eed`) | `tkl_kws.c` (standalone WN, gain 2.0), `tkl_kws.h`, `audio_afe.c/.h` (VAD‑only) |
| Handoff `D:\esp32\1003` (github.com/tommychubenko/leffberg-1001, main) | коміт цього README + `patches/*2026-09-30.patch`, тег `stable-2026-09-30` | Патчі для відтворення на чистому TuyaOpen |

Патчі: `patches/wakeword_hilexin_2026-09-30.patch` (TuyaOpen, `git apply` поверх `46f6e383`), `patches/platform_esp32_kws_afe_2026-09-30.patch` (platform/ESP32, поверх `a934eed`). Раніші патчі (аудіо 09‑24, pre-buffer 09‑29) лишаються актуальними. `tuya_config.h` з UUID/AuthKey **не в git** (є `config/tuya_config.h.example`).

### Стан макросів у стабільній збірці (`tdd_audio_no_codec.c` / `tkl_kws.c`)

| Макрос | Значення | Роль |
|--------|----------|------|
| `ENABLE_MIC_HPF` / `MIC_HPF_FC_HZ` | **1 / 180** | HPF 2‑го порядку на 24‑бітних даних до `>>14`: шум спокою 900 → ~90, DC‑дрейф прибрано. **Лишити.** |
| `TKL_KWS_GAIN` | **2.0** | Замість `get_vol_gain` (~10×, кліпінг). Норма в `wn feed`: `clipped=0`, `detects≈31`. |
| стек `esp32_i2s_read` | **6144** | у ньому працює `detect()` + колбек |
| `ENABLE_MIC_LEVEL_LOG` | 0 | діагностика (peak/rms/hfpct 1 Гц) — вмикати лише для розбору мікрофона |
| `ENABLE_MIC_PCM_DUMP` | 0 | PCM‑знімки base64 → `%TEMP%\pcmd_to_wav.py` |
| `ENABLE_MIC_FREQ_RESP_TEST` | 0 | сходинки тонів динамік → мікрофон (Goertzel) |
| `ENABLE_MIC_LOOPBACK_STATS`, `ENABLE_I2S_TONE_TEST` | 0 | стара діагностика |
| лог `wn feed: …` 1 Гц | увімкнено | єдиний «пульс» KWS у стабільній збірці; корисний і в полі |

Перевірка стабільної збірки після прошивки (`com4_stable_boot.log`): `mic HPF: 2nd-order 180 Hz @ 16000 Hz`, `standalone WakeNet ready … gain=2.0`, `KWS armed (standalone Hi Lexin)`, `wn feed: … clipped=0 detects=31`, без `mic level` / `pcm dump`.

### Не чіпати без нової скарги

I2S TX 16-bit Philips L=R, MP3 reservoir/frame-skip, GAIN 3V3, SD GPIO17, TTS pre-buffer 3 КБ, HPF 180 Hz, KWS gain 2.0, **орієнтація INMP441 — отвір у повітря, не в плату**.

### Наступні кроки (після стабільної)

1. Корпус: отвір під мікрофон навпроти порту INMP441; мікрофон подалі від динаміка.
2. Оцінити false‑wake у побуті (лог `KWS detected` без мови); за потреби підняти threshold 0.40 → 0.5 у `tkl_kws.c`.
3. Опційно: пуш коду TuyaOpen/platform у форки на GitHub (зараз код — лише локальні коміти + патчі в handoff).

---

## Архів: стан на 2026-09-30 ~00:45 (до знаходження причини)

### Працювало

1. **Голос (діалог):** mic → ASR → хмара → TTS → динамік — ок після фіксу контактів INMP441 і аудіо-шляху.
2. **BOOT як PTT/toggle у `AI_CHAT_MODE_WAKEUP`:**
   - IDLE → BOOT → LISTEN (alert) → мова → THINK → SPEAK;
   - після кінця TTS знову **LISTEN ~30 с** (follow-up без повторного wake);
   - BOOT під час сесії → cancel → IDLE.
3. **Архітектура wake (задум):** Tuya on-device KWS **не** використовуємо. В IDLE — **Espressif WakeNet `wn9_hilexin`**; детекція має = **virtual BOOT** (той самий шлях, що кнопка).

### Тоді не працювало

4. **Wake word Hi Lexin — НЕ спрацьовував.** У логах **0** × `KWS detected` / `wakeword detected` при багатьох спробах. (Розв'язано 30.09 увечері — див. вище.)
5. Мікрофон при цьому **живий**: в IDLE піки ростуть, коли говориш; standalone WN їсть PCM (`wn feed: … detects≈16/s`).
6. Модель у flash: `wn9_hilexin`, word1 = **`嗨，乐鑫`**, rate=16000, chunk=512. Колбек заплановано як `HEY_TUYA` (бренд), акустика — Hi Lexin.

---

## Wake word — сесія 2026-09-29/30 (історія; результат — у блоці «СТАБІЛЬНА ВЕРСІЯ» вище)

### Цільова схема (погоджено)

```
IDLE:  Espressif WN9_HILEXIN ("Hi Lexin") ──detect──► virtual BOOT → LISTEN → … діалог
LISTEN: WakeNet OFF; AFE VAD ON → cloud ASR
BOOT:   той самий шлях LISTEN / cancel (toggle)
```

Tuya wake-фразу / хмарний KWS **не** використовуємо. On-device лише Espressif.

### Що змінили в коді (закомічено 30.09: TuyaOpen `760d198a`, platform/ESP32 `29eb874`)

| Зміна | Навіщо |
|-------|--------|
| Дефолт `AI_CHAT_MODE_WAKEUP`, force mode у firmware | Режим wake замість HOLD з NVS |
| BOOT toggle IDLE↔cancel; після TTS → LISTEN 30 с | UX; фікс follow-up після «привіт» |
| `tkl_kws.c`: **standalone WakeNet** (`esp_wn_*`), `tkl_kws_feed()` з I2S | Обхід AFE-гейту; Hi Lexin → `HEY_TUYA` → virtual BOOT |
| AFE: `wakenet_init=false`, VAD лише для LISTEN | Розділення KWS / ASR |
| `srmodels`: `wn9_hilexin` (UTF-8 build), flash `0xed0000` | Модель у partition `model` |
| Mic PCM: було `>>16`, зараз **`>>14`** (WN digital gain: ~10× → **2.0** фіксовано 30.09) + HPF 180 Hz | Гучніший сигнал для WN без кліпінгу |
| DET threshold 0.40, DET_MODE_95 (коли був AFE-WN) | Чутливість |

Ключові файли: `tkl_kws.c`, `audio_afe.c`, `ai_mode_wakeup.c`, `tdd_audio_no_codec.c`, sdkconfig HILEXIN.

### Що підтвердили логами (COM4)

| Факт | Доказ |
|------|--------|
| Модель завантажена | `standalone WakeNet ready: model=wn9_hilexin word1=嗨，乐鑫 chunk=512 rate=16000` |
| KWS armed в IDLE | `KWS armed (standalone Hi Lexin)` |
| PCM доходить до WN | `wn feed: peak=… detects=15..16 gain=10.0 armed=1` (~кожну секунду) |
| Голос чути міком | `afe feed` / `wn feed` peak скаче (тисячі → 10k–32k при мові) |
| BOOT = virtual wake | `BOOT -> LISTEN (virtual BOOT)` + ASR + SPEAK |
| Follow-up після TTS | LISTEN після PLAY_END (після фіксу race з `sg_is_wakeup`) |
| **Детекція wake** | **`KWS detected` = 0** на всіх зйомках |

### Що пробували і не допомогло для Hi Lexin

- AFE pipeline з WakeNet + VAD / без VAD / `disable_wakenet` у LISTEN;
- `DET_MODE_95`, threshold 0.40;
- digital gain ×4 / ×8 / `get_vol_gain` (~10);
- mic shift `>>16` → `>>14`;
- standalone `esp_wn_iface` напряму на PCM (поза AFE).

### Відкрита проблема (станом на ранок 30.09 — закрита увечері)

WakeNet **ініціалізований і годується**, але **ніколи не повертає detect>0** на «Hi Lexin». Можливі напрямки далі: вимова/акцент vs тренування `嗨，乐鑫`, якість/спектр INMP441 після shift, альтернативна модель/кастомна фраза Espressif, або тимчасово лишити лише BOOT.

Критерій успіху wake: у логу `KWS detected -> HEY_TUYA` і одразу `… -> LISTEN (virtual BOOT)` без натискання BOOT. **Досягнуто 30.09 ~20:50** (див. нижче і блок «СТАБІЛЬНА ВЕРСІЯ»).

### Сесія 2026-09-30 (вечір) — що знайдено інструментально

Стан на момент паузи (~20:25): **корінь — фізика мікрофона**, софт‑тракт до WakeNet перевірений і чистий.

| Крок | Факт |
|------|------|
| На платі стояла прошивка 23:58 (не остання) | `KWS armed`, але **жодного `wn feed`** → PCM до WakeNet взагалі не доходив |
| `tkl_kws.c`: `get_vol_gain(-25 dB)` одразу після `create()` | Повертав ~10× → кліпінг кожного слова. Замінено на фіксований `TKL_KWS_GAIN 2.0`; лог `wn feed: peak_in/peak/clipped/detects` (1 Гц, норма `detects≈31`, `clipped=0`) |
| Стек `esp32_i2s_read` 3072 → 6144 | у ньому крутиться `detect()` + колбек wake |
| PCM‑знімки (`ENABLE_MIC_PCM_DUMP`, `PCMD:` base64 → `%TEMP%\pcmd_to_wav.py`) | Сигнал спокою: 90 % енергії < 300 Hz, «плаваючий» DC (100‑мс відрізки з rms~1000 і 0 перетинів нуля) → **HPF 180 Hz 2‑го порядку** на 24‑бітних даних до `>>14` (`ENABLE_MIC_HPF`). Шум спокою 900 → **~90** (rms 22, білий), кліпінгу немає |
| Тест «динамік → мікрофон» (`ENABLE_MIC_FREQ_RESP_TEST`, сходинки 300…4000 Hz) | 300:75 500:204 1000:760 2000:1043 3000:595 4000:389 — кожен тон точно у своєму біні: **рейт 16 kHz точний, мікрофон бачить 2–4 kHz** |
| FFT записів «Хай Лешін» (з 0.5–1 м) | 700–1500 Hz ≤ 2 %, > 1500 Hz ≈ 0 % — **мова обрізана ~600 Hz**, приголосних немає. WakeNet із такого не розпізнає нічого; хмарний ASR ще витягує за голосними |
| Причина | **Порт INMP441 дивився в стіл/бредборд** (підтверджено користувачем). Тест динаміком пройшов, бо вібрація йде через плату прямо в корпус мікрофона |

**Результат (~20:50):** користувач перевернув INMP441 отвором у повітря — **«Хай Лешін» спрацьовує**, лог: `KWS detected -> HEY_TUYA (standalone, r=1 peak_in=11763 peak=23526)` → `KWS Hi Lexin word=3 (acts as BOOT)` → `KWS -> LISTEN (virtual BOOT)` → `state change form IDLE to LISTEN`. У `mic level` під час мови `peak≈29000 rms≈3000 hfpct≈58` (раніше в «стіл» — мова без ВЧ). Контрольний варіант через AFE (`wakenet_init=true`) не знадобився.

**Стабільна збірка (~21:15):** вимкнено `ENABLE_MIC_LEVEL_LOG`, `ENABLE_MIC_PCM_DUMP` (`ENABLE_MIC_FREQ_RESP_TEST` уже 0); лишено `ENABLE_MIC_HPF 1`, `TKL_KWS_GAIN 2.0`, стек 6144, `wn feed` 1 Гц. Зібрано, прошито, boot‑лог чистий (`com4_stable_boot.log`). Закомічено: TuyaOpen `760d198a`, platform/ESP32 `29eb874` (гілка `leffberg-audio`), обидва з тегом `stable-2026-09-30-wakeword`. «Hey Tuya» ця прошивка не розпізнає в принципі — у flash лише `wn9_hilexin`; `HEY_TUYA` — назва колбека.

---

## ГОЛОВНЕ (архів 2026-09-29, ~23:40) — попередній зріз

1. **Стабільний голос ок** (BOOT PTT / toggle): mic/ASR/TTS після фіксу контактів INMP441.
2. **Режим `AI_CHAT_MODE_WAKEUP`**: акустика **Hi Lexin** (`wn9_hilexin`); колбек **`HEY_TUYA`**. Рідний Tuya ESP32 був би «你好小智»; «Hey Tuya» публічно немає. **Хмара Tuya не керує on-device WakeNet.**
3. **WakeNet Espressif:** **Hi Lexin** — безкоштовний для комерції; своя фраза — платний кастом.
4. **Не чіпати** без нової скарги: I2S 16-bit Philips, MP3 reservoir/frame-skip, GAIN 3V3, SD GPIO17, pre-buffer 3 КБ.
5. **Тоді в роботі:** on-device KWS; у логах шукати `wakeword detected` / `KWS detected`. (Закрито 30.09 — див. початок документа.)

Деталі фіксів ривків — блок нижче; ніч 24-го — «Тести 2026-09-24».

**Оновлення 2026-09-29 (пізно) — ривки: дві причини знайдено в коді, обидві виправлено, прошито:**

- **`ENABLE_TTS_UART_DUMP`** робив hex‑дамп кожного TTS‑chunk (~864 B → 54 рядки × ~90 символів ≈ 4.9 КБ по UART 115200 ≈ **425 мс**) у потоці прийому **до** `tuya_ai_player_feed`, тоді як сам chunk = 216 мс звуку. Подача в плеєр була вдвічі повільніша за відтворення → ривки гарантовано. Вимкнено разом з іншою діагностикою.
- **У `svc_ai_player.c` не було jitter‑буфера**: декодування починалось з першого байта в ring‑буфері; кожен мережевий chunk відігравався і плеєр чекав наступного (DMA I2S тримає лише 90 мс). Додано pre‑buffer для `AI_PLAYER_SRC_MEM`: старт після **3 КБ** (~0.75 с при 32 kbps) або EOF, або 1.5 с таймауту. Лог: `ai player FG prebuffer ready: N B queued after M ms`.
- Новий детектор: `I2S16 play: stall N ms between chunks (DMA underrun -> dropout)` — якщо між викликами play() пауза 100–500 мс. **Немає `stall` у лозі = немає дірок у PCM.** Якщо ривки лишаються без `stall` — це вже не потік, а живлення/динамік.
- Побічно: `tos.py build` зависав, бо питав про оновлення `platform/ESP32` до коміту з yaml (інший коміт `a934eed`). Створено маркер `D:\esp32\TuyaOpen\.cache\.dont_prompt_update_platform` → більше не питає.

Деталі логів TTS 2026-09-24 — у розділі «Тести 2026-09-24 (ніч)» нижче.

---

## Нотатки наступному агенту (2026-09-30)

Аудіо-MVP **закрито**. Wake word **закрито 30.09 увечері** (причина — орієнтація мікрофона; див. «СТАБІЛЬНА ВЕРСІЯ»). Не відкривати знову «empty TTS», «32-bit I2S», «бітрейт 32k», «GAIN на GND», «WakeNet не детектить при живому feed».

### Що вважати готовим

| Шар | Стан |
|-----|------|
| Mic → ASR → NLG | Працює |
| Хмара → MP3 16 kHz ~32 kbps | Працює (COM4 + `tts_dumps`) |
| minimp3 + reservoir/frame-skip | Працює |
| I2S Philips 16-bit L=R | Працює |
| Amp MAX98357, GAIN 3V3, SD GPIO17, динамік паяний | Працює |
| Рівний TTS (без ривків) | Працює після вимкнення dump + pre-buffer |
| BOOT toggle + follow-up LISTEN 30 с | Працює |
| Espressif Hi Lexin → virtual BOOT | **Працює** (мікрофон отвором у повітря; HPF 180 Hz; gain 2.0) |

### Не чіпати

I2S формат TX, MP3-фікси, GAIN 3V3, HPF 180 Hz, KWS gain 2.0, орієнтація INMP441, UUID у git. Pre-buffer (`AI_PLAYER_STREAM_PREBUF_BYTES` 3072) — лишити, поки звук рівний.

### Наступний етап

1. ~~Wake word~~ — зроблено 30.09 (`760d198a` / `29eb874`).
2. ~~Закомітити TuyaOpen, оновити `patches/`~~ — зроблено 30.09. `tuya_config.h` з ключами — як і раніше **не в git**.
3. «Чиста» прошивка: floor volume; один connect-sound (зараз PCM chime); діагностика вже 0.
4. Платформа Tuya: DP **206** Delete, якщо не BT.
5. Залізо: корпус з отвором під мікрофон, конденсатори на Vin, коротші I2S, BOM.
6. Поле: поспостерігати false‑wake (`KWS detected` без мови); threshold 0.40 в `tkl_kws.c` за потреби.

Критерій демо без wake: тиша в простої, ding-dong, BOOT → рівна відповідь + follow-up, Smart Life жива.  
Критерій демо з wake: те саме + `KWS detected` на Hi Lexin — **виконано 30.09**.

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
| Talk | кнопка **BOOT** (GPIO0), wake-mode toggle + запасний PTT |
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
- OLED вимкнено; чат **`AI_CHAT_MODE_WAKEUP`** (BOOT toggle + Hi Lexin-задум; після TTS знову LISTEN **30 с**). HOLD більше не дефолт.
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
| BOOT wake + follow-up | **Працює** (2026-09-30) |
| Espressif Hi Lexin (on-device) | **НЕ працює** — 0 детекцій при живому `wn feed` |

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
4. Опційно **робочий** wake word (зараз лише код/модель, детекція ні), BOM, «чиста» прошивка без test-floor гучності.

---

## Наступні кроки

1. **Добити Hi Lexin** (або задокументувати «тільки BOOT»): лог `KWS detected` обов’язковий для закриття wake.
2. Закомітити TuyaOpen (pre-buffer, wakeup UX, standalone `tkl_kws`, mic `>>14`) **без** `tuya_config.h`; оновити `patches/`.
3. «Чиста» прошивка: floor volume, один connect-alert, diag macros=0.
4. DP 206 на платформі; конденсатори на Vin за бажанням.
5. Корпус / BOM — після стабільної чистої збірки.

---

## Висновок і пропозиції (що лишити / що вимкнути)

**Висновок (2026-09-30):** колонка як AI-спікер **працює через BOOT**. Скрип і ривки закриті. **Wake word Hi Lexin — відкрита проблема** (модель/PCM ок, detect=0). Далі — KWS або «тільки BOOT», коміт, чиста збірка.

### Залишити назавжди (продукт)

1. PID / UUID / AuthKey, board `ESP32S3_BREAD_COMPACT_WIFI`
2. SD mute на **GPIO17** (idle LOW)
3. OLED вимкнений (без панелі)
4. DP **203 / 204 / 205** + report при online
5. I2S TX **16‑біт Philips stereo L=R** (`tkl_i2s.c`)
6. mono→stereo + play чанками в internal RAM
7. Фікси MP3: **reservoir** (`minimp3.h`) + **frame‑skip** (`decoder_mp3.c`)
8. PCM chime чанками на стеку (без великого `malloc`)
9. `default_vol = 50`, режим **WAKEUP** (BOOT + Hi Lexin-задум; HOLD більше не дефолт)
10. cmake fallback у `cli_prepare.py`
11. **Pre-buffer TTS** у `svc_ai_player` (3 КБ / 1.5 с) — лишити
12. Діагностичні макроси **0** у продуктовій збірці
13. Standalone Espressif WN у `tkl_kws` (поки без успішної детекції)

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
| Wake mode / virtual BOOT | `src/ai_components/ai_mode/src/ai_mode_wakeup.c` |
| Standalone Hi Lexin KWS | `platform/ESP32/.../audio/tkl_kws.c` (+ `tkl_kws_feed` з `tdd_audio_no_codec.c`) |
| AFE VAD (LISTEN) | `platform/ESP32/.../audio/audio_afe.c` |
| MP3 декодер | `src/audio_player/src/decoder/decoder_mp3.c`, `…/minimp3/minimp3.h` |
| Захоплення COM4 | `%TEMP%\cap_com4.py <log> <сек> [noreset]`; live KWS: `%TEMP%\mon_kws.py` |
| Дамп TTS → MP3 на ПК | `D:\esp32\1003\dump_tts.py` → `tts_dumps\tts_NNN.mp3` (`ENABLE_TTS_UART_DUMP`) |
| Прошивка app | `esptool … write_flash 0xd000 ota_data_initial.bin 0x10000 your_chat_bot.bin` |
| Прошивка + srmodels | … + `0xed0000 .build\bin\srmodels.bin` (після зміни WakeNet моделі) |
| Board SD pin | `boards/ESP32/ESP32S3_BREAD_COMPACT_WIFI/esp32s3_bread_compact_wifi.c` |
| Agent Runtime Logs | https://developer.tuya.com/en/docs/iot/ai-agent-trace?id=Kfa18iwxmkeh5 |
| Патчі аудіо‑шляху (2026‑09‑24) | `patches/audio_path_2026-09-24.patch`, `patches/platform_esp32_tkl_i2s_2026-09-24.patch` |
| Патч pre-buffer TTS (2026‑09‑29) | `patches/stutter_prebuffer_2026-09-29.patch` |
| Патчі wake word (2026‑09‑30, стабільна) | `patches/wakeword_hilexin_2026-09-30.patch` (TuyaOpen), `patches/platform_esp32_kws_afe_2026-09-30.patch` (platform/ESP32) |
| PCM‑знімки мікрофона → WAV | `%TEMP%\pcmd_to_wav.py <log> <prefix> [--bands]` (потрібен `ENABLE_MIC_PCM_DUMP 1`) |
| Continue | [`CONTINUE.md`](./CONTINUE.md) |
| Секрети | `secrets.local.md` (не комітити) |

---

## Підсумок одним реченням

**2026-09-30 (стабільна): «Hi Lexin» → LISTEN → діалог → follow-up працює без кнопки; причиною мовчання WakeNet була орієнтація INMP441 (порт у стіл), софт‑фікси — HPF 180 Hz, KWS gain 2.0, стек 6144; діагностика вимкнена; коміти TuyaOpen `760d198a`, platform/ESP32 `29eb874`, патчі в `patches/*2026-09-30.patch`. Далі — корпус і спостереження за false‑wake.**
