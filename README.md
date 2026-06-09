# Waveshare 12.48 inch E-Paper on Arduino UNO R4 WiFi

This project drives a Waveshare 12.48 inch tri-color E-Paper display (black, white, red) from an Arduino UNO R4 WiFi.

This repository is a renamed copy of `waveshare-arduino-content` and is now maintained as `waveshare-arduino-wifi`.

## Recent Updates (2026-06-09)

- Added `config.h` for central startup configuration:
    - `MODE=AP` or `MODE=STA`
    - `TITLE`, `CONTENT`, `STATUS` display defaults
- Added dual network mode support: Access Point (AP) and normal WiFi client (STA).
- Added serial commands to switch network mode at runtime: `WIFI=AP`, `WIFI=STA`, `WIFI=MODE`.
- Added WiFi boot connection using credentials from `secrets.h`.
- Added `STATUS=IP` command to show current local IP address in the status bar.
- Added automatic status text `webwings.nl 2026 (AP: <ip>)` or `webwings.nl 2026 (STA: <ip>)` on `CONTENT` updates only (not during `setup()`).
- Added styled `CONTENT` markup with `_text_` rendering in red.
- Added styled `CONTENT` markup with `|text|` rendering extra bold.
- Added styled `CONTENT` markup where `\n` forces a line break.
- Added `secrets-example.h` and `.gitignore` workflow for safe GitHub usage without exposing local credentials.

## Current Functionality

The sketch currently provides:

- Full-screen tri-color render cycle using `EPD_12in48B`
- White background with a title effect:
    - black shadow offsets: x-2, x+2, y-1, y+2 and diagonals
    - red overlay (Font64): x-1, x+2, y+1, y-1 plus base text
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
- `STATUS=IP` shows `WiFi disconnected` when no WiFi connection is available.
- Text between underscores in `CONTENT` is rendered in red (example: `CONTENT=Dit is _rood_ en dit is zwart`).
- Text between pipes in `CONTENT` is rendered extra bold with overdraw on x+1, x-1, y+1 and y-1 (example: `CONTENT=Dit is |extra vet|`).
- Use `\n` in `CONTENT` for an explicit line break (example: `CONTENT=Regel 1\nRegel 2`).

## Web Interface

When network mode is active (AP or STA), the sketch starts a minimal web server on port 80.

1. Open the board IP in your browser (shown on Serial as `Web UI ready (<mode>): http://<ip>`).
2. Fill in `Titel` and/or `Inhoud`.
3. Click `POST`.

On POST, the sketch applies the values exactly as if `TITLE=<text>` and `CONTENT=<text>` were sent over Serial, then refreshes the display.

When `CONTENT` is updated (and no explicit `status` field is sent), the status bar is automatically set to:

- `webwings.nl 2026 (AP: <board-ip>)` in AP mode
- `webwings.nl 2026 (STA: <board-ip>)` in STA mode

This automatic AP/STA status update is not performed during `setup()` boot rendering.

The web page also shows a mode-switch button:

- In AP mode: button to switch to STA mode.
- In STA mode: button to switch to AP mode.

If switching fails (for example invalid/missing STA credentials), the firmware keeps the previous mode so the device remains reachable.

## Network Modes (AP and STA)

The firmware supports two WiFi modes:

- `AP` mode: the board creates its own WiFi network and hosts the web UI/API directly.
- `STA` mode: the board connects to your existing router/network and hosts the web UI/API on its local IP.

Default boot behavior is configured in `config.h` via `MODE=AP` or `MODE=STA`.

Serial commands to switch mode at runtime:

- `WIFI=AP`: switch to Access Point mode.
- `WIFI=STA`: switch to normal WiFi client mode.
- `WIFI=MODE`: print the active mode (`AP` or `STA`).

AP defaults (when not overridden in `secrets.h`):

- SSID: `Waveshare-AP`
- Password: `waveshare123`

AP mode includes a captive-portal style redirect:

