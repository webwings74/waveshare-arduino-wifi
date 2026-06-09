#include "DEV_Config.h"
#include "EPD_12in48b.h"
#include "GUI_Paint.h"
#include "SRAM_23LC.h"
#include "imagedata.h"
#include "secrets.h"
#include <WiFiS3.h>
#include <string.h>

static const bool kDiagnosticMode = false;
static const UWORD kDisplayWidth = 1304;
static const UWORD kDisplayHeight = 984;
static const UWORD kLogoSize = 240;
static const uint32_t kSettleBeforeSleepMs = 12000;
static const size_t kTitleTextMax = 64;
static const size_t kContentTextMax = 257;
static const size_t kStatusTextMax = 64;
static const size_t kSerialLineMax = 320;
static const unsigned long kSerialIdleProcessMs = 500;
static const unsigned long kWifiConnectTimeoutMs = 30000;
static const unsigned long kWifiConnectPollMs = 500;
static const UWORD kBoldOffsetPx = 2;

static char gTitleText[kTitleTextMax] = "Waveshare";
static char gContentText[kContentTextMax] = "";
static char gStatusText[kStatusTextMax] = "webwings.nl 2026";
static String gSerialLine;
static unsigned long gLastSerialCharMs = 0;

static void logStatus(const char* msg)
{
    if (!kDiagnosticMode) {
        return;
    }
    Serial.print("[");
    Serial.print(millis());
    Serial.print(" ms] ");
    Serial.println(msg);
}

static void logBusyPins(const char* phase)
{
    if (!kDiagnosticMode) {
        return;
    }
    Serial.print("[");
    Serial.print(millis());
    Serial.print(" ms] ");
    Serial.print(phase);
    Serial.print(" BUSY M1=");
    Serial.print(digitalRead(EPD_M1_BUSY_PIN));
    Serial.print(" S1=");
    Serial.print(digitalRead(EPD_S1_BUSY_PIN));
    Serial.print(" M2=");
    Serial.print(digitalRead(EPD_M2_BUSY_PIN));
    Serial.print(" S2=");
    Serial.println(digitalRead(EPD_S2_BUSY_PIN));
}

static void logSramSanity(void)
{
    if (!kDiagnosticMode) {
        return;
    }

    // Quick SRAM loopback test at a few addresses used by the framebuffer.
    const UDOUBLE a0 = 0;
    const UDOUBLE a1 = 12345;
    const UDOUBLE a2 = 160392;

    SRAM_WriteByte(a0, 0xAA);
    SRAM_WriteByte(a1, 0x55);
    SRAM_WriteByte(a2, 0x3C);

    UBYTE r0 = SRAM_ReadByte(a0);
    UBYTE r1 = SRAM_ReadByte(a1);
    UBYTE r2 = SRAM_ReadByte(a2);

    Serial.print("[");
    Serial.print(millis());
    Serial.print(" ms] SRAM test r0=");
    Serial.print(r0, HEX);
    Serial.print(" r1=");
    Serial.print(r1, HEX);
    Serial.print(" r2=");
    Serial.println(r2, HEX);
}

static void printWifiStatus(void)
{
    Serial.print(F("WiFi connected to SSID: "));
    Serial.println(WiFi.SSID());

    IPAddress ip = WiFi.localIP();
    Serial.print(F("IP address: "));
    Serial.println(ip);

    long rssi = WiFi.RSSI();
    Serial.print(F("Signal strength (RSSI): "));
    Serial.print(rssi);
    Serial.println(F(" dBm"));
}

static void connectWifiAfterBootRefresh(void)
{
    if (WiFi.status() == WL_NO_MODULE) {
        Serial.println(F("WiFi module not detected."));
        return;
    }

    if (strcmp(WIFI_SSID, "YOUR_WIFI_SSID") == 0 || strcmp(WIFI_PASSWORD, "YOUR_WIFI_PASSWORD") == 0) {
        Serial.println(F("WiFi credentials not configured in secrets.h."));
        return;
    }

    Serial.print(F("Connecting to WiFi: "));
    Serial.println(WIFI_SSID);

    unsigned long startMs = millis();
    int status = WL_IDLE_STATUS;

    while (status != WL_CONNECTED && (millis() - startMs) < kWifiConnectTimeoutMs) {
        status = WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

        unsigned long waitStartMs = millis();
        while (WiFi.status() != WL_CONNECTED && (millis() - waitStartMs) < 8000) {
            Serial.print('.');
            delay(kWifiConnectPollMs);
        }

        status = WiFi.status();
        if (status != WL_CONNECTED) {
            Serial.println();
            Serial.println(F("WiFi not connected yet, retrying..."));
            WiFi.disconnect();
            delay(500);
        }
    }

    Serial.println();
    if (WiFi.status() == WL_CONNECTED) {
        printWifiStatus();
    } else {
        Serial.println(F("WiFi connection failed (timeout)."));
    }
}

