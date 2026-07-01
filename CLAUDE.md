# claude-desktop-buddy-c6

Port of [anthropics/claude-desktop-buddy](https://github.com/anthropics/claude-desktop-buddy)
(M5StickCPlus) to the **Waveshare ESP32-C6-LCD-1.47** board. Upstream clone for
reference lives in `../upstream/` (read-only; do not edit).

Goal: feature parity with upstream over the BLE Nordic UART protocol so a real
Claude Desktop (developer mode) pairs with this device. Stop condition is
milestone M7: compiles, uploads, runs, NUS exposed correctly, pet states cycle
on JSON input, ASCII + one bundled GIF pack render. Final UX test with Claude
Desktop is done manually by Shai.

## Hardware truth table (verified, trust this over the wiki)

| Thing | Value |
|---|---|
| Chip | ESP32-C6FH8 — **8 MB embedded flash**. Waveshare wiki says 4 MB; that is WRONG for this SKU. Never shrink partitions back to 4 MB. |
| Serial port | **COM12** (native USB Serial/JTAG, VID:PID 303A:1001, serial = MAC 10:51:DB:3B:04:E4). No reset dance. |
| LCD | ST7789 172x320 portrait, 262K colors. MOSI=6 SCLK=7 CS=14 DC=15 RST=21 BL=22 (active-high). **Column offset 34** ((240-172)/2) — configured once in LovyanGFX `Panel_ST7789` config (`offset_x=34`); without it rendering is truncated + shifted. |
| RGB LED | WS2812 on **GPIO8** — boot-strap pin, never drive before `setup()` runs. Use Arduino-ESP32 3.x built-in `rgbLedWrite(pin,r,g,b)` (the current name of `neopixelWrite`). |
| microSD | SPI, shares MOSI=6/SCLK=7 with LCD, MISO=5, CS=4. Optional, not used yet — but `bus_shared=true` in LovyanGFX config. |
| Buttons | BOOT (GPIO9, pressed=LOW, internal pull-up) + RESET only. No side buttons, no IMU, no PMIC/battery gauge, no buzzer, no RTC chip. |

## Toolchain (decided, do not re-litigate)

- PlatformIO + **pioarduino** fork: `platform = https://github.com/pioarduino/platform-espressif32.git#develop`
  (upstream platform-espressif32 lacks Arduino-ESP32 3.x, required for C6).
- `board = esp32-c6-devkitc-1`, `framework = arduino`, flash overridden to 8MB,
  custom `partitions.csv` (nvs 24K / otadata 8K / app0 2.5M / app1 2.5M / littlefs 2.94M — littlefs fills to exactly 8MB, spec said "2.8MB").
- Libraries: LovyanGFX (display), h2zero/NimBLE-Arduino 2.x (C6 Arduino is NimBLE-only),
  bitbank2/AnimatedGIF ^2.1.1 (same as upstream), bblanchon/ArduinoJson ^7 (same as
  upstream — not in the original library list but upstream depends on it), LittleFS +
  Preferences (built into core).

## Upstream protocol — REFERENCE.md summary + things NOT in REFERENCE.md

Transport: BLE Nordic UART Service. Service `6e400001-b5a3-f393-e0a9-e50e24dcca9e`,
RX (desktop→device, write) `...0002...`, TX (device→desktop, notify) `...0003...`.
Advertise name `Claude-XXXX` (last 2 BT MAC bytes). Everything is UTF-8 JSON, one
object per line, `\n` terminated; accumulate bytes to `\n` then parse (only lines
starting with `{`). **Upstream feeds the same parser from USB Serial too** — that is
our autonomous test path (inject JSON on COM12).

Messages desktop → device:
- Heartbeat snapshot (on change + 10s keepalive): `total`, `running`, `waiting`,
  `msg`, `entries[]` (newest LAST in upstream's rendering assumption; `lines[n-1]`
  compared against `msg`), `tokens` (cumulative since bridge start), `tokens_today`,
  `prompt{id,tool,hint}` (present only while a decision is pending; absence clears it).
  **Undocumented**: `completed` (bool) → `recentlyCompleted` → celebrate state.
  No snapshot for 30s = connection dead (`dataConnected()`).
- Turn events `{"evt":"turn","role":...,"content":[...]}` — upstream ignores these
  (no handler), so we ignore them too.
- One-shot on connect: `{"time":[epoch_sec,tz_offset_sec]}` (upstream sets RTC to
  *local* time; we settimeofday(epoch+offset) and read via gmtime_r → local),
  `{"cmd":"owner","name":"Felix"}`.
- Commands (all expect ack `{"ack":"<cmd>","ok":bool,"n":N}` written to BOTH Serial
  and BLE): `status`, `name`, `owner`, `unpair`, and **undocumented `species`**
  (`{"cmd":"species","idx":N}`, idx 0xFF = GIF mode; persists to NVS key `species`).
- Folder push: `char_begin{name,total}` → per file: `file{path,size}` →
  `chunk{d:<base64>}`* → `file_end` → ... → `char_end`. chunk ack carries `n` =
  bytes written so far in current file; file_end ack `n` = final size. char_begin
  does a fit-check (`total+4096 > free+reclaimable` → `ok:false, error:"need NK, have NK", n=available`),
  then wipes ALL of `/characters/` (one character at a time policy). Files go to
  `/characters/<name>/<path>`. base64 decoded via mbedtls, ≤300 bytes per chunk
  buffer (desktop sends 256-byte chunks). REFERENCE.md says validate `path` against
  `../`+absolute — upstream doesn't actually do it; WE DO.

Messages device → desktop:
- `{"cmd":"permission","id":"<prompt.id>","decision":"once"|"deny"}` on button press.
- Status ack data (poll ~2s from the Hardware Buddy window): upstream sends
  `name, owner, sec, bat{pct,mV,mA,usb}, sys{up,heap,fsFree,fsTotal}, stats{appr,deny,vel,nap,lvl}`.
  `owner`, `fsFree`, `fsTotal` are NOT in REFERENCE.md but upstream sends them.
  We omit `bat` entirely (no PMIC; REFERENCE.md allows omitting).

Security (per REFERENCE.md + upstream impl): LE Secure Connections + MITM + bonding,
IO capability DisplayOnly (device shows 6-digit passkey, user types it on desktop),
NUS characteristics encrypted-only. `sec:true` in status once encrypted. `unpair`
erases bonds. NimBLE mapping: `setSecurityAuth(true,true,true)`,
`BLE_HS_IO_DISPLAY_ONLY`, `READ_ENC|READ_AUTHEN` / `WRITE_ENC|WRITE_AUTHEN` flags.
NimBLE inversion vs Bluedroid: Bluedroid *notifies* us of the stack-chosen passkey;
NimBLE *asks us* (`onPassKeyDisplay()`) — we generate a random 6-digit and render it.

## State machine (exact upstream semantics, from src/main.cpp)

`PersonaState { sleep, idle, busy, attention, celebrate, dizzy, heart }` — enum order
is the manifest/state-name order everywhere.

`derive(tama)` upstream, verbatim:
1. `!connected` → **IDLE** (yes, IDLE — the README table saying "sleep = not connected"
   is a simplification; upstream's sleep visuals come from screen-off + a 12s forced
   P_SLEEP "wake-up transition" after button wake, and from clock-mode schedules)
2. `waiting > 0` → ATTENTION
3. `recentlyCompleted` → CELEBRATE
4. `running >= 3` → BUSY (note: >= 3, not > 0!)
5. else IDLE

One-shots override `activeState` for a duration: level-up → CELEBRATE 3000ms,
approval in <5s → HEART 2000ms, (upstream shake → DIZZY 2000ms — replaced here, see deltas).
Prompt arrival: force approval screen, wake, reset `responseSent`.

**Our delta**: rule 1 becomes SLEEP, BLE-connection-driven — no client
(`!bleConnected()` and no live data) for 30s → SLEEP + backlight dim; wake on BLE
connect/data. Sleep time feeds the `nap` stat (upstream napped on face-down IMU).

## Stats/NVS (namespace `buddy`, exact upstream keys)

`nap`(U32 s) `appr`(U16) `deny`(U16) `vidx`,`vcnt`(U8) `lvl`(U8) `tok`(U32)
`vel`(bytes, 8xU16 ring of seconds-to-respond) `petname` `owner` `species`(U8, 0xFF=GIF)
plus settings `s_snd s_bt s_wifi s_led s_hud s_crot` (we only really use `s_led`, `s_hud`).
Level = tokens/50K, fed pips = (tokens%50K)/5K. Token deltas from cumulative bridge
counter with first-sight latch + restart resync (see upstream stats.h — ported as-is).
Save on events only (approval/denial/nap-end/level-up), never on a timer (NVS wear).
Mood tier from median velocity + denial ratio; energy 3/5 at boot, 5/5 after nap,
-1 per 2h awake.

## ASCII pets

18 species (capybara duck goose blob cat dragon octopus owl penguin turtle snail
ghost axolotl cactus robot rabbit mushroom chonk — registry order matters, `species`
NVS index points into it). Each species file: namespace with 7 `void fn(uint32_t t)`
renderers + `Species` struct {name, bodyColor, states[7]}. They draw ONLY through
`buddyPrint*` helpers (they declare `extern TFT_eSprite spr;` but never use it →
ports are a mechanical include-swap). Tick = 200ms (5fps), redraw gated on tick or
state/species change. Geometry: X_CENTER=86 (was 67), CANVAS_W=172 (was 135),
Y_BASE=30, CHAR 6x8, scale 2x on home screen. Art is space-padded; at 2x helpers
trim + re-center per line.

## GIF characters

`/characters/<name>/manifest.json` + 96px-wide GIFs on LittleFS. Manifest: `name`,
`colors{body,bg,text,textDim,ink}` (hex → RGB565), `states{...}` each a filename or
array (arrays = idle carousel), optional `"mode":"text"` alternative (frames+delay).
Playback rules that matter (ported from upstream character.cpp):
- `gif.begin(LITTLE_ENDIAN_PIXELS)`; draw via per-pixel palette lookup, transparent
  → `pal.bg` (GIFs are unoptimized full-frame; no disposal handling).
- Single-GIF states FREEZE on last frame instead of re-opening (LittleFS open +
  header decode is a blocking multi-ms burst that starved BT upstream).
- Multi-GIF states: loop current variant until 5000ms dwell, then 800ms pause, then
  rotate to next.
- On install (`char_end`): reload character, GIF mode on, `species=0xFF`.
- Boot: scan `/characters/` for first directory if no name given.
Bundled starter pack: upstream's bufo (~555KB) in `data/characters/bufo/` →
`pio run -t uploadfs`.

## Layout (172x320 vs upstream 135x240)

- y 0..140: character region (GIF centered, 96w sprites; ASCII at 2x centered on x=86)
- y ~142..154: status strip (new space): state + link + sessions summary
- y ~156..320: recent-message area (wrapped transcript lines, more than upstream's 3);
  approval panel overrides the bottom ~90px when a prompt is pending; passkey screen
  overrides everything during pairing.
- Full-screen 172x320 16-bit LGFX_Sprite (110KB — fits C6's RAM with NimBLE; check
  free heap in status output).

## Behavioral deltas from upstream (intended, don't "fix" back)

1. **RGB LED** (new capability): sleep/idle=off, busy=blue pulse, attention=amber
   blink, celebrate=green sweep, dizzy=red flicker, heart=soft pink pulse (heart cue
   chosen by us in the same spirit; not in the spec list). Kept dim (~25% max).
   `s_led` setting gates it, on by default.
2. **BOOT-button UX** (only button): prompt pending → short press = approve
   ("once"), long press ≥800ms = deny. No prompt → short press = next pet
   (upstream menu "ascii pet" cycle: GIF→species0→...→speciesN→GIF), long press =
   demo mode toggle (upstream menu "demo"). No menu system, no info/pet pages.
3. **Sleep is BLE-driven** (no IMU): see state machine above. No shake→dizzy (dizzy
   still reachable via demo mode + clock schedules are dropped since no RTC/no
   charging detection... actually: no clock mode at all — it depended on AXP charge
   detection + IMU orientation).
4. Dropped: buzzer beeps (no speaker), battery telemetry (no PMIC), screen-off
   power button (no such button; backlight dims in sleep instead), landscape clock,
   menu/settings/info/pet screens.

## Source map (ours ← upstream)

    src/main.cpp                     ← main.cpp (loop, FSM glue, HUD/approval/passkey rendering)
    src/ble_bridge.{h,cpp}           ← ble_bridge.cpp (NimBLE NUS) + data.h (line buffer,
                                       JSON dispatch) + xfer.h (folder push, cmd acks)
    src/display.{h,cpp}              ← new: LovyanGFX LGFX class w/ offset_x=34, sprite owner
    src/state.{h,cpp}                ← TamaState + PersonaState + derive() + one-shots
    src/led.{h,cpp}                  ← new: WS2812 cues per state
    src/input.{h,cpp}                ← new: BOOT short/long press
    src/stats.{h,cpp}                ← stats.h (de-headerized)
    src/characters/ascii_pets.{h,cpp}← buddy.{h,cpp} + buddy_common.h (registry+helpers)
    src/characters/buddies/*.cpp     ← buddies/*.cpp verbatim (include swap only)
    src/characters/gif_player.{h,cpp}← character.{h,cpp} (AnimatedGIF+LittleFS)
    src/characters/manifest.{h,cpp}  ← manifest parsing split out of character.cpp
    data/characters/bufo/            ← characters/bufo/ (starter pack)
    tools/test_serial.py             ← adapted for COM port arg (state cycling test)
    tools/test_xfer.py               ← adapted for COM port arg (folder push test)

## Verification playbook (autonomous, no Claude Desktop needed)

- Build: `pio run` in this directory. Upload: `pio run -t upload`. FS: `pio run -t uploadfs`.
- Monitor: `pio run -t monitor` or `pio device monitor -p COM12 -b 115200`.
  Native USB CDC: prints before the host attaches are dropped — firmware keeps
  emitting periodic liveness lines, don't panic about a missed boot banner.
- State machine: `python tools/test_serial.py COM12` cycles heartbeat JSONs; watch
  `[state]` transitions in the log (serial port is shared — stop the monitor first).
- Folder push: `python tools/test_xfer.py COM12 ../upstream/characters/bufo bufo`.
- BLE: serial log prints advertising name; radio-side verification needs nRF Connect
  (manual) — code parity + advertise logs are the autonomous proxy.

## Milestone log

- [x] M1 — hello world + RGB LED cycle: verified 2026-07-01. Serial liveness lines
  + LED cycle running on device; free heap at idle ≈ 427KB (plenty for the 110KB
  full-screen sprite). Board USB-dropped once mid-milestone and returned as COM13 —
  hence VID-based port autodetection in tools/auto_port.py.
- [x] M2 — display bring-up: code complete + running 2026-07-01 (sprite allocs
  111,952B, pattern pushed, no hang; 311KB heap free after). **Visual check
  pending Shai**: border on 4 edges = offset ok; bar colors match labels =
  rgb_order/invert ok. Config gamble documented: `invert=true, rgb_order=false`.
- [ ] M3 — NUS echo (advertise `Claude-XXXX`, echo RX→TX with `\r\n`)
- [ ] M4 — wire protocol + FSM + ASCII pets + LED per state
- [ ] M6 — GIF playback of bundled bufo from LittleFS (spec numbering has no M5)
- [ ] M7 — stats + BOOT input + LED polish + this file finalized

## Gotchas / decisions log

- 2026-07-01 Project scaffolded. littlefs partition sized 0x2F0000 (2.94MB) to fill
  8MB exactly rather than the spec's approximate "2.8MB" — more room for packs.
- nvs is 20K, not the spec'd 24K: otadata (fixed 8K) must end at 0x10000 where the
  64K-aligned app0 starts, so nvs+otadata share 0x9000..0x10000 = 28K. 24K nvs +
  8K otadata literally cannot fit before 0x10000 (first build failed on overlap).
- `monitor_dtr=0 / monitor_rts=0` in platformio.ini: pyserial asserting DTR/RTS on
  open can reset a USB-Serial/JTAG chip into the bootloader.
- Upstream `entries` are documented "newest first" in REFERENCE.md but the HUD code
  compares `lines[n-1]` to `msg` for change detection and renders the LAST line as
  freshest — i.e. code treats newest as LAST. We mirror code, not doc (guardrail:
  src/ behavior wins). Cited ambiguity per instructions.
- `spr` extern in species files is vestigial upstream (never used) — dropped in port.