- In AP mode, unknown `GET` paths are redirected to `http://<board-ip>/`.
- On many phones/laptops this triggers the automatic sign-in page after joining the AP, so users land directly on the edit page.

## Startup Config (config.h)

Use `config.h` to set network startup mode and display defaults:

```c
#define MODE AP
#define TITLE "Waveshare"
#define CONTENT "LOGO"
#define STATUS "webwings.nl 2026"
```

Notes:

- `MODE AP` boots as Access Point.
- `MODE STA` boots as normal router/client WiFi mode.
- In `MODE AP`, the boot status bar is set to `webwings.nl 2026 (Access Point Mode)`.
- Boot rendering keeps configured/default status text; AP/STA `<ip>` status is only auto-set on runtime `CONTENT` updates.
- `CONTENT "LOGO"` shows the centered logo by default.
- Any other `CONTENT` value is used as default content text.

### HTTP POST From Another Device

For automation from another device in the same network, use:

- `POST /api/update`
- `Content-Type: application/x-www-form-urlencoded`
- Body fields: `title`, `content` and/or `status` (all optional, at least one required)
- If `status` is sent empty (`status=`), the status bar is set to `webwings.nl 2026 (IP:<board-ip>)` when WiFi is connected.
- If WiFi is not connected, empty `status` falls back to `webwings.nl 2026`.

Examples:

```bash
curl -X POST "http://<arduino-ip>/api/update" \
    -H "Content-Type: application/x-www-form-urlencoded" \
    -d "title=Nieuwe Titel"

curl -X POST "http://<arduino-ip>/api/update" \
    -H "Content-Type: application/x-www-form-urlencoded" \
    -d "content=Dit is _rood_ en |vet|"

curl -X POST "http://<arduino-ip>/api/update" \
    -H "Content-Type: application/x-www-form-urlencoded" \
    -d "title=Web Update&content=Regel 1\\nRegel 2&status="

curl -X POST "http://<arduino-ip>/api/update" \
    -H "Content-Type: application/x-www-form-urlencoded" \
    -d "status=Custom status"
```

Python example:

```python
import requests

arduino_url = "http://<arduino-ip>/api/update"

# Update only title
r1 = requests.post(arduino_url, data={"title": "Python Titel"}, timeout=5)
print(r1.status_code, r1.text)

# Update only content
r2 = requests.post(arduino_url, data={"content": "_Rood_ en |vet| via Python"}, timeout=5)
print(r2.status_code, r2.text)

# Reset status to default + current board IP
r3 = requests.post(arduino_url, data={"status": ""}, timeout=5)
print(r3.status_code, r3.text)
```

### CLI Helper Scripts

Two helper scripts are included for quick testing from another device:

- `example-post.py`: interactive prompts for `Titel`, `Inhoud`, and `Status`, then sends a fire-and-forget POST to `/api/update`.
- `example-json.py`: accepts JSON input via `--json`, `--json-file`, or `--stdin` and posts `title`, `content`, and/or `status`.

Examples:

```bash
python3 example-post.py

python3 example-json.py --json '{"title":"Hallo","content":"_Rood_","status":""}'

echo '{"title":"CLI","content":"Test","status":""}' | python3 example-json.py --stdin
```

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

For WiFi functionality, this project uses a local `secrets.h` file.

1. Copy `secrets-example.h` to `secrets.h`.
2. Set `WIFI_SSID` and `WIFI_PASSWORD` for STA mode in `secrets.h`.
3. Optionally set `AP_SSID` and `AP_PASSWORD` to override AP defaults.
4. Set `WEB_TITLE` to control the page title/header shown in the web UI.
5. Keep `secrets.h` local only.

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
- `config.h`: startup mode and default display values
- `example-post.py`: interactive fire-and-forget API client
- `example-json.py`: JSON-based CLI API client
- `secrets-example.h`: template for local WiFi credentials
- `.gitignore`: ignores local secrets and OS-specific files
- `README.md`: setup, runtime behavior, and serial command usage