static bool setStatusToCurrentIp(void)
{
    if (WiFi.status() != WL_CONNECTED) {
        copyStringToBuffer(String("WiFi disconnected"), gStatusText, kStatusTextMax);
        return false;
    }

    IPAddress ip = WiFi.localIP();
    String ipText = String(ip[0]) + "." + String(ip[1]) + "." + String(ip[2]) + "." + String(ip[3]);
    copyStringToBuffer(ipText, gStatusText, kStatusTextMax);
    return true;
}

static void copyStringToBuffer(const String& src, char* dst, size_t dstSize)
{
    src.toCharArray(dst, dstSize);
    dst[dstSize - 1] = '\0';
}

static void drawCenteredText(UWORD yTop, UWORD areaHeight, const char* text, sFONT* font, UWORD textColor)
{
    const UWORD textWidth = static_cast<UWORD>(strlen(text) * font->Width);
    const UWORD textHeight = font->Height;
    const UWORD textX = (kDisplayWidth > textWidth) ? (kDisplayWidth - textWidth) / 2 : 0;
    const UWORD textY = yTop + ((areaHeight > textHeight) ? (areaHeight - textHeight) / 2 : 0);

    Paint_DrawString_EN(textX, textY, text, font, WHITE, textColor);
}

static void drawLeftAlignedText(UWORD yTop, UWORD areaHeight, UWORD xLeft, const char* text, sFONT* font, UWORD textColor)
{
    const UWORD textHeight = font->Height;
    const UWORD textY = yTop + ((areaHeight > textHeight) ? (areaHeight - textHeight) / 2 : 0);

    Paint_DrawString_EN(xLeft, textY, text, font, WHITE, textColor);
}

