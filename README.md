# Waveshare 12.48 inch E-Paper on Arduino UNO R4 WiFi

This project drives a Waveshare 12.48 inch tri-color E-Paper display (black, white, red) from an Arduino UNO R4 WiFi.

This repository is a renamed copy of `waveshare-arduino-content` and is now maintained as `waveshare-arduino-wifi`.

## Recent Updates (2026-06-09)

- Added WiFi boot connection using credentials from `secrets.h`.
- Added `STATUS=IP` command to show current local IP address in the status bar.
- Added styled `CONTENT` markup with `_text_` rendering in red.
- Added styled `CONTENT` markup with `|text|` rendering bold (double draw with horizontal offset).
- Added styled `CONTENT` markup where `\n` forces a line break.
- Added `secrets-example.h` and `.gitignore` workflow for safe GitHub usage without exposing local credentials.

## Current Functionality

The sketch currently provides:

- Full-screen tri-color render cycle using `EPD_12in48B`
- White background with a two-layer title:
    - red title offset (drop-shadow effect)
    - black title on top
- Divider line below the title bar
- Main content area that shows either:
    - centered Waveshare logo (`CONTENT=LOGO`), or
    - centered wrapped text (`CONTENT=<text>`)
- Bottom status bar with left-aligned status text
- Full refresh followed by a settle delay and panel sleep

Default content at boot:

- Title: `Waveshare`
- Content: centered logo
- Status: `webwings.nl 2026`

## Serial Command Interface

After boot, open Serial Monitor at `115200` baud and send commands:

- `TITLE=<text>`: update title and refresh display
- `CONTENT=<text>`: update body text and refresh display
- `CONTENT=LOGO`: switch body back to centered logo
- `STATUS=<text>`: update status bar and refresh display
- `STATUS=IP`: set status bar to the current local WiFi IP
- `REFRESH`: redraw display with current values
- `HELP`: print command list

Notes:

- Commands are processed on newline.
- "No line ending" mode is also supported via idle timeout parsing.
- `CONTENT=<text>` supports up to 256 characters (longer input is truncated).
- Text between underscores in `CONTENT` is rendered in red (example: `CONTENT=Dit is _rood_ en dit is zwart`).
- Text between pipes in `CONTENT` is rendered bold by drawing twice with horizontal offset (example: `CONTENT=Dit is |vet|`).
- Use `\n` in `CONTENT` for an explicit line break (example: `CONTENT=Regel 1\nRegel 2`).

## Hardware

- Arduino UNO R4 WiFi
- Waveshare 12.48 inch E-Paper module
- External SRAM chips as required by the Waveshare 12in48 library/wiring
- Stable power source (this panel is sensitive to supply dips)

## Software Setup

1. Install Arduino board package for UNO R4.
2. Install the Waveshare 12in48 library in your Arduino libraries folder.
3. Open `waveshare-arduino-wifi.ino` in Arduino IDE or compile with `arduino-cli`.

## WiFi Credentials

For WiFi functionality, this project uses a local `secrets.h` file with your network credentials.

1. Copy `secrets-example.h` to `secrets.h`.
2. Set `WIFI_SSID` and `WIFI_PASSWORD` in `secrets.h`.
3. Keep `secrets.h` local only.

`secrets.h` is ignored by Git via `.gitignore`, so it is safe to keep your real credentials out of GitHub.

## Working Driver Variant

For this hardware combination, the sketch uses the `EPD_12in48B` driver variant.

## Pin Mapping

The library pin mapping for non-ESP32 targets (used here) is:

- SPI SCK: D13
- SPI MOSI: D11
- SPI MISO: D12
- EPD M1 CS: D2
- EPD S1 CS: D3
- EPD M2 CS: A4
- EPD S2 CS: A0
- EPD M1S1 DC: D6
- EPD M2S2 DC: A3
- EPD M1S1 RST: D5
- EPD M2S2 RST: A2
- EPD M1 BUSY: D4
- EPD S1 BUSY: D7
- EPD M2 BUSY: A1
- EPD S2 BUSY: A5
- SRAM CS1: D10
- SRAM CS2: D9
- SRAM CS3: D8

## Build and Upload

Compile:

    arduino-cli compile -b arduino:renesas_uno:unor4wifi waveshare-arduino-wifi.ino

Upload (replace port):

    arduino-cli upload -b arduino:renesas_uno:unor4wifi -p /dev/tty.usbmodemXXXX waveshare-arduino-wifi.ino

## Runtime Notes

- Large tri-color updates are slow.
- A full refresh can take significant time.
- The sketch includes a settle delay before sleep (`kSettleBeforeSleepMs`) to avoid incomplete final phases.

## Diagnostics Mode

The sketch contains a diagnostics switch:

- `kDiagnosticMode = false` for normal use
- `kDiagnosticMode = true` for verbose serial logs, BUSY pin snapshots, and SRAM sanity checks

## Troubleshooting

If output is unstable:

- Verify wiring carefully, especially BUSY and all CS lines.
- Keep cable lengths short.
- Ensure power is stable during refresh.
- Keep the selected driver variant as `EPD_12in48B`.
- If needed, increase settle delay before sleep.

If upload fails with "serial port busy":

- Close any active serial monitor/terminal using the board port and retry upload.

## Repository Contents

- `waveshare-arduino-wifi.ino`: main sketch
- `secrets-example.h`: template for local WiFi credentials
- `.gitignore`: ignores local secrets and OS-specific files
- `README.md`: setup, runtime behavior, and serial command usage
