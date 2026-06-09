#include "DEV_Config.h"
#include "EPD_12in48b.h"
#include "GUI_Paint.h"
#include "SRAM_23LC.h"
#include "imagedata.h"
#include "config.h"
#include "secrets.h"
#include <WiFiS3.h>
#include <string.h>

static const bool kDiagnosticMode = false;
#if MODE == AP
static const bool kBootInAccessPointMode = true;
#elif MODE == STA
static const bool kBootInAccessPointMode = false;
#else
#error "Invalid MODE in config.h. Use AP or STA."
#endif
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
static const char kDefaultApSsid[] = "Waveshare-AP";
static const char kDefaultApPassword[] = "waveshare123";
static const char kApModeBootStatus[] = "webwings.nl 2026 (Access Point Mode)";
static const UWORD kBoldOffsetPx = 1;
static const unsigned long kHttpReadTimeoutMs = 3000;
static const size_t kHttpBodyMax = 512;

static char gTitleText[kTitleTextMax] = "";
static char gContentText[kContentTextMax] = "";
static char gStatusText[kStatusTextMax] = "";
static String gSerialLine;
static unsigned long gLastSerialCharMs = 0;
static WiFiServer gWebServer(80);
static bool gWebServerStarted = false;
static bool gUseAccessPointMode = kBootInAccessPointMode;

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

static String ipToString(const IPAddress& ip)
{
    return String(ip[0]) + "." + String(ip[1]) + "." + String(ip[2]) + "." + String(ip[3]);
}

static const char* getAccessPointSsid(void)
{
#ifdef AP_SSID
    return AP_SSID;
#else
    return kDefaultApSsid;
#endif
}

static const char* getAccessPointPassword(void)
{
#ifdef AP_PASSWORD
    return AP_PASSWORD;
#else
    return kDefaultApPassword;
#endif
}

static bool isStationConnected(void)
{
    return WiFi.status() == WL_CONNECTED;
}

static bool isAccessPointActive(void)
{
    const int status = WiFi.status();
    return status == WL_AP_LISTENING || status == WL_AP_CONNECTED;
}

static bool isNetworkReady(void)
{
    return isStationConnected() || isAccessPointActive();
}

static const char* activeWifiModeLabel(void)
{
    return gUseAccessPointMode ? "AP" : "STA";
}

static uint16_t toScalePermille(float scale)
{
    if (scale < 0.1f) {
        scale = 1.0f;
    }
    if (scale > 4.0f) {
        scale = 4.0f;
    }
    return static_cast<uint16_t>(scale * 1000.0f + 0.5f);
}

static uint16_t getConfiguredTextScalePermille(void)
{
#if CONTENT_FONT == CONTENT_FONT_SPACE_MONO
    const float scale = CONTENT_FONT_SCALE_SPACE_MONO;
#elif CONTENT_FONT == CONTENT_FONT_MANROPE
    const float scale = CONTENT_FONT_SCALE_MANROPE;
#elif CONTENT_FONT == CONTENT_FONT_ANTON
    const float scale = CONTENT_FONT_SCALE_ANTON;
#elif CONTENT_FONT == CONTENT_FONT_PERMANENT_MARKER
    const float scale = CONTENT_FONT_SCALE_PERMANENT_MARKER;
#else
    const float scale = CONTENT_FONT_SCALE_DEFAULT;
#endif

    return toScalePermille(scale);
}

static sFONT* getConfiguredTitleFont(void)
{
#if CONTENT_FONT == CONTENT_FONT_SPACE_MONO
    return &Font64_GoogleSpaceMono;
#elif CONTENT_FONT == CONTENT_FONT_MANROPE
    return &Font64_GoogleManrope;
#elif CONTENT_FONT == CONTENT_FONT_ANTON
    return &Font64_GoogleAnton;
#elif CONTENT_FONT == CONTENT_FONT_PERMANENT_MARKER
    return &Font64_GooglePermanentMarker;
#else
    return &Font64;
#endif
}

static sFONT* getConfiguredContentFont(void)
{
#if CONTENT_FONT == CONTENT_FONT_SPACE_MONO
    return &Font48_GoogleSpaceMono;
#elif CONTENT_FONT == CONTENT_FONT_MANROPE
    return &Font48_GoogleManrope;
#elif CONTENT_FONT == CONTENT_FONT_ANTON
    return &Font48_GoogleAnton;
#elif CONTENT_FONT == CONTENT_FONT_PERMANENT_MARKER
    return &Font48_GooglePermanentMarker;
#else
    return &Font48;
#endif
}

static sFONT* getConfiguredStatusFont(void)
{
#if CONTENT_FONT == CONTENT_FONT_SPACE_MONO
    return &Font24_GoogleSpaceMono;
#elif CONTENT_FONT == CONTENT_FONT_MANROPE
    return &Font24_GoogleManrope;
#elif CONTENT_FONT == CONTENT_FONT_ANTON
    return &Font24_GoogleAnton;
#elif CONTENT_FONT == CONTENT_FONT_PERMANENT_MARKER
    return &Font24_GooglePermanentMarker;
#else
    return &Font24;
#endif
}

