# Cardputer Meme Soundboard — CLAUDE.md

## Project Overview
Firmware for **M5 Cardputer ADV** (ESP32-S3): dual-mode device.
- **Mode 1 — Soundboard**: the UI is chosen by `useSoundboardBrowseUI()` (`sdReady && !boardPaths.empty() && soundboardDir != ""`):
  - **Browse UI** (any `/boards/<name>/` panel is active): `,`/`/` step through keys that have a playable `.mp3` (current panel or `meme/` fallback), ENTER replays the current one, any letter/digit key jumps to that key and plays it immediately. Image (`<key>.jpg`, then `<key>.png`) fills the screen; colour tile with the letter if no image. A key without an `.mp3` plays a short 220 ms tone instead.
  - **Piano** (PIANO slot selected, or no boards on SD / no SD): pure polyphonic synth (tones only, no MP3s), 36 notes C3–B5; hold keys for chords; pressed keys highlighted yellow.
  - Panels are first-level subfolders under `/boards/`, sorted alphabetically with `meme` forced first. Missing file on the active panel → fallback from `/boards/meme/`.
  - TAB cycles: boards[0] (meme) → … → boards[N-1] → **PIANO** → MP3 Player → boards[0]. PIANO is encoded as `soundboardBoardIdx == boardPaths.size()`.
  - Every panel switch shows a splash (`<NAME> BOARD`); the first key press dismisses it and is then processed normally.
- **Mode 2 — MP3 Player**: file browser rooted at `/mp3`, arbitrary nesting, dirs listed first. Auto-advance continues from the **playing** file's folder (`playerSelectCurrentFile()`), not from the cursor.

## Hardware
- Board: M5Stack Cardputer ADV (ESP32-S3, 240×135 ST7789 display, physical QWERTY keyboard)
- Audio: built-in I2S speaker via M5Unified
- Storage: microSD via SPI — SCK=40, MISO=39, MOSI=14, CS=12 (10 MHz)
- Flash: 8 MB → partition `default_8MB.csv` (the device will not boot with a 16 MB table)

## Build System
- **PlatformIO** + Arduino framework (Arduino-ESP32 2.x / IDF4), board: `m5stack-stamps3`
- `pio run` — build
- `pio run -t upload --upload-port /dev/tty.usbmodem201101` — flash firmware
- `pio device monitor` — serial monitor (115200)
- Build flags: `BOARD_HAS_PSRAM`, `ARDUINO_USB_CDC_ON_BOOT=1` (Serial = USB CDC)
- No unit tests; verification is on-device via serial logs.

## Key Libraries
- `m5stack/M5Cardputer` — hardware abstraction (keyboard, display, speaker); pulls in M5Unified + LovyanGFX
- ESP8266Audio — MP3 decoding (`AudioGeneratorMP3` = libmad), **local copy** in `lib/ESP8266AudioLocal/`
  - `AudioOutputI2S*`, `AudioOutputPDM*`, `AudioOutputSPDIF*`, `AudioOutputULP*` were removed — they require `driver/i2s_std.h` (IDF5), while Arduino-ESP32 2.x uses IDF4
- `src/AudioOutputM5Speaker.h` — custom bridge ESP8266Audio → M5Unified Speaker (triple buffer, stereo `playRaw()` on channel 0)

## SD Card Structure
```
/settings.cfg       ← written by firmware: volume=<0..255>, panel=<idx>
/boards/
  meme/             ← default panel: a.mp3, a.jpg, … (a-z, 0-9)
  <other>/          ← custom panel — same filenames; missing file → fallback from meme/
/mp3/
  classic/          ← 10 tracks
  background/       ← 10 tracks
  ← arbitrary folder nesting is supported
```
Bundled content: `sd_card_content/` — copy the whole tree to the SD card.
Images are loaded into internal DMA RAM; files > 200 KB are skipped (colour tile is shown).

## SD Card — File Transfer over USB Serial
While the Cardputer is connected via USB, the firmware accepts `SD> …` text commands on the serial port (115200 baud): `PING LS STAT GET RM MKDIR PUT HELP`. Full protocol: `docs/SD_SERIAL_TRANSFER.md`. `PUT` is processed synchronously in `loop()` — the UI is frozen while a file uploads.

