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

// Content font preset (Font48 height) for the main content area.
// 0 = default library font, 1 = Google Space Mono, 2 = Google Manrope, 3 = Google Anton.
#define CONTENT_FONT_DEFAULT 0
#define CONTENT_FONT_SPACE_MONO 1
#define CONTENT_FONT_MANROPE 2
#define CONTENT_FONT_ANTON 3
#define CONTENT_FONT CONTENT_FONT_DEFAULT

#endif  // CONFIG_H