static String buildDefaultStatusText(void)
{
    String status = "webwings.nl 2026";
    if (isNetworkReady()) {
        status += " (IP:";
        status += ipToString(WiFi.localIP());
        status += ")";
    }
    return status;
}

static bool setStatusToCurrentModeAndIp(void)
{
    if (!isNetworkReady()) {
        return false;
    }

    String status = "webwings.nl 2026 (";
    status += activeWifiModeLabel();
    status += ": ";
    status += ipToString(WiFi.localIP());
    status += ")";
    copyStringToBuffer(status, gStatusText, kStatusTextMax);
    return true;
}

static bool connectWifiAfterBootRefresh(void)
{
    if (WiFi.status() == WL_NO_MODULE) {
        Serial.println(F("WiFi module not detected."));
        return false;
    }

    if (strcmp(WIFI_SSID, "YOUR_WIFI_SSID") == 0 || strcmp(WIFI_PASSWORD, "YOUR_WIFI_PASSWORD") == 0) {
        Serial.println(F("WiFi credentials not configured in secrets.h."));
        return false;
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
        return true;
    } else {
        Serial.println(F("WiFi connection failed (timeout)."));
        return false;
    }
}

static bool startAccessPointMode(void)
{
    if (WiFi.status() == WL_NO_MODULE) {
        Serial.println(F("WiFi module not detected."));
        return false;
    }

    const char* apSsid = getAccessPointSsid();
    const char* apPassword = getAccessPointPassword();

    Serial.print(F("Starting Access Point: "));
    Serial.println(apSsid);

    int status = WL_IDLE_STATUS;
    if (strlen(apPassword) >= 8) {
        status = WiFi.beginAP(apSsid, apPassword);
    } else {
        status = WiFi.beginAP(apSsid);
    }

    unsigned long startMs = millis();
    while (status != WL_AP_LISTENING && status != WL_AP_CONNECTED && (millis() - startMs) < kWifiConnectTimeoutMs) {
        Serial.print('.');
        delay(kWifiConnectPollMs);
        status = WiFi.status();
    }

    Serial.println();
    if (status == WL_AP_LISTENING || status == WL_AP_CONNECTED) {
        Serial.print(F("AP active. Connect to SSID: "));
        Serial.println(apSsid);
        Serial.print(F("AP IP address: "));
        Serial.println(WiFi.localIP());
        return true;
    }

    Serial.println(F("Failed to start Access Point mode."));
    return false;
}

static bool applyNetworkMode(void)
{
    gWebServerStarted = false;
    WiFi.disconnect();
    delay(200);

    if (gUseAccessPointMode) {
        return startAccessPointMode();
    }

    return connectWifiAfterBootRefresh();
}

static bool switchNetworkMode(const bool useAccessPointMode)
{
    const bool previousMode = gUseAccessPointMode;
    gUseAccessPointMode = useAccessPointMode;
    bool ok = applyNetworkMode();
    if (!ok) {
        // Keep device reachable by restoring previous mode if switch fails.
        gUseAccessPointMode = previousMode;
        applyNetworkMode();
    }

    if (ok) {
        Serial.print(F("Network mode active: "));
        Serial.println(activeWifiModeLabel());
    } else {
        Serial.print(F("Network mode switch failed, kept mode: "));
        Serial.println(activeWifiModeLabel());
    }
    return ok;
}

static bool setStatusToCurrentIp(void)
{
    if (!isNetworkReady()) {
        copyStringToBuffer(String("WiFi disconnected"), gStatusText, kStatusTextMax);
        return false;
    }

    IPAddress ip = WiFi.localIP();
    String ipText = ipToString(ip);
    copyStringToBuffer(ipText, gStatusText, kStatusTextMax);
    return true;
}

static void copyStringToBuffer(const String& src, char* dst, size_t dstSize)
{
    src.toCharArray(dst, dstSize);
    dst[dstSize - 1] = '\0';
}

static void applyConfiguredDisplayDefaults(void)
{
    copyStringToBuffer(String(TITLE), gTitleText, kTitleTextMax);

    if (gUseAccessPointMode) {
        copyStringToBuffer(String(kApModeBootStatus), gStatusText, kStatusTextMax);
    } else {
        copyStringToBuffer(String(STATUS), gStatusText, kStatusTextMax);
    }

    String defaultContent = String(CONTENT);
    defaultContent.trim();
    if (defaultContent.equalsIgnoreCase("LOGO")) {
        gContentText[0] = '\0';
    } else {
        copyStringToBuffer(defaultContent, gContentText, kContentTextMax);
    }
}

static int hexDigitToInt(const char ch)
{
    if (ch >= '0' && ch <= '9') {
        return ch - '0';
    }
    if (ch >= 'a' && ch <= 'f') {
        return (ch - 'a') + 10;
    }
    if (ch >= 'A' && ch <= 'F') {
        return (ch - 'A') + 10;
    }
    return -1;
}