Python helper (`tools/sd_xfer.py`, requires `pyserial`):
```bash
# Normalize MP3s, then mirror sd_card_content/ to the card (deletes extra files, uploads missing/size-changed)
./tools/sync_sd_card_content.sh          # port: /dev/tty.usbmodem201101 by default
./tools/sync_sd_card_content.sh -p <port>
SYNC_SD_CONFIRM=yes ./tools/sync_sd_card_content.sh   # skip the y/N prompt

# Individual operations
python3 tools/sd_xfer.py -p /dev/tty.usbmodem201101 sync sd_card_content
python3 tools/sd_xfer.py -p /dev/tty.usbmodem201101 ls /boards/meme
python3 tools/sd_xfer.py -p /dev/tty.usbmodem201101 put local.mp3 /boards/meme/a.mp3
python3 tools/sd_xfer.py -p /dev/tty.usbmodem201101 rm /mp3/old.mp3
```
- Close `pio device monitor` before running the helper (only one program can hold the port).
- `sync_sd_card_content.sh` first runs `tools/normalize_mp3.sh`, which **re-encodes files in `sd_card_content/` in place** (needs `ffmpeg`/`ffprobe`): `/boards/**` → mono 44.1 kHz 96 kb/s, `/mp3/**` → mono 44.1 kHz ≤128 kb/s. Expect git diffs on MP3s after a sync.

## Soundboard — Note Mapping
Keyboard rows bottom → top map to low → high notes (`NOTE_KEY` / `NOTE_FREQ` / `NOTE_NAME` in `notes.cpp`). The same `NOTE_KEY` order defines the browse order for `,` / `/`.

| Row | Keys | Notes |
|-----|------|-------|
| Bottom | `z x c v b n m` | C3 C#3 D3 D#3 E3 F3 F#3 |
| Middle | `a s d f g h j k l` | G3 G#3 A3 A#3 B3 C4 C#4 D4 D#4 |
| Top | `q w e r t y u i o p` | E4 F4 F#4 G4 G#4 A4 A#4 B4 C5 C#5 |
| Digits | `1 2 3 4 5 6 7 8 9 0` | D5 D#5 E5 F5 F#5 G5 G#5 A5 A#5 B5 |

- Up to 7 simultaneous voices (M5.Speaker channels 1–7); the 8th note steals channel 1
- A note sounds while the key is held (press → `noteOn`, release → `noteOff`)
- The piano strip is redrawn incrementally (only changed keys); `pianoNeedsFullRedraw` forces a full redraw

## MP3 Player — Controls
| Key | Action |
|-----|--------|
| `j` / `.` | Next item |
| `k` / `;` | Previous item |
| `l` / `/` | Enter folder |
| `h` / `,` | Go up one level (not above `/mp3`) |
| ENTER on file | Play / "pause" / "resume" — pause is `stopAudio()`, resume **restarts the file from the beginning** (no seek support) |
| ENTER on folder | Enter folder |
| `` ` `` (ESC) | If playing/paused: first press jumps the list to the current file, next press stops; if stopped: go up one level |
| TAB | → boards[0] (meme) |

## Universal Controls
| Key | Soundboard | MP3 Player |
|-----|------------|------------|
| TAB | → next panel → PIANO → MP3 Player (cycle) | → boards[0] (meme) |
| `+` / `=` | Volume up (step 16, persisted) | Volume up |
| `-` | Volume down | Volume down |
| `` ` `` | Stop audio + release all notes | Jump to current / stop / go up |

## Source Layout
```
src/
  main.cpp               ← setup(), loop(), mode switching (enterSoundboard/enterMp3Player), boot screen, WDT setup
  config.h               ← #define constants (pins, paths, screen geometry, note channels), AppMode enum
  audio.h/cpp            ← audio task (core 0), static MP3 decoder/source, stopAudio/startMp3
  AudioOutputM5Speaker.h ← ESP8266Audio → M5Unified bridge (triple buffer, abort flag)
  notes.h/cpp            ← note tables, polyphony state, noteOn/noteOff/stopAllNotes
  soundboard.h/cpp       ← board scan, meme/image resolution with fallback, browse UI, piano UI, key handling
  mp3player.h/cpp        ← MP3 player file browser UI and key handling
  settings.h/cpp         ← volume state + overlay, load/save /settings.cfg, applyVolume
  ui_utils.h/cpp         ← isEscKey, clearStatusBar, readFileToBuffer (≤200 KB, internal DMA RAM)
  log.h/cpp              ← logLine/logBoot/logHeap/logTaskStack (mutex-protected Serial)
  sd_serial_xfer.h/cpp   ← USB serial SD protocol (SD> commands)
lib/
  ESP8266AudioLocal/     ← ESP8266Audio without I2S output files
sd_card_content/         ← mirror of the SD card (boards/meme, mp3/classic, mp3/background)
tools/
  sd_xfer.py             ← Python helper: ls/stat/get/put/rm/mkdir/sync over serial
  sync_sd_card_content.sh ← normalize + mirror sd_card_content/ to SD card
  normalize_mp3.sh       ← ffmpeg re-encode of sd_card_content/ MP3s (in place)
docs/
  SD_SERIAL_TRANSFER.md  ← full serial protocol reference
article/                 ← EN/RU blog article about the project + images
platformio.ini
```