static void drawCenteredWrappedStyledText(UWORD yTop, UWORD areaHeight, UWORD xLeft, UWORD areaWidth, const char* rawText, sFONT* font, bool drawRedSegments)
{
    if (areaWidth < font->Width || areaHeight < font->Height) {
        return;
    }

    const size_t maxCharsPerLine = areaWidth / font->Width;
    const size_t maxLinesInArea = areaHeight / font->Height;
    const size_t kMaxLinesBuffer = 24;
    if (maxCharsPerLine == 0 || maxLinesInArea == 0) {
        return;
    }

    String source = rawText;
    source.trim();
    if (source.length() == 0) {
        return;
    }

    char normalized[kContentTextMax];
    bool redMask[kContentTextMax];
    bool boldMask[kContentTextMax];
    size_t normalizedLen = 0;
    bool inRedSegment = false;
    bool inBoldSegment = false;
    bool prevWasSpace = false;

    for (size_t i = 0; i < static_cast<size_t>(source.length()) && normalizedLen < (kContentTextMax - 1); i++) {
        const char ch = source[i];
        if (ch == '_') {
            inRedSegment = !inRedSegment;
            continue;
        }

        if (ch == '|') {
            inBoldSegment = !inBoldSegment;
            continue;
        }

        if (ch == '\\' && (i + 1) < static_cast<size_t>(source.length()) && source[i + 1] == 'n') {
            while (normalizedLen > 0 && normalized[normalizedLen - 1] == ' ') {
                normalizedLen--;
            }
            if (normalizedLen < (kContentTextMax - 1)) {
                normalized[normalizedLen] = '\n';
                redMask[normalizedLen] = false;
                boldMask[normalizedLen] = false;
                normalizedLen++;
            }
            prevWasSpace = false;
            i++;
            continue;
        }

        const bool isWhitespace = (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n');
        if (isWhitespace) {
            if (ch == '\r' || ch == '\n') {
                while (normalizedLen > 0 && normalized[normalizedLen - 1] == ' ') {
                    normalizedLen--;
                }
                if (normalizedLen < (kContentTextMax - 1)) {
                    normalized[normalizedLen] = '\n';
                    redMask[normalizedLen] = false;
                    boldMask[normalizedLen] = false;
                    normalizedLen++;
                }
                prevWasSpace = false;
                continue;
            }

            if (normalizedLen == 0 || prevWasSpace) {
                continue;
            }

            if (normalized[normalizedLen - 1] == '\n') {
                continue;
            }

            normalized[normalizedLen] = ' ';
            redMask[normalizedLen] = false;
            boldMask[normalizedLen] = false;
            normalizedLen++;
            prevWasSpace = true;
            continue;
        }

        normalized[normalizedLen] = ch;
        redMask[normalizedLen] = inRedSegment;
        boldMask[normalizedLen] = inBoldSegment;
        normalizedLen++;
        prevWasSpace = false;
    }

    if (normalizedLen == 0) {
        return;
    }

    while (normalizedLen > 0 && (normalized[normalizedLen - 1] == ' ' || normalized[normalizedLen - 1] == '\n')) {
        normalizedLen--;
    }
    if (normalizedLen == 0) {
        return;
    }

    normalized[normalizedLen] = '\0';

    size_t lineStarts[kMaxLinesBuffer];
    size_t lineEnds[kMaxLinesBuffer];
    size_t lineCount = 0;
    size_t pos = 0;

    while (pos < normalizedLen && lineCount < kMaxLinesBuffer) {
        if (normalized[pos] == '\n') {
            lineStarts[lineCount] = pos;
            lineEnds[lineCount] = pos;
            lineCount++;
            pos++;
            continue;
        }

        while (pos < normalizedLen && normalized[pos] == ' ') {
            pos++;
        }
        if (pos >= normalizedLen) {
            break;
        }

        if (normalized[pos] == '\n') {
            lineStarts[lineCount] = pos;
            lineEnds[lineCount] = pos;
            lineCount++;
            pos++;
            continue;
        }

        const size_t lineStart = pos;
        size_t lastSpace = static_cast<size_t>(-1);
        size_t taken = 0;

        while (pos < normalizedLen && taken < maxCharsPerLine && normalized[pos] != '\n') {
            if (normalized[pos] == ' ') {
                lastSpace = pos;
            }
            pos++;
            taken++;
        }

        size_t lineEnd = pos;
        if (pos < normalizedLen && normalized[pos] == '\n') {
            // Respect explicit line break from literal "\\n" in CONTENT.
        } else if (pos < normalizedLen && taken == maxCharsPerLine && normalized[pos] != ' ' && lastSpace != static_cast<size_t>(-1) && lastSpace > lineStart) {
            lineEnd = lastSpace;
            pos = lastSpace + 1;
        }

        while (lineEnd > lineStart && normalized[lineEnd - 1] == ' ') {
            lineEnd--;
        }

        lineStarts[lineCount] = lineStart;
        lineEnds[lineCount] = lineEnd;
        lineCount++;

        if (pos < normalizedLen && normalized[pos] == '\n') {
            pos++;
        }
    }

    if (lineCount == 0) {
        return;
    }

    const size_t renderLineCount = (lineCount > maxLinesInArea) ? maxLinesInArea : lineCount;
    const UWORD blockHeight = static_cast<UWORD>(renderLineCount * font->Height);
    const UWORD startY = yTop + ((areaHeight > blockHeight) ? (areaHeight - blockHeight) / 2 : 0);

    for (size_t lineIndex = 0; lineIndex < renderLineCount; lineIndex++) {
        const size_t lineStart = lineStarts[lineIndex];
        const size_t lineEnd = lineEnds[lineIndex];
        const size_t lineLen = lineEnd - lineStart;
        const UWORD lineWidth = static_cast<UWORD>(lineLen * font->Width);
        const UWORD lineX = xLeft + ((areaWidth > lineWidth) ? (areaWidth - lineWidth) / 2 : 0);
        const UWORD lineY = startY + static_cast<UWORD>(lineIndex * font->Height);

        if (lineLen == 0) {
            continue;
        }

        size_t runStart = lineStart;
        while (runStart < lineEnd) {
            const bool runIsRed = redMask[runStart];
            const bool runIsBold = boldMask[runStart];
            size_t runEnd = runStart + 1;
            while (runEnd < lineEnd && redMask[runEnd] == runIsRed && boldMask[runEnd] == runIsBold) {
                runEnd++;
            }

            if (runIsRed == drawRedSegments) {
                const size_t runLen = runEnd - runStart;
                char runText[kContentTextMax];
                memcpy(runText, &normalized[runStart], runLen);
                runText[runLen] = '\0';

                const UWORD runX = lineX + static_cast<UWORD>((runStart - lineStart) * font->Width);
                const UWORD color = drawRedSegments ? RED : BLACK;
                Paint_DrawString_EN(runX, lineY, runText, font, WHITE, color);
                if (runIsBold) {
                    Paint_DrawString_EN(runX + kBoldOffsetPx, lineY, runText, font, WHITE, color);
                }
            }

            runStart = runEnd;
        }
    }
}

static void runDisplayCycle(void)
{
    const UWORD statusLeftPadding = 12;
    const UWORD contentSidePadding = 20;
    const UWORD titleOffsetPx = 2;
    const char* titleText = gTitleText;
    const UWORD titleBarHeight = (kDisplayHeight * 20) / 100;
    const UWORD statusBarHeight = Font24.Height + 12;
    const UWORD contentTop = titleBarHeight;
    const UWORD contentBottom = kDisplayHeight - statusBarHeight;
    const UWORD contentHeight = contentBottom - contentTop;
    const UWORD contentLeft = contentSidePadding;
    const UWORD contentWidth = kDisplayWidth - (contentSidePadding * 2);

    const UWORD titleTextWidth = static_cast<UWORD>(strlen(titleText) * Font64.Width);
    const UWORD titleBaseX = (kDisplayWidth > titleTextWidth) ? (kDisplayWidth - titleTextWidth) / 2 : 0;
    const UWORD titleBaseY = (titleBarHeight > Font64.Height) ? (titleBarHeight - Font64.Height) / 2 : 0;
    const UWORD titleRedX = titleBaseX + titleOffsetPx;
    const UWORD titleRedY = titleBaseY + titleOffsetPx;

    const UWORD logoW = kLogoSize;
    const UWORD logoH = kLogoSize;
    const UWORD logoX = (kDisplayWidth - logoW) / 2;
    const UWORD logoY = contentTop + ((contentHeight > logoH) ? (contentHeight - logoH) / 2 : 0);

    // Re-init module each cycle so refresh works reliably after deep sleep.
    DEV_ModuleInit();

    logStatus("Init display driver");
    EPD_12in48B_Init();
    logBusyPins("After EPD_12in48B_Init");

    logStatus("Init SRAM");
    SRAM_Init();
    logSramSanity();

    logStatus("Clear white");
    EPD_12in48B_Clear();
    logBusyPins("After EPD_12in48B_Clear");

    DEV_Delay_ms(500);

    logStatus("Prepare framebuffer");
    Paint_NewImage(REDIMAGE, kDisplayWidth, kDisplayHeight, ROTATE_0, WHITE);
    Paint_Clear();
    Paint_DrawString_EN(titleRedX, titleRedY, titleText, &Font64, WHITE, RED);
    if (strlen(gContentText) > 0) {
        logStatus("Draw red content text");
        drawCenteredWrappedStyledText(contentTop, contentHeight, contentLeft, contentWidth, gContentText, &Font48, true);
    }

    Paint_NewImage(BLACKIMAGE, kDisplayWidth, kDisplayHeight, ROTATE_0, WHITE);
    Paint_Clear();
    Paint_DrawString_EN(titleBaseX, titleBaseY, titleText, &Font64, WHITE, BLACK);

    Paint_DrawLine(0, titleBarHeight, kDisplayWidth - 1, titleBarHeight, BLACK, LINE_STYLE_SOLID, DOT_PIXEL_2X2);
    Paint_DrawLine(0, contentBottom, kDisplayWidth - 1, contentBottom, BLACK, LINE_STYLE_SOLID, DOT_PIXEL_2X2);
    drawLeftAlignedText(contentBottom, statusBarHeight, statusLeftPadding, gStatusText, &Font24, BLACK);

    if (strlen(gContentText) == 0) {
        logStatus("Draw content logo");
        Paint_DrawImage(gImage_240x240logo, logoX, logoY, logoW, logoH);
    } else {
        logStatus("Draw black content text");
        drawCenteredWrappedStyledText(contentTop, contentHeight, contentLeft, contentWidth, gContentText, &Font48, false);
    }

    logStatus("Start EPD_12in48B_Display (full refresh)");
    EPD_12in48B_Display();
    logBusyPins("After EPD_12in48B_Display");

    // This large tri-color panel can still be finishing internal waveform phases
    // even after BUSY appears released on some board/panel combinations.
    // Give it extra settle time before entering deep sleep.
    logStatus("Settle before sleep");
    DEV_Delay_ms(kSettleBeforeSleepMs);
    logBusyPins("Before sleep");

    logStatus("Put panel in sleep mode");
    EPD_12in48B_Sleep();
    logStatus("Cycle complete");
}

static void printSerialHelp(void)
{
    Serial.println(F("Commands:"));
    Serial.println(F("  TITLE=<text>    Update title (Font64)"));
    Serial.println(F("  CONTENT=<text>  Update content (Font48, max 256 chars; _red_, |bold|, \\n line break)"));
    Serial.println(F("  CONTENT=LOGO    Show centered logo in content area"));
    Serial.println(F("  STATUS=<text>   Update status bar (Font24, left aligned)"));
    Serial.println(F("  STATUS=IP       Show local WiFi IP in status bar"));
    Serial.println(F("  REFRESH         Redraw display with current values"));
    Serial.println(F("  HELP            Show this help"));
}

static void processSerialCommand(const String& input)
{
    String cmd = input;
    cmd.trim();
    if (cmd.length() == 0) {
        return;
    }

    String upper = cmd;
    upper.toUpperCase();

    if (upper == "HELP") {
        printSerialHelp();
        return;
    }

    if (upper == "REFRESH") {
        Serial.println(F("Refreshing display..."));
        runDisplayCycle();
        Serial.println(F("OK: display refreshed."));
        return;
    }

    if (upper.startsWith("TITLE=")) {
        String value = cmd.substring(6);
        value.trim();
        copyStringToBuffer(value, gTitleText, kTitleTextMax);
        runDisplayCycle();
        Serial.print(F("OK: title="));
        Serial.println(gTitleText);
        return;
    }

    if (upper.startsWith("CONTENT=")) {
        String value = cmd.substring(8);
        value.trim();

        if (value.equalsIgnoreCase("LOGO")) {
            gContentText[0] = '\0';
        } else {
            copyStringToBuffer(value, gContentText, kContentTextMax);
        }

        runDisplayCycle();
        if (strlen(gContentText) == 0) {
            Serial.println(F("OK: content=LOGO"));
        } else {
            Serial.print(F("OK: content="));
            Serial.println(gContentText);
        }
        return;
    }

    if (upper.startsWith("STATUS=")) {
        String value = cmd.substring(7);
        value.trim();

        if (value.equalsIgnoreCase("IP")) {
            setStatusToCurrentIp();
        } else {
            copyStringToBuffer(value, gStatusText, kStatusTextMax);
        }

        runDisplayCycle();
        Serial.print(F("OK: status="));
        Serial.println(gStatusText);
        return;
    }

    Serial.print(F("Unknown command: "));
    Serial.println(cmd);
    Serial.println(F("Type HELP for command list."));
}

static void pollSerialCommands(void)
{
    while (Serial.available() > 0) {
        const char ch = static_cast<char>(Serial.read());
        gLastSerialCharMs = millis();

        if (ch == '\r' || ch == '\n') {
            processSerialCommand(gSerialLine);
            gSerialLine = "";
            continue;
        }

        if (gSerialLine.length() < kSerialLineMax) {
            gSerialLine += ch;
        }
    }

    // Allow command entry when Serial Monitor is set to "No line ending".
    if (gSerialLine.length() > 0 && (millis() - gLastSerialCharMs) >= kSerialIdleProcessMs) {
        processSerialCommand(gSerialLine);
        gSerialLine = "";
    }
}

void setup()
{
    Serial.begin(115200);
    while (!Serial && millis() < 3000) {
        delay(10);
    }

    logStatus("Boot");
    DEV_ModuleInit();
    logStatus("DEV_ModuleInit done");

    logBusyPins("Initial pin read");
    runDisplayCycle();
    connectWifiAfterBootRefresh();

    Serial.println(F("Display command interface ready. Type HELP."));
    printSerialHelp();
}

void loop()
{
    pollSerialCommands();

    if (!kDiagnosticMode) {
        return;
    }

    static unsigned long lastHeartbeat = 0;
    if (millis() - lastHeartbeat >= 5000) {
        lastHeartbeat = millis();
        logStatus("Idle heartbeat.");
        logBusyPins("Heartbeat");
    }
}