static String urlDecode(const String& encoded)
{
    String decoded;
    decoded.reserve(encoded.length());

    for (size_t i = 0; i < static_cast<size_t>(encoded.length()); i++) {
        const char ch = encoded[i];
        if (ch == '+') {
            decoded += ' ';
            continue;
        }

        if (ch == '%' && (i + 2) < static_cast<size_t>(encoded.length())) {
            const int hi = hexDigitToInt(encoded[i + 1]);
            const int lo = hexDigitToInt(encoded[i + 2]);
            if (hi >= 0 && lo >= 0) {
                decoded += static_cast<char>((hi << 4) | lo);
                i += 2;
                continue;
            }
        }

        decoded += ch;
    }

    return decoded;
}

static String getFormField(const String& body, const char* key)
{
    const String needle = String(key) + "=";
    const int startPos = body.indexOf(needle);
    if (startPos < 0) {
        return "";
    }

    const int valueStart = startPos + needle.length();
    int valueEnd = body.indexOf('&', valueStart);
    if (valueEnd < 0) {
        valueEnd = body.length();
    }
    return body.substring(valueStart, valueEnd);
}

static bool hasFormField(const String& body, const char* key)
{
    const String needle = String(key) + "=";
    return body.indexOf(needle) >= 0;
}

static String htmlEscape(const String& raw)
{
    String out;
    out.reserve(raw.length() + 16);

    for (size_t i = 0; i < static_cast<size_t>(raw.length()); i++) {
        const char ch = raw[i];
        switch (ch) {
        case '&':
            out += F("&amp;");
            break;
        case '<':
            out += F("&lt;");
            break;
        case '>':
            out += F("&gt;");
            break;
        case '"':
            out += F("&quot;");
            break;
        case '\'':
            out += F("&#39;");
            break;
        default:
            out += ch;
            break;
        }
    }

    return out;
}

static String jsonEscape(const String& raw)
{
    String out;
    out.reserve(raw.length() + 16);

    for (size_t i = 0; i < static_cast<size_t>(raw.length()); i++) {
        const char ch = raw[i];
        switch (ch) {
        case '\\':
            out += F("\\\\");
            break;
        case '"':
            out += F("\\\"");
            break;
        case '\n':
            out += F("\\n");
            break;
        case '\r':
            out += F("\\r");
            break;
        case '\t':
            out += F("\\t");
            break;
        default:
            out += ch;
            break;
        }
    }

    return out;
}

static void sendWebFormPage(WiFiClient& client, const String& message)
{
    const String safeTitle = htmlEscape(String(gTitleText));
    const String safeContent = htmlEscape(String(gContentText));
    const String safeMessage = htmlEscape(message);
    const String safeWebTitle = htmlEscape(String(WEB_TITLE));
    const char* modeLabel = gUseAccessPointMode ? "AP" : "STA";
    const char* targetMode = gUseAccessPointMode ? "STA" : "AP";

    client.println(F("HTTP/1.1 200 OK"));
    client.println(F("Content-Type: text/html; charset=utf-8"));
    client.println(F("Connection: close"));
    client.println();

    client.println(F("<!doctype html>"));
    client.println(F("<html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>"));
    client.print(F("<title>"));
    client.print(safeWebTitle);
    client.println(F("</title>"));
    client.println(F("<style>body{font-family:Arial,sans-serif;max-width:720px;margin:2rem auto;padding:0 1rem;}input,textarea,button{width:100%;font-size:16px;box-sizing:border-box;margin-top:.5rem;padding:.7rem;}button{cursor:pointer;}label{font-weight:600;display:block;margin-top:1rem;}.msg{margin:1rem 0;padding:.7rem;border:1px solid #8bc28b;background:#eef8ee;}small{display:block;margin-top:.5rem;color:#444;line-height:1.4;}.mode{margin-top:1rem;padding:.6rem;border:1px dashed #999;background:#f8f8f8;}</style>"));
    client.print(F("</head><body><h1>"));
    client.print(safeWebTitle);
    client.println(F("</h1>"));
    client.print(F("<div class='mode'>Active network mode: <strong>"));
    client.print(modeLabel);
    client.println(F("</strong></div>"));

    if (safeMessage.length() > 0) {
        client.print(F("<div class='msg'>"));
        client.print(safeMessage);
        client.println(F("</div>"));
    }

    client.println(F("<form method='POST' action='/'>"));
    client.print(F("<label for='title'>Title</label><input id='title' name='title' type='text' maxlength='63' value='"));
    client.print(safeTitle);
    client.println(F("'>"));

    client.print(F("<label for='content'>Content</label><textarea id='content' name='content' rows='7' maxlength='256'>"));
    client.print(safeContent);
    client.println(F("</textarea>"));

    client.println(F("<small>Supported: _red_, |bold| (extra bold), ~inverse~ (highlight), and \\n for a new line. Empty content shows the logo view.</small>"));
    client.println(F("<button type='submit'>POST</button></form>"));

    client.println(F("<form method='POST' action='/'>"));
    client.print(F("<input type='hidden' name='mode' value='"));
    client.print(targetMode);
    client.println(F("'>"));
    client.print(F("<button type='submit'>Switch to "));
    client.print(targetMode);
    client.println(F(" mode</button></form>"));

    client.println(F("</body></html>"));
}

