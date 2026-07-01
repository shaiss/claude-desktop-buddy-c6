# claude-desktop-buddy-c6

A port of Anthropic's [claude-desktop-buddy](https://github.com/anthropics/claude-desktop-buddy)
to the **Waveshare ESP32-C6-LCD-1.47** — a $10 board with a 172×320 IPS
display, an RGB LED, and native USB.

It's a desk pet that lives off your Claude Desktop sessions over BLE. It
sleeps when nothing's happening, wakes when you start working, gets visibly
impatient when a permission prompt is waiting — and you approve or deny
right from the device's BOOT button. Feature parity with upstream: the same
Nordic UART wire protocol, the same 18 ASCII species × 7 animations, the
same GIF character packs (one bundled), the same NVS-backed stats.

Upstream's [CONTRIBUTING.md](https://github.com/anthropics/claude-desktop-buddy/blob/main/CONTRIBUTING.md)
says "the best contribution is a fork" — this is that fork.

## Hardware

Waveshare **ESP32-C6-LCD-1.47**. One important correction to the vendor
wiki: this SKU's ESP32-C6FH8 has **8 MB embedded flash**, not 4 MB. The
partition table here uses all of it (OTA A/B slots + a 2.94 MB LittleFS
for character packs).

| Peripheral | Pins |
| --- | --- |
| ST7789 LCD (172×320) | MOSI 6, SCLK 7, CS 14, DC 15, RST 21, BL 22 (active-high) |
| WS2812 RGB LED | GPIO 8 (boot-strap pin — never driven before `setup()`) |
| BOOT button | GPIO 9 (the only user button) |
| microSD (unused, shares SPI) | MISO 5, CS 4 |

The 172-wide panel sits centered in the ST7789's 240-column RAM, so the
LovyanGFX config carries `offset_x = 34`. Without it everything renders
truncated and shifted.

## Flashing

Install [PlatformIO Core](https://docs.platformio.org/en/latest/core/installation/),
plug the board in (native USB — no driver, no boot-button dance), then:

```bash
pio run -t upload      # firmware
pio run -t uploadfs    # LittleFS image with the bundled bufo character
```

The serial port is auto-detected by USB VID (`303A:1001`). First build
downloads the [pioarduino](https://github.com/pioarduino/platform-espressif32)
platform — upstream platform-espressif32 doesn't ship the Arduino-ESP32 3.x
core that the C6 requires.

## Pairing with Claude Desktop

1. In Claude for macOS/Windows: **Help → Troubleshooting → Enable Developer Mode**
2. **Developer → Open Hardware Buddy…** → **Connect**, pick `Claude-XXXX`
3. Type the 6-digit passkey shown on the device's screen

The link uses LE Secure Connections bonding; reconnects are automatic and
don't re-prompt. The Hardware Buddy window's drop target streams character
pack folders to the device, same as upstream.

## Controls

Only two buttons exist: BOOT and RESET. So:

|              | Prompt pending | Otherwise |
| ------------ | -------------- | --------- |
| **BOOT** tap | **approve** | next pet (GIF → 18 ASCII species → GIF) |
| **BOOT** hold (≥ 0.8 s) | **deny** | demo mode on/off |

## The seven states

| State | Trigger | LED |
| --- | --- | --- |
| `sleep` | no BLE client / no data for ~30 s | off |
| `idle` | connected, nothing urgent | off |
| `busy` | 3+ sessions generating | blue pulse |
| `attention` | approval pending | amber blink |
| `celebrate` | level up (every 50 K tokens) or turn completed | green sweep |
| `dizzy` | demo mode carousel | red flicker |
| `heart` | approved in under 5 s | pink pulse |

## Differences from the M5StickCPlus original

Necessitated by the hardware (no IMU, no side buttons, no PMIC, no buzzer):

- **Sleep is BLE-connection-driven** instead of screen-off timers and
  face-down naps. Sleep time still feeds the `nap` stat; the backlight dims.
- **BOOT button replaces the A/B buttons** (mapping above). The menu,
  settings, info and pet-stats screens are gone; everything they set is
  still reachable over the wire protocol (`status`, `species`, `name`, …).
- **No shake-to-dizzy, no landscape clock, no battery telemetry** (the
  status ack simply omits `bat`, which the protocol allows).
- **The RGB LED is new** — upstream had a single red LED that blinked on
  attention; this board's WS2812 gets a cue per state (kept dim).
- The taller screen adds a status strip (state / sessions / link / level)
  and shows more transcript lines than upstream's three.

Everything on the wire is unchanged — a Claude Desktop that can talk to an
upstream buddy talks to this one.

## Character packs

Identical format to upstream: a folder with `manifest.json` and 96 px-wide
GIFs, under 1.8 MB total. Drag it onto the Hardware Buddy drop target, or
skip the radio while iterating:

```bash
python tools/test_xfer.py            # stream a pack over USB serial
python upstream/tools/prep_character.py   # (in the upstream repo) resize art
```

The bundled starter is upstream's bufo (third-party art from
[bufo.zone](https://bufo.zone) — see LICENSE).

## Development

`CLAUDE.md` is the living engineering log: architecture, protocol notes
beyond REFERENCE.md, and the gotchas that cost time (LittleFS partition
labels, USB-CDC RX buffer overflows, the wiki's wrong flash size). The
`tools/` scripts verify everything over USB serial without a BLE client:

| Script | What it proves |
| --- | --- |
| `test_protocol.py` | 13 checks: state machine, acks, level-up, sleep/wake |
| `test_input.py` | 10 checks: approve/deny path, stats, pet cycling |
| `test_xfer.py` | folder-push install end to end |
| `test_ble_scan.py` | advertisement, GATT, encryption gate (host BT adapter) |
| `monitor_once.py` | non-interactive serial capture |

Lines starting with `!` on USB serial (`!short`, `!long`) inject synthetic
button presses — a test seam, invisible to the JSON protocol.

## Credits

- Original firmware and protocol: [Anthropic's claude-desktop-buddy](https://github.com/anthropics/claude-desktop-buddy)
  by Felix Rieseberg — the 18 ASCII species and the entire wire protocol
  are ported from there.
- bufo artwork: the [bufo.zone](https://bufo.zone) community emoji set.
- Port developed with [Claude Code](https://claude.com/claude-code).

MIT — see [LICENSE](LICENSE) (bufo artwork excepted).
