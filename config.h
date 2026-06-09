#ifndef CONFIG_H
#define CONFIG_H

// Network mode selection.
// Use: MODE=AP for board-hosted Access Point, or MODE=STA for router/client mode.
#define AP 1
#define STA 2
#define MODE AP

// Display defaults at boot.
// CONTENT="LOGO" shows the centered logo in the content area.
#define TITLE "Waveshare"
#define CONTENT "LOGO"
#define STATUS "webwings.nl 2026"

// Font preset for all text regions (title, content, status).
// 0 = default library fonts, 1 = Google Space Mono, 2 = Google Manrope,
// 3 = Google Anton, 4 = Google Permanent Marker.
#define CONTENT_FONT_DEFAULT 0
#define CONTENT_FONT_SPACE_MONO 1
#define CONTENT_FONT_MANROPE 2
#define CONTENT_FONT_ANTON 3
#define CONTENT_FONT_PERMANENT_MARKER 4
#define CONTENT_FONT CONTENT_FONT_DEFAULT

// Per-preset text scaling (applies to title/content/status for the selected preset).
// Decimal values are supported, for example: 1.25f, 1.5f, 1.8f.
// Keep values >= 1.0f.
#define CONTENT_FONT_SCALE_DEFAULT 1.0f
#define CONTENT_FONT_SCALE_SPACE_MONO 1.2f
#define CONTENT_FONT_SCALE_MANROPE 1.0f
#define CONTENT_FONT_SCALE_ANTON 1.5f
#define CONTENT_FONT_SCALE_PERMANENT_MARKER 2.0f

#endif  // CONFIG_H