static void sendRedirectToRoot(WiFiClient& client)
{
    const String location = String("http://") + ipToString(WiFi.localIP()) + "/";

    client.println(F("HTTP/1.1 302 Found"));
    client.print(F("Location: "));
    client.println(location);
    client.println(F("Cache-Control: no-store, no-cache, must-revalidate, max-age=0"));
    client.println(F("Pragma: no-cache"));
    client.println(F("Connection: close"));
    client.println();
}

static void startWebServerIfConnected(void)
{
    if (gWebServerStarted || !isNetworkReady()) {
        return;
    }

    gWebServer.begin();
    gWebServerStarted = true;
    Serial.print(F("Web UI ready ("));
    Serial.print(activeWifiModeLabel());
    Serial.print(F("): http://"));
    Serial.println(WiFi.localIP());
    if (gUseAccessPointMode) {
        Serial.println(F("AP captive redirect active: unknown GET paths redirect to /"));
    }
}

static void handleWebClient(void)
{
    if (!gWebServerStarted) {
        return;
    }

    WiFiClient client = gWebServer.available();
    if (!client) {
        return;
    }

    client.setTimeout(2000);

    String requestLine = client.readStringUntil('\n');
    requestLine.trim();

    const int firstSpace = requestLine.indexOf(' ');
    const int secondSpace = requestLine.indexOf(' ', firstSpace + 1);
    String method = "";
    String path = "";
    if (firstSpace > 0 && secondSpace > firstSpace) {
        method = requestLine.substring(0, firstSpace);
        path = requestLine.substring(firstSpace + 1, secondSpace);
    }

    const bool isGetRoot = (method == "GET" && path == "/");
    const bool isPostRoot = (method == "POST" && path == "/");
    const bool isPostApiUpdate = (method == "POST" && path == "/api/update");

    int contentLength = 0;
    while (client.connected()) {
        String headerLine = client.readStringUntil('\n');
        if (headerLine == "\r" || headerLine.length() == 0) {
            break;
        }

        String trimmed = headerLine;
        trimmed.trim();
        if (trimmed.length() == 0) {
            break;
        }

        String lower = trimmed;
        lower.toLowerCase();
        if (lower.startsWith("content-length:")) {
            String value = trimmed.substring(15);
            value.trim();
            contentLength = value.toInt();
        }
    }

    String message = "";
    bool didUpdate = false;
    bool modeFieldPresent = false;
    bool modeFieldValid = false;
    bool modeAlreadyActive = false;
    bool shouldSwitchMode = false;
    bool switchToApMode = gUseAccessPointMode;

    if ((isPostRoot || isPostApiUpdate) && contentLength > 0) {
        const int bodyLimit = (contentLength > static_cast<int>(kHttpBodyMax)) ? static_cast<int>(kHttpBodyMax) : contentLength;
        String body;
        body.reserve(bodyLimit);

        int bytesRead = 0;

        const unsigned long readStartMs = millis();
        while (bytesRead < contentLength && (millis() - readStartMs) < kHttpReadTimeoutMs) {
            while (client.available() > 0 && bytesRead < contentLength) {
                const char ch = static_cast<char>(client.read());
                if (static_cast<int>(body.length()) < bodyLimit) {
                    body += ch;
                }
                bytesRead++;
            }
            delay(1);
        }

        const bool hasTitle = hasFormField(body, "title");
        const bool hasContent = hasFormField(body, "content");
        const bool hasStatus = hasFormField(body, "status");
        const bool hasMode = hasFormField(body, "mode");

        if (hasTitle) {
            const String newTitle = urlDecode(getFormField(body, "title"));
            copyStringToBuffer(newTitle, gTitleText, kTitleTextMax);
        }

        if (hasContent) {
            const String newContent = urlDecode(getFormField(body, "content"));
            if (newContent.equalsIgnoreCase("LOGO")) {
                gContentText[0] = '\0';
            } else {
                copyStringToBuffer(newContent, gContentText, kContentTextMax);
            }
        }

        if (hasStatus) {
            String newStatus = urlDecode(getFormField(body, "status"));
            newStatus.trim();
            if (newStatus.length() == 0) {
                copyStringToBuffer(buildDefaultStatusText(), gStatusText, kStatusTextMax);
            } else {
                copyStringToBuffer(newStatus, gStatusText, kStatusTextMax);
            }
        }

        if (hasContent && !hasStatus) {
            setStatusToCurrentModeAndIp();
        }

        if (hasMode) {
            modeFieldPresent = true;
            String newMode = urlDecode(getFormField(body, "mode"));
            newMode.trim();
            newMode.toUpperCase();

            if (newMode == "AP") {
                modeFieldValid = true;
                switchToApMode = true;
                modeAlreadyActive = gUseAccessPointMode;
                shouldSwitchMode = !modeAlreadyActive;
            } else if (newMode == "STA") {
                modeFieldValid = true;
                switchToApMode = false;
                modeAlreadyActive = !gUseAccessPointMode;
                shouldSwitchMode = !modeAlreadyActive;
            }
        }

        didUpdate = hasTitle || hasContent || hasStatus;
        if (didUpdate) {
            runDisplayCycle();
            message = "Display updated via web POST.";
            Serial.println(F("OK: web POST applied."));
        }

        if (modeFieldPresent) {
            if (!modeFieldValid) {
                message = "Invalid mode. Use mode=AP or mode=STA.";
            } else if (modeAlreadyActive) {
                message = "Selected network mode is already active.";
            } else if (shouldSwitchMode) {
                if (didUpdate) {
                    message = "Display updated. Network mode will now switch.";
                } else {
                    message = "Network mode will now switch.";
                }
            }
        } else if (!didUpdate) {
            message = "No fields received (use title=, content=, and/or status=).";
        }
    } else if (isPostRoot || isPostApiUpdate) {
        message = "Empty POST body received.";
    } else if (!isGetRoot) {
        message = "Only GET on / and POST on / or /api/update are supported.";
    }

    if (isPostApiUpdate) {
        const String safeTitleJson = jsonEscape(String(gTitleText));
        const String safeContentJson = jsonEscape(String(gContentText));
        const String safeStatusJson = jsonEscape(String(gStatusText));
        const String safeMessageJson = jsonEscape(message);

        client.println(F("HTTP/1.1 200 OK"));
        client.println(F("Content-Type: application/json; charset=utf-8"));
        client.println(F("Connection: close"));
        client.println();
        client.print(F("{\"ok\":"));
        client.print(didUpdate ? F("true") : F("false"));
        client.print(F(",\"title\":\""));
        client.print(safeTitleJson);
        client.print(F("\",\"content\":\""));
        client.print(safeContentJson);
        client.print(F("\",\"status\":\""));
        client.print(safeStatusJson);
        client.print(F("\",\"message\":\""));
        client.print(safeMessageJson);
        client.println(F("\"}"));
    } else if (isGetRoot || isPostRoot) {
        sendWebFormPage(client, message);
    } else if (gUseAccessPointMode && method == "GET") {
        sendRedirectToRoot(client);
    } else {
        client.println(F("HTTP/1.1 404 Not Found"));
        client.println(F("Content-Type: text/plain; charset=utf-8"));
        client.println(F("Connection: close"));
        client.println();
        client.println(F("Not Found"));
    }

    delay(1);
    client.stop();

    if (shouldSwitchMode) {
        Serial.print(F("Web requested mode switch to "));
        Serial.println(switchToApMode ? F("AP") : F("STA"));
        const bool switched = switchNetworkMode(switchToApMode);
        if (!switched) {
            Serial.println(F("Web mode switch failed; kept previous mode."));
        }
        startWebServerIfConnected();
    }
}