## Critical Implementation Notes
- **IDF4/IDF5**: `driver/i2s_std.h` is not available on IDF4 → I2S output files removed from ESP8266Audio
- **Images**: `drawJpgFile(SD, path)` does not work (no `DataWrapperT<fs::SDFS>`). Workaround: `readFileToBuffer()` into heap (`heap_caps_malloc(INTERNAL|DMA)`), then `drawJpg(buf, len)` / `drawPng(buf, len)`
- **Audio task**: dedicated FreeRTOS task `audioTaskFn` pinned to core 0, priority 2 (> Arduino loop's 1), 20 KB stack, guarded by a mutex. The main loop is never blocked by decoding. The decoder (`AudioGeneratorMP3`) and source (`AudioFileSourceSD`) are **static objects with a pre-allocated buffer** — `gen`/`src` pointers are just set to them or `nullptr`; never `new`/`delete` them (avoids heap fragmentation / OOM reboots). `audioEndedNaturally` is set by the task and handled in `loop()`. The decoder loop ends with `vTaskDelay(1)`.
- **Stopping audio**: `stopAudio()` / `startMp3()` first call `spk->requestAbort()`; while the flag is set `ConsumeSample` returns `false` and `flushBuffer` skips the blocking `playRaw`, so the audio task releases the mutex quickly. **`ConsumeSample` must return `false` on abort** — returning `true` spins `gen->loop()` forever and deadlocks the main task. `spk->stop()` resets the flag.
- **SD access while playing**: concurrent SD access from the main loop and the audio task is safe at the driver level — FatFS is built with `FF_FS_REENTRANT=1` and `sd_diskio` wraps every command in `SPIClass::beginTransaction` (mutex), and the display is on a separate bus (SPI3_HOST via M5GFX; SD uses Arduino `SPI` = FSPI/SPI2). The MP3 player browses folders and `saveSettings()` writes while music plays. The soundboard still calls `stopAudio()` before drawing a new meme's image — that is just to cut the previous clip; keep the order `stopAudio → draw → startMp3`.
- **End of stream**: on natural end the audio task does `spk->flush()` then `spk->drain()` (waits for `M5.Speaker.isPlaying(0)` to reach 0, aborts on `requestAbort()`) before `gen->stop()`, otherwise `Speaker.stop(0)` clips the last ~30–50 ms.
- **Watchdog**: the audio task starves IDLE0 by design. `setup()` re-inits TWDT with a **15 s timeout + panic** (`esp_task_wdt_init(15, true)`) and removes IDLE0 from it (`esp_task_wdt_delete(xTaskGetIdleTaskHandleForCPU(0))`). The audio task subscribes itself (`esp_task_wdt_add(nullptr)`) and feeds it every loop iteration and before each `playRaw`, so a hung decoder (libmad inf-loop on a corrupt frame) still panics. **Do not re-add IDLE0 to the WDT.**
- **Audio output buffering**: `AudioOutputM5Speaker` MUST use **triple buffering** (3 × 1536 int16 ≈ 3 × 17 ms), not double. M5Unified's Speaker keeps a 2-slot queue per channel; with 2 buffers the next `playRaw` overwrites the one still being DMA'd → constant hiss. Output is **stereo** pairs straight from the decoder.
- **Audio task stack**: 20 KB. libmad needs ~10–12 KB for stereo frames. Watch `[AUDIO] task_stack_min_free=…`; bump `xTaskCreatePinnedToCore` in `audio.cpp` if it drops below ~2 KB.
- **Logging**: `logLine(tag, fmt, …)` → `[<millis>][TAG] message`, mutex-protected so lines from both cores don't interleave. The leading `[…][…]` matches the regex in `tools/sd_xfer.py` so SD-over-serial transfers ignore log lines — **any new Serial output must use `logLine`** (or the `+`/`-` protocol prefixes), otherwise it can break `sd_xfer.py`. `logBoot()` prints `esp_reset_reason()` — first thing to check after a spontaneous reboot.
- **Polyphony**: M5.Speaker channel 0 = MP3 (`AudioOutputM5Speaker`); notes use channels 1–7. `M5.Speaker.tone(freq, 0, ch, false)`: duration 0 → infinite, `stop_current=false` → doesn't cut other channels.
- **Keyboard**: soundboard handles press **and** release via `isChange()` and diffs `st.word` against `prevSbKeys` (edge detection); `,`/`/` and `+`/`-` have their own prev-state flags. MP3 Player only reacts to `isPressed()`.
- **Settings**: `saveSettings()` writes `/settings.cfg` on every panel switch and every volume step. On boot the saved `panel` is restored, including the PIANO slot (`== boardPaths.size()`); anything larger falls back to 0.
- **Partition**: must use `default_8MB.csv` or the device will not boot

## Language
Communicate with the user in English.