static UWORD scaledSpanFromPermille(UWORD value, uint16_t scalePermille)
{
    const uint32_t scaled = static_cast<uint32_t>(value) * static_cast<uint32_t>(scalePermille);
    UWORD out = static_cast<UWORD>((scaled + 999U) / 1000U);
    if (out == 0) {
        out = 1;
    }
    return out;
}

static int16_t scaledOffsetFromPermille(int16_t value, uint16_t scalePermille)
{
    if (value == 0) {
        return 0;
    }

    int32_t scaled = static_cast<int32_t>(value) * static_cast<int32_t>(scalePermille);
    if (scaled >= 0) {
        scaled = (scaled + 500) / 1000;
    } else {
        scaled = (scaled - 500) / 1000;
    }

    if (scaled == 0) {
        scaled = (value > 0) ? 1 : -1;
    }
    return static_cast<int16_t>(scaled);
}

static void drawStringScaled(UWORD xStart, UWORD yStart, const char* text, sFONT* font, UWORD textColor, uint16_t scalePermille)
{
    if (scalePermille <= 1000U) {
        Paint_DrawString_EN(xStart, yStart, text, font, WHITE, textColor);
        return;
    }

    const uint8_t bytesPerRow = static_cast<uint8_t>((font->Width / 8) + ((font->Width % 8) ? 1 : 0));
    UWORD cursorX = xStart;

    for (size_t idx = 0; text[idx] != '\0'; idx++) {
        const char ch = text[idx];
        if (ch < ' ' || ch > '~') {
            continue;
        }

        const uint32_t charOffset = static_cast<uint32_t>(ch - ' ') * font->Height * bytesPerRow;
        const unsigned char* glyph = &font->table[charOffset];

        for (UWORD row = 0; row < font->Height; row++) {
            const unsigned char* rowPtr = glyph + static_cast<size_t>(row) * bytesPerRow;
            for (UWORD col = 0; col < font->Width; col++) {
                if (pgm_read_byte(rowPtr + (col / 8)) & (0x80 >> (col % 8))) {
                    uint32_t x0 = static_cast<uint32_t>(cursorX) + (static_cast<uint32_t>(col) * scalePermille) / 1000U;
                    uint32_t x1 = static_cast<uint32_t>(cursorX) + (static_cast<uint32_t>(col + 1U) * scalePermille) / 1000U;
                    uint32_t y0 = static_cast<uint32_t>(yStart) + (static_cast<uint32_t>(row) * scalePermille) / 1000U;
                    uint32_t y1 = static_cast<uint32_t>(yStart) + (static_cast<uint32_t>(row + 1U) * scalePermille) / 1000U;
                    if (x1 <= x0) {
                        x1 = x0 + 1U;
                    }
                    if (y1 <= y0) {
                        y1 = y0 + 1U;
                    }

                    for (uint32_t py = y0; py < y1; py++) {
                        if (py >= kDisplayHeight) {
                            continue;
                        }
                        for (uint32_t px = x0; px < x1; px++) {
                            if (px < kDisplayWidth) {
                                Paint_SetPixel(static_cast<UWORD>(px), static_cast<UWORD>(py), textColor);
                            }
                        }
                    }
                }
            }
        }

        cursorX += scaledSpanFromPermille(font->Width, scalePermille);
    }
}

static void drawCenteredText(UWORD yTop, UWORD areaHeight, const char* text, sFONT* font, UWORD textColor, uint16_t scalePermille)
{
    const UWORD scaledCharWidth = scaledSpanFromPermille(font->Width, scalePermille);
    const UWORD textWidth = static_cast<UWORD>(strlen(text) * scaledCharWidth);
    const UWORD textHeight = scaledSpanFromPermille(font->Height, scalePermille);
    const UWORD textX = (kDisplayWidth > textWidth) ? (kDisplayWidth - textWidth) / 2 : 0;
    const UWORD textY = yTop + ((areaHeight > textHeight) ? (areaHeight - textHeight) / 2 : 0);

    drawStringScaled(textX, textY, text, font, textColor, scalePermille);
}

static void drawLeftAlignedText(UWORD yTop, UWORD areaHeight, UWORD xLeft, const char* text, sFONT* font, UWORD textColor, uint16_t scalePermille)
{
    const UWORD textHeight = scaledSpanFromPermille(font->Height, scalePermille);
    const UWORD textY = yTop + ((areaHeight > textHeight) ? (areaHeight - textHeight) / 2 : 0);

    drawStringScaled(xLeft, textY, text, font, textColor, scalePermille);
}

static void drawTextWithOffset(UWORD baseX, UWORD baseY, const char* text, sFONT* font, int16_t offsetX, int16_t offsetY, UWORD textColor, uint16_t scalePermille)
{
    const long x = static_cast<long>(baseX) + static_cast<long>(offsetX);
    const long y = static_cast<long>(baseY) + static_cast<long>(offsetY);
    if (x < 0 || y < 0 || x >= static_cast<long>(kDisplayWidth) || y >= static_cast<long>(kDisplayHeight)) {
        return;
    }

    drawStringScaled(static_cast<UWORD>(x), static_cast<UWORD>(y), text, font, textColor, scalePermille);
}

static void drawCenteredWrappedStyledText(UWORD yTop, UWORD areaHeight, UWORD xLeft, UWORD areaWidth, const char* rawText, sFONT* font, bool drawRedSegments, uint16_t scalePermille)
{
    const UWORD scaledCharWidth = scaledSpanFromPermille(font->Width, scalePermille);
    const UWORD scaledCharHeight = scaledSpanFromPermille(font->Height, scalePermille);
    if (areaWidth < scaledCharWidth || areaHeight < scaledCharHeight) {
        return;
    }

    const size_t maxCharsPerLine = areaWidth / scaledCharWidth;
    const size_t maxLinesInArea = areaHeight / scaledCharHeight;
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
    bool inverseMask[kContentTextMax];
    size_t normalizedLen = 0;
    bool inRedSegment = false;
    bool inBoldSegment = false;
    bool inInverseSegment = false;
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

        if (ch == '~') {
            inInverseSegment = !inInverseSegment;
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
                inverseMask[normalizedLen] = false;
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
                    inverseMask[normalizedLen] = false;
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
            redMask[normalizedLen] = inRedSegment;
            boldMask[normalizedLen] = inBoldSegment;
            inverseMask[normalizedLen] = inInverseSegment;
            normalizedLen++;
            prevWasSpace = true;
            continue;
        }

        normalized[normalizedLen] = ch;
        redMask[normalizedLen] = inRedSegment;
        boldMask[normalizedLen] = inBoldSegment;
        inverseMask[normalizedLen] = inInverseSegment;
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
    const UWORD blockHeight = static_cast<UWORD>(renderLineCount * scaledCharHeight);
    const UWORD startY = yTop + ((areaHeight > blockHeight) ? (areaHeight - blockHeight) / 2 : 0);

    for (size_t lineIndex = 0; lineIndex < renderLineCount; lineIndex++) {
        const size_t lineStart = lineStarts[lineIndex];
        const size_t lineEnd = lineEnds[lineIndex];
        const size_t lineLen = lineEnd - lineStart;
        const UWORD lineWidth = static_cast<UWORD>(lineLen * scaledCharWidth);
        const UWORD lineX = xLeft + ((areaWidth > lineWidth) ? (areaWidth - lineWidth) / 2 : 0);
        const UWORD lineY = startY + static_cast<UWORD>(lineIndex * scaledCharHeight);

        if (lineLen == 0) {
            continue;
        }

        size_t runStart = lineStart;
        while (runStart < lineEnd) {
            const bool runIsRed = redMask[runStart];
            const bool runIsBold = boldMask[runStart];
            const bool runIsInverse = inverseMask[runStart];
            size_t runEnd = runStart + 1;
            while (runEnd < lineEnd && redMask[runEnd] == runIsRed && boldMask[runEnd] == runIsBold && inverseMask[runEnd] == runIsInverse) {
                runEnd++;
            }

            if (runIsRed == drawRedSegments) {
                const size_t runLen = runEnd - runStart;
                char runText[kContentTextMax];
                memcpy(runText, &normalized[runStart], runLen);
                runText[runLen] = '\0';

                const UWORD runX = lineX + static_cast<UWORD>((runStart - lineStart) * scaledCharWidth);
                UWORD color = drawRedSegments ? RED : BLACK;

                if (runIsInverse) {
                    const UWORD runWidth = static_cast<UWORD>(runLen * scaledCharWidth);
                    if (runWidth > 0 && runX < kDisplayWidth && lineY < kDisplayHeight) {
                        UWORD rectXEnd = runX + runWidth - 1;
                        UWORD rectYEnd = lineY + scaledCharHeight - 1;
                        if (rectXEnd >= kDisplayWidth) {
                            rectXEnd = kDisplayWidth - 1;
                        }
                        if (rectYEnd >= kDisplayHeight) {
                            rectYEnd = kDisplayHeight - 1;
                        }
                        Paint_DrawRectangle(runX, lineY, rectXEnd, rectYEnd, color, DRAW_FILL_FULL, DOT_PIXEL_1X1);
                    }
                    color = WHITE;
                }

                drawStringScaled(runX, lineY, runText, font, color, scalePermille);
                if (runIsBold) {
                    const UWORD boldOffset = static_cast<UWORD>(scaledOffsetFromPermille(static_cast<int16_t>(kBoldOffsetPx), scalePermille));
                    if ((runX + boldOffset) < kDisplayWidth) {
                        drawStringScaled(runX + boldOffset, lineY, runText, font, color, scalePermille);
                    }
                    if (runX >= boldOffset) {
                        drawStringScaled(runX - boldOffset, lineY, runText, font, color, scalePermille);
                    }
                    if ((lineY + boldOffset) < kDisplayHeight) {
                        drawStringScaled(runX, lineY + boldOffset, runText, font, color, scalePermille);
                    }
                    if (lineY >= boldOffset) {
                        drawStringScaled(runX, lineY - boldOffset, runText, font, color, scalePermille);
                    }
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
    const char* titleText = gTitleText;
    sFONT* titleFont = getConfiguredTitleFont();
    sFONT* contentFont = getConfiguredContentFont();
    sFONT* statusFont = getConfiguredStatusFont();
    const uint16_t textScalePermille = getConfiguredTextScalePermille();
    const UWORD scaledTitleCharWidth = scaledSpanFromPermille(titleFont->Width, textScalePermille);
    const UWORD scaledTitleHeight = scaledSpanFromPermille(titleFont->Height, textScalePermille);
    const UWORD titleBarHeight = (kDisplayHeight * 20) / 100;
    const UWORD statusBarHeight = scaledSpanFromPermille(statusFont->Height, textScalePermille) + 12;
    const UWORD contentTop = titleBarHeight;
    const UWORD contentBottom = kDisplayHeight - statusBarHeight;
    const UWORD contentHeight = contentBottom - contentTop;
    const UWORD contentLeft = contentSidePadding;
    const UWORD contentWidth = kDisplayWidth - (contentSidePadding * 2);

    const UWORD titleTextWidth = static_cast<UWORD>(strlen(titleText) * scaledTitleCharWidth);
    const UWORD titleBaseX = (kDisplayWidth > titleTextWidth) ? (kDisplayWidth - titleTextWidth) / 2 : 0;
    const UWORD titleBaseY = (titleBarHeight > scaledTitleHeight)
                                 ? (titleBarHeight - scaledTitleHeight) / 2
                                 : 0;
    const int16_t shadowOffset1 = scaledOffsetFromPermille(1, textScalePermille);
    const int16_t shadowOffset2 = scaledOffsetFromPermille(2, textScalePermille);

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
    Paint_NewImage(BLACKIMAGE, kDisplayWidth, kDisplayHeight, ROTATE_0, WHITE);
    Paint_Clear();

    // Title shadow pass (black) first.
    drawTextWithOffset(titleBaseX, titleBaseY, titleText, titleFont, -shadowOffset2, 0, BLACK, textScalePermille);
    drawTextWithOffset(titleBaseX, titleBaseY, titleText, titleFont, shadowOffset2, 0, BLACK, textScalePermille);
    drawTextWithOffset(titleBaseX, titleBaseY, titleText, titleFont, 0, -shadowOffset1, BLACK, textScalePermille);
    drawTextWithOffset(titleBaseX, titleBaseY, titleText, titleFont, 0, shadowOffset2, BLACK, textScalePermille);
    drawTextWithOffset(titleBaseX, titleBaseY, titleText, titleFont, -shadowOffset1, -shadowOffset1, BLACK, textScalePermille);
    drawTextWithOffset(titleBaseX, titleBaseY, titleText, titleFont, shadowOffset1, shadowOffset1, BLACK, textScalePermille);
    drawTextWithOffset(titleBaseX, titleBaseY, titleText, titleFont, -shadowOffset1, shadowOffset1, BLACK, textScalePermille);
    drawTextWithOffset(titleBaseX, titleBaseY, titleText, titleFont, shadowOffset1, -shadowOffset1, BLACK, textScalePermille);

    Paint_DrawLine(0, titleBarHeight, kDisplayWidth - 1, titleBarHeight, BLACK, LINE_STYLE_SOLID, DOT_PIXEL_2X2);
    Paint_DrawLine(0, contentBottom, kDisplayWidth - 1, contentBottom, BLACK, LINE_STYLE_SOLID, DOT_PIXEL_2X2);
    drawLeftAlignedText(contentBottom, statusBarHeight, statusLeftPadding, gStatusText, statusFont, BLACK, textScalePermille);

    if (strlen(gContentText) == 0) {
        logStatus("Draw content logo");
        Paint_DrawImage(gImage_240x240logo, logoX, logoY, logoW, logoH);
    } else {
        logStatus("Draw black content text");
        drawCenteredWrappedStyledText(contentTop, contentHeight, contentLeft, contentWidth, gContentText, contentFont, false, textScalePermille);
    }

    Paint_NewImage(REDIMAGE, kDisplayWidth, kDisplayHeight, ROTATE_0, WHITE);
    Paint_Clear();

    // Red title overlay pass.
    drawTextWithOffset(titleBaseX, titleBaseY, titleText, titleFont, 0, 0, RED, textScalePermille);
    drawTextWithOffset(titleBaseX, titleBaseY, titleText, titleFont, -shadowOffset1, 0, RED, textScalePermille);
    drawTextWithOffset(titleBaseX, titleBaseY, titleText, titleFont, shadowOffset2, 0, RED, textScalePermille);
    drawTextWithOffset(titleBaseX, titleBaseY, titleText, titleFont, 0, shadowOffset1, RED, textScalePermille);
    drawTextWithOffset(titleBaseX, titleBaseY, titleText, titleFont, 0, -shadowOffset1, RED, textScalePermille);
    if (strlen(gContentText) > 0) {
        logStatus("Draw red content text");
        drawCenteredWrappedStyledText(contentTop, contentHeight, contentLeft, contentWidth, gContentText, contentFont, true, textScalePermille);
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
    Serial.println(F("  TITLE=<text>    Update title (selected preset)"));
    Serial.println(F("  CONTENT=<text>  Update content (selected preset, max 256 chars; _red_, |bold extra|, ~inverse~, \\n line break)"));
    Serial.println(F("                  Auto status: webwings.nl 2026 (AP/STA: <ip>) if network is active"));
    Serial.println(F("  CONTENT=LOGO    Show centered logo in content area"));
    Serial.println(F("  STATUS=<text>   Update status bar (selected preset, left aligned)"));
    Serial.println(F("  STATUS=IP       Show local WiFi IP in status bar"));
    Serial.println(F("  WIFI=AP         Switch to Access Point mode"));
    Serial.println(F("  WIFI=STA        Switch to normal WiFi mode (router)"));
    Serial.println(F("  WIFI=MODE       Show active network mode"));
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

        setStatusToCurrentModeAndIp();

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

    if (upper == "WIFI=MODE") {
        Serial.print(F("OK: wifi_mode="));
        Serial.println(activeWifiModeLabel());
        return;
    }

    if (upper == "WIFI=AP") {
        const bool ok = switchNetworkMode(true);
        startWebServerIfConnected();
        Serial.print(F("OK: wifi_mode="));
        Serial.println(ok ? F("AP") : F("AP (failed)"));
        return;
    }

    if (upper == "WIFI=STA") {
        const bool ok = switchNetworkMode(false);
        startWebServerIfConnected();
        Serial.print(F("OK: wifi_mode="));
        Serial.println(ok ? F("STA") : F("STA (failed)"));
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
    applyConfiguredDisplayDefaults();
    runDisplayCycle();
    switchNetworkMode(gUseAccessPointMode);
    startWebServerIfConnected();

    Serial.println(F("Display command interface ready. Type HELP."));
    printSerialHelp();
}

void loop()
{
    startWebServerIfConnected();
    handleWebClient();
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
