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
// Bold is simulated by overdrawing the glyph shifted ±1 px in all four cardinal directions.
// Larger values produce a thicker stroke but look blurry at base font size.
static const UWORD kBoldOffsetPx = 1;
static const unsigned long kHttpReadTimeoutMs = 3000;
static const size_t kHttpBodyMax = 512;

static char gTitleText[kTitleTextMax] = "";
static char gContentText[kContentTextMax] = "";
static char gStatusText[kStatusTextMax] = "";
static char gSerialLine[kSerialLineMax + 1] = "";
static size_t gSerialLineLen = 0;
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

static void buildDefaultStatusText(void)
{
    if (isNetworkReady()) {
        IPAddress ip = WiFi.localIP();
        snprintf(gStatusText, kStatusTextMax, "webwings.nl 2026 (IP:%u.%u.%u.%u)", ip[0], ip[1], ip[2], ip[3]);
    } else {
        strncpy(gStatusText, "webwings.nl 2026", kStatusTextMax - 1);
        gStatusText[kStatusTextMax - 1] = '\0';
    }
}

static bool setStatusToCurrentModeAndIp(void)
{
    if (!isNetworkReady()) {
        return false;
    }

    IPAddress ip = WiFi.localIP();
    snprintf(gStatusText, kStatusTextMax, "webwings.nl 2026 (%s: %u.%u.%u.%u)",
             activeWifiModeLabel(), ip[0], ip[1], ip[2], ip[3]);
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
        strncpy(gStatusText, "WiFi disconnected", kStatusTextMax - 1);
        gStatusText[kStatusTextMax - 1] = '\0';
        return false;
    }

    IPAddress ip = WiFi.localIP();
    snprintf(gStatusText, kStatusTextMax, "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
    return true;
}

static void strTrimRight(char* s)
{
    size_t len = strlen(s);
    while (len > 0 && (s[len - 1] == ' ' || s[len - 1] == '\t' || s[len - 1] == '\r' || s[len - 1] == '\n')) {
        s[--len] = '\0';
    }
}

static bool isLogoString(const char* s)
{
    return (s[0] == 'L' || s[0] == 'l') &&
           (s[1] == 'O' || s[1] == 'o') &&
           (s[2] == 'G' || s[2] == 'g') &&
           (s[3] == 'O' || s[3] == 'o') &&
           s[4] == '\0';
}

static void applyConfiguredDisplayDefaults(void)
{
    strncpy(gTitleText, TITLE, kTitleTextMax - 1);
    gTitleText[kTitleTextMax - 1] = '\0';

    if (gUseAccessPointMode) {
        snprintf(gStatusText, kStatusTextMax, "webwings.nl 2026 (Access Point Mode: %s)", getAccessPointSsid());
    } else {
        strncpy(gStatusText, STATUS, kStatusTextMax - 1);
        gStatusText[kStatusTextMax - 1] = '\0';
    }

    char tmpContent[kContentTextMax];
    strncpy(tmpContent, CONTENT, kContentTextMax - 1);
    tmpContent[kContentTextMax - 1] = '\0';
    strTrimRight(tmpContent);
    const char* tstart = tmpContent;
    while (*tstart == ' ' || *tstart == '\t') tstart++;

    if (isLogoString(tstart)) {
        gContentText[0] = '\0';
    } else {
        strncpy(gContentText, tstart, kContentTextMax - 1);
        gContentText[kContentTextMax - 1] = '\0';
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

static void urlDecodeN(const char* encoded, size_t encodedLen, char* out, size_t outSize)
{
    size_t outLen = 0;
    for (size_t i = 0; i < encodedLen && outLen < outSize - 1; i++) {
        const char ch = encoded[i];
        if (ch == '+') {
            out[outLen++] = ' ';
            continue;
        }
        if (ch == '%' && i + 2 < encodedLen) {
            const int hi = hexDigitToInt(encoded[i + 1]);
            const int lo = hexDigitToInt(encoded[i + 2]);
            if (hi >= 0 && lo >= 0) {
                out[outLen++] = static_cast<char>((hi << 4) | lo);
                i += 2;
                continue;
            }
        }
        out[outLen++] = ch;
    }
    out[outLen] = '\0';
}

static bool getFormField(const char* body, const char* key, char* out, size_t outSize)
{
    char needle[68];
    snprintf(needle, sizeof(needle), "%s=", key);
    const char* start = strstr(body, needle);
    if (!start) {
        out[0] = '\0';
        return false;
    }
    start += strlen(needle);
    const char* end = strchr(start, '&');
    if (!end) end = start + strlen(start);
    urlDecodeN(start, static_cast<size_t>(end - start), out, outSize);
    return true;
}

static bool hasFormField(const char* body, const char* key)
{
    char needle[68];
    snprintf(needle, sizeof(needle), "%s=", key);
    return strstr(body, needle) != nullptr;
}

static void sendHtmlEscaped(WiFiClient& client, const char* s)
{
    for (; *s; s++) {
        switch (*s) {
        case '&':  client.print(F("&amp;"));  break;
        case '<':  client.print(F("&lt;"));   break;
        case '>':  client.print(F("&gt;"));   break;
        case '"':  client.print(F("&quot;")); break;
        case '\'': client.print(F("&#39;"));  break;
        default:   client.write(*s);           break;
        }
    }
}

static void sendJsonEscaped(WiFiClient& client, const char* s)
{
    for (; *s; s++) {
        switch (*s) {
        case '\\': client.print(F("\\\\")); break;
        case '"':  client.print(F("\\\"")); break;
        case '\n': client.print(F("\\n"));  break;
        case '\r': client.print(F("\\r"));  break;
        case '\t': client.print(F("\\t"));  break;
        default:   client.write(*s);         break;
        }
    }
}

static void sendWebFormPage(WiFiClient& client, const char* message)
{
    const char* modeLabel = gUseAccessPointMode ? "AP" : "STA";
    const char* targetMode = gUseAccessPointMode ? "STA" : "AP";

    client.println(F("HTTP/1.1 200 OK"));
    client.println(F("Content-Type: text/html; charset=utf-8"));
    client.println(F("Connection: close"));
    client.println();

    client.println(F("<!doctype html>"));
    client.println(F("<html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>"));
    client.print(F("<title>"));
    sendHtmlEscaped(client, WEB_TITLE);
    client.println(F("</title>"));
    client.println(F("<style>body{font-family:Arial,sans-serif;max-width:720px;margin:2rem auto;padding:0 1rem;}input,textarea,button{width:100%;font-size:16px;box-sizing:border-box;margin-top:.5rem;padding:.7rem;}button{cursor:pointer;}label{font-weight:600;display:block;margin-top:1rem;}.msg{margin:1rem 0;padding:.7rem;border:1px solid #8bc28b;background:#eef8ee;}small{display:block;margin-top:.5rem;color:#444;line-height:1.4;}.mode{margin-top:1rem;padding:.6rem;border:1px dashed #999;background:#f8f8f8;}</style>"));
    client.print(F("</head><body><h1>"));
    sendHtmlEscaped(client, WEB_TITLE);
    client.println(F("</h1>"));
    client.print(F("<div class='mode'>Active network mode: <strong>"));
    client.print(modeLabel);
    client.println(F("</strong></div>"));

    if (message[0] != '\0') {
        client.print(F("<div class='msg'>"));
        sendHtmlEscaped(client, message);
        client.println(F("</div>"));
    }

    client.println(F("<form method='POST' action='/'>"));
    client.print(F("<label for='title'>Title</label><input id='title' name='title' type='text' maxlength='63' value='"));
    sendHtmlEscaped(client, gTitleText);
    client.println(F("'>"));

    client.print(F("<label for='content'>Content</label><textarea id='content' name='content' rows='7' maxlength='256'>"));
    sendHtmlEscaped(client, gContentText);
    client.println(F("</textarea>"));

    client.println(F("<small>Supported: \xC2\xA7red\xC2\xA7 (red), _underline_, [bold] (extra bold), {inverse} (highlight), * bullet (left-aligned, indent on wrap), and \\n for a new line. Empty content shows the logo view.</small>"));
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
    IPAddress ip = WiFi.localIP();
    char location[48];
    snprintf(location, sizeof(location), "http://%u.%u.%u.%u/", ip[0], ip[1], ip[2], ip[3]);

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

    // Read and parse the request line
    static char requestLine[128];
    size_t reqLen = client.readBytesUntil('\n', requestLine, sizeof(requestLine) - 1);
    requestLine[reqLen] = '\0';
    if (reqLen > 0 && requestLine[reqLen - 1] == '\r') requestLine[--reqLen] = '\0';

    static char method[8];
    static char path[128];
    method[0] = '\0';
    path[0] = '\0';
    char* sp1 = strchr(requestLine, ' ');
    if (sp1) {
        size_t mLen = static_cast<size_t>(sp1 - requestLine);
        if (mLen >= sizeof(method)) mLen = sizeof(method) - 1;
        memcpy(method, requestLine, mLen);
        method[mLen] = '\0';
        char* sp2 = strchr(sp1 + 1, ' ');
        if (sp2) {
            size_t pLen = static_cast<size_t>(sp2 - (sp1 + 1));
            if (pLen >= sizeof(path)) pLen = sizeof(path) - 1;
            memcpy(path, sp1 + 1, pLen);
            path[pLen] = '\0';
        }
    }

    const bool isGetRoot = (strcmp(method, "GET") == 0 && strcmp(path, "/") == 0);
    const bool isPostRoot = (strcmp(method, "POST") == 0 && strcmp(path, "/") == 0);
    const bool isPostApiUpdate = (strcmp(method, "POST") == 0 && strcmp(path, "/api/update") == 0);

    // Read headers
    int contentLength = 0;
    static char headerLine[128];
    while (client.connected()) {
        size_t hLen = client.readBytesUntil('\n', headerLine, sizeof(headerLine) - 1);
        headerLine[hLen] = '\0';
        if (hLen > 0 && headerLine[hLen - 1] == '\r') headerLine[--hLen] = '\0';
        if (hLen == 0) break;
        // Case-insensitive check for content-length
        char lc[32];
        size_t lcLen = (hLen < 31) ? hLen : 31;
        for (size_t i = 0; i < lcLen; i++) lc[i] = tolower(headerLine[i]);
        lc[lcLen] = '\0';
        if (strncmp(lc, "content-length:", 15) == 0) {
            contentLength = atoi(headerLine + 15);
        }
    }

    const char* message = "";
    bool didUpdate = false;
    bool modeFieldPresent = false;
    bool modeFieldValid = false;
    bool modeAlreadyActive = false;
    bool shouldSwitchMode = false;
    bool switchToApMode = gUseAccessPointMode;

    if ((isPostRoot || isPostApiUpdate) && contentLength > 0) {
        const int bodyLimit = (contentLength > static_cast<int>(kHttpBodyMax)) ? static_cast<int>(kHttpBodyMax) : contentLength;
        static char body[kHttpBodyMax + 1];
        size_t bodyLen = 0;
        int bytesRead = 0;

        const unsigned long readStartMs = millis();
        while (bytesRead < contentLength && (millis() - readStartMs) < kHttpReadTimeoutMs) {
            while (client.available() > 0 && bytesRead < contentLength) {
                const char ch = static_cast<char>(client.read());
                if (static_cast<int>(bodyLen) < bodyLimit) {
                    body[bodyLen++] = ch;
                }
                bytesRead++;
            }
            delay(1);
        }
        body[bodyLen] = '\0';

        const bool hasTitle = hasFormField(body, "title");
        const bool hasContent = hasFormField(body, "content");
        const bool hasStatus = hasFormField(body, "status");
        const bool hasMode = hasFormField(body, "mode");

        static char fieldBuf[kContentTextMax];

        if (hasTitle) {
            getFormField(body, "title", gTitleText, kTitleTextMax);
        }

        if (hasContent) {
            getFormField(body, "content", fieldBuf, kContentTextMax);
            if (isLogoString(fieldBuf)) {
                gContentText[0] = '\0';
            } else {
                strncpy(gContentText, fieldBuf, kContentTextMax - 1);
                gContentText[kContentTextMax - 1] = '\0';
            }
        }

        if (hasStatus) {
            getFormField(body, "status", gStatusText, kStatusTextMax);
            if (strlen(gStatusText) == 0) {
                buildDefaultStatusText();
            }
        }

        if (hasContent && !hasStatus) {
            setStatusToCurrentModeAndIp();
        }

        if (hasMode) {
            modeFieldPresent = true;
            char modeVal[8];
            getFormField(body, "mode", modeVal, sizeof(modeVal));
            for (size_t i = 0; modeVal[i]; i++) modeVal[i] = toupper(modeVal[i]);

            if (strcmp(modeVal, "AP") == 0) {
                modeFieldValid = true;
                switchToApMode = true;
                modeAlreadyActive = gUseAccessPointMode;
                shouldSwitchMode = !modeAlreadyActive;
            } else if (strcmp(modeVal, "STA") == 0) {
                modeFieldValid = true;
                switchToApMode = false;
                modeAlreadyActive = !gUseAccessPointMode;
                shouldSwitchMode = !modeAlreadyActive;
            }
        }

        didUpdate = hasTitle || hasContent || hasStatus;
        if (didUpdate) {
            message = "Display updated via web POST.";
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
        client.println(F("HTTP/1.1 200 OK"));
        client.println(F("Content-Type: application/json; charset=utf-8"));
        client.println(F("Connection: close"));
        client.println();
        client.print(F("{\"ok\":"));
        client.print(didUpdate ? F("true") : F("false"));
        client.print(F(",\"title\":\""));
        sendJsonEscaped(client, gTitleText);
        client.print(F("\",\"content\":\""));
        sendJsonEscaped(client, gContentText);
        client.print(F("\",\"status\":\""));
        sendJsonEscaped(client, gStatusText);
        client.print(F("\",\"message\":\""));
        sendJsonEscaped(client, message);
        client.println(F("\"}"));
    } else if (isGetRoot || isPostRoot) {
        sendWebFormPage(client, message);
    } else if (gUseAccessPointMode && strcmp(method, "GET") == 0) {
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

    // Display refresh and mode switch run after the client is closed so the
    // WiFi SPI bus is idle during the long e-paper refresh cycle.
    if (didUpdate) {
        Serial.println(F("OK: web POST applied."));
        runDisplayCycle();
    }

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
    // At 1:1 scale delegate to the library's optimised routine; the pixel-loop
    // below is only needed when upscaling (scale > 1000 ‰ = 100 %).
    if (scalePermille <= 1000U) {
        Paint_DrawString_EN(xStart, yStart, text, font, WHITE, textColor);
        return;
    }

    const uint8_t bytesPerRow = static_cast<uint8_t>((font->Width / 8) + ((font->Width % 8) ? 1 : 0));
    UWORD cursorX = xStart;

    for (size_t idx = 0; text[idx] != '\0'; idx++) {
        const char ch = text[idx];
        if (ch < ' ' || (uint8_t)ch > 0x7F) {
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

// Called twice per refresh: once with drawRedSegments=false for the black framebuffer,
// once with drawRedSegments=true for the red framebuffer. The e-paper panel requires
// two separate images; splitting here avoids building two full normalized arrays.
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

    // Trim leading and trailing whitespace from rawText without copying to a String
    const char* src = rawText;
    while (*src == ' ' || *src == '\t' || *src == '\r' || *src == '\n') src++;
    const char* srcEnd = src + strlen(src);
    while (srcEnd > src && (*(srcEnd - 1) == ' ' || *(srcEnd - 1) == '\t' || *(srcEnd - 1) == '\r' || *(srcEnd - 1) == '\n')) srcEnd--;
    const size_t srcLen = static_cast<size_t>(srcEnd - src);
    if (srcLen == 0) {
        return;
    }

    static char normalized[kContentTextMax];
    static bool redMask[kContentTextMax];
    static bool boldMask[kContentTextMax];
    static bool inverseMask[kContentTextMax];
    static bool underlineMask[kContentTextMax];
    static bool bulletLineMask[kContentTextMax];
    size_t normalizedLen = 0;
    bool inRedSegment = false;
    bool inBoldSegment = false;
    bool inInverseSegment = false;
    bool inUnderlineSegment = false;
    bool inBulletLine = false;
    bool prevWasSpace = false;

    for (size_t i = 0; i < srcLen && normalizedLen < (kContentTextMax - 1); i++) {
        const char ch = src[i];
        // § is U+00A7, encoded in UTF-8 as two bytes: 0xC2 0xA7.
        if ((uint8_t)ch == 0xC2 && (i + 1) < srcLen && (uint8_t)src[i + 1] == 0xA7) {
            inRedSegment = !inRedSegment;
            i++;
            continue;
        }

        // € is U+20AC, encoded in UTF-8 as three bytes: 0xE2 0x82 0xAC.
        // Map it to 0x7F which holds the euro glyph appended to every font table.
        if ((uint8_t)ch == 0xE2 && (i + 2) < srcLen && (uint8_t)src[i + 1] == 0x82 && (uint8_t)src[i + 2] == 0xAC) {
            normalized[normalizedLen] = '\x7f';
            redMask[normalizedLen] = inRedSegment;
            boldMask[normalizedLen] = inBoldSegment;
            inverseMask[normalizedLen] = inInverseSegment;
            underlineMask[normalizedLen] = inUnderlineSegment;
            bulletLineMask[normalizedLen] = inBulletLine;
            normalizedLen++;
            i += 2;
            continue;
        }

        if (ch == '_') {
            inUnderlineSegment = !inUnderlineSegment;
            continue;
        }

        if (ch == '*') {
            inBulletLine = true;
            if (normalizedLen < (kContentTextMax - 1)) {
                normalized[normalizedLen] = '*';
                redMask[normalizedLen] = false;
                boldMask[normalizedLen] = false;
                inverseMask[normalizedLen] = false;
                underlineMask[normalizedLen] = false;
                bulletLineMask[normalizedLen] = true;
                normalizedLen++;
            }
            continue;
        }

        if (ch == '[' || ch == ']') {
            inBoldSegment = !inBoldSegment;
            continue;
        }

        if (ch == '{' || ch == '}') {
            inInverseSegment = !inInverseSegment;
            continue;
        }

        if (ch == '\\' && (i + 1) < srcLen && src[i + 1] == 'n') {
            while (normalizedLen > 0 && normalized[normalizedLen - 1] == ' ') {
                normalizedLen--;
            }
            if (normalizedLen < (kContentTextMax - 1)) {
                normalized[normalizedLen] = '\n';
                redMask[normalizedLen] = false;
                boldMask[normalizedLen] = false;
                inverseMask[normalizedLen] = false;
                underlineMask[normalizedLen] = false;
                bulletLineMask[normalizedLen] = false;
                normalizedLen++;
            }
            inBulletLine = false;
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
                    underlineMask[normalizedLen] = false;
                    bulletLineMask[normalizedLen] = false;
                    normalizedLen++;
                }
                inBulletLine = false;
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
            underlineMask[normalizedLen] = inUnderlineSegment;
            bulletLineMask[normalizedLen] = inBulletLine;
            normalizedLen++;
            prevWasSpace = true;
            continue;
        }

        normalized[normalizedLen] = ch;
        redMask[normalizedLen] = inRedSegment;
        boldMask[normalizedLen] = inBoldSegment;
        inverseMask[normalizedLen] = inInverseSegment;
        underlineMask[normalizedLen] = inUnderlineSegment;
        bulletLineMask[normalizedLen] = inBulletLine;
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

    static size_t lineStarts[kMaxLinesBuffer];
    static size_t lineEnds[kMaxLinesBuffer];
    static UWORD lineIndentPx[kMaxLinesBuffer];
    static bool lineBullet[kMaxLinesBuffer];
    size_t lineCount = 0;
    size_t pos = 0;
    bool inBulletWrap = false;
    UWORD bulletIndentPx = 0;

    while (pos < normalizedLen && lineCount < kMaxLinesBuffer) {
        if (normalized[pos] == '\n') {
            lineStarts[lineCount] = pos;
            lineEnds[lineCount] = pos;
            lineIndentPx[lineCount] = 0;
            lineBullet[lineCount] = false;
            lineCount++;
            pos++;
            inBulletWrap = false;
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
            lineIndentPx[lineCount] = 0;
            lineBullet[lineCount] = false;
            lineCount++;
            pos++;
            inBulletWrap = false;
            continue;
        }

        const bool lineIsBulletFirst = bulletLineMask[pos] && normalized[pos] == '*';
        const bool lineIsBulletContinuation = inBulletWrap && !lineIsBulletFirst;

        if (lineIsBulletFirst) {
            inBulletWrap = true;
            // 2 chars: the bullet character itself plus the space the user types after it.
            bulletIndentPx = scaledCharWidth * 2;
        }

        const UWORD indentPx = lineIsBulletContinuation ? bulletIndentPx : 0;
        const UWORD effectiveWidth = (indentPx < areaWidth) ? areaWidth - indentPx : scaledCharWidth;
        const size_t effectiveMaxChars = effectiveWidth / scaledCharWidth;

        const size_t lineStart = pos;
        size_t lastSpace = static_cast<size_t>(-1);
        size_t taken = 0;

        while (pos < normalizedLen && taken < effectiveMaxChars && normalized[pos] != '\n') {
            if (normalized[pos] == ' ') {
                lastSpace = pos;
            }
            pos++;
            taken++;
        }

        size_t lineEnd = pos;
        if (pos < normalizedLen && normalized[pos] == '\n') {
            // Respect explicit line break from literal "\\n" in CONTENT.
        } else if (pos < normalizedLen && taken == effectiveMaxChars && normalized[pos] != ' ' && lastSpace != static_cast<size_t>(-1) && lastSpace > lineStart) {
            lineEnd = lastSpace;
            pos = lastSpace + 1;
        }

        while (lineEnd > lineStart && normalized[lineEnd - 1] == ' ') {
            lineEnd--;
        }

        lineStarts[lineCount] = lineStart;
        lineEnds[lineCount] = lineEnd;
        lineIndentPx[lineCount] = indentPx;
        lineBullet[lineCount] = lineIsBulletFirst || lineIsBulletContinuation;
        lineCount++;

        if (pos < normalizedLen && normalized[pos] == '\n') {
            inBulletWrap = false;
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
        const UWORD lineX = lineBullet[lineIndex]
            ? xLeft + lineIndentPx[lineIndex]
            : xLeft + ((areaWidth > lineWidth) ? (areaWidth - lineWidth) / 2 : 0);
        const UWORD lineY = startY + static_cast<UWORD>(lineIndex * scaledCharHeight);

        if (lineLen == 0) {
            continue;
        }

        size_t runStart = lineStart;
        while (runStart < lineEnd) {
            const bool runIsRed = redMask[runStart];
            const bool runIsBold = boldMask[runStart];
            const bool runIsInverse = inverseMask[runStart];
            const bool runIsUnderline = underlineMask[runStart];
            size_t runEnd = runStart + 1;
            while (runEnd < lineEnd && redMask[runEnd] == runIsRed && boldMask[runEnd] == runIsBold && inverseMask[runEnd] == runIsInverse && underlineMask[runEnd] == runIsUnderline) {
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
                    // Draw the glyph four more times, each shifted by boldOffset in one cardinal
                    // direction. The five overlapping copies thicken every stroke uniformly.
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

                if (runIsUnderline) {
                    const UWORD runWidth = static_cast<UWORD>(runLen * scaledCharWidth);
                    const UWORD underlineY = lineY + scaledCharHeight - 1;
                    if (runWidth > 0 && runX < kDisplayWidth && underlineY < kDisplayHeight) {
                        UWORD underlineXEnd = runX + runWidth - 1;
                        if (underlineXEnd >= kDisplayWidth) {
                            underlineXEnd = kDisplayWidth - 1;
                        }
                        Paint_DrawLine(runX, underlineY, underlineXEnd, underlineY, color, LINE_STYLE_SOLID, DOT_PIXEL_3X3);
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

    // Switch to the red framebuffer. The panel sends both images together during
    // EPD_12in48B_Display(); pixels set in both buffers appear darkest (black wins).
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
    Serial.println(F("  CONTENT=<text>  Update content (selected preset, max 256 chars; \xC2\xA7red\xC2\xA7, _underline_, [bold], {inverse}, * bullet (left+indent), \\n line break)"));
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

static void processSerialCommand(const char* input)
{
    while (*input == ' ' || *input == '\t' || *input == '\r' || *input == '\n') input++;
    if (*input == '\0') {
        return;
    }

    static char upper[kSerialLineMax + 1];
    size_t inputLen = strlen(input);
    if (inputLen > kSerialLineMax) inputLen = kSerialLineMax;
    memcpy(upper, input, inputLen);
    upper[inputLen] = '\0';
    while (inputLen > 0 && (upper[inputLen - 1] == ' ' || upper[inputLen - 1] == '\t' || upper[inputLen - 1] == '\r' || upper[inputLen - 1] == '\n')) {
        upper[--inputLen] = '\0';
    }
    for (size_t i = 0; i < inputLen; i++) upper[i] = toupper(upper[i]);

    if (strcmp(upper, "HELP") == 0) {
        printSerialHelp();
        return;
    }

    if (strcmp(upper, "REFRESH") == 0) {
        Serial.println(F("Refreshing display..."));
        runDisplayCycle();
        Serial.println(F("OK: display refreshed."));
        return;
    }

    if (strncmp(upper, "TITLE=", 6) == 0) {
        const char* value = input + 6;
        while (*value == ' ' || *value == '\t') value++;
        strncpy(gTitleText, value, kTitleTextMax - 1);
        gTitleText[kTitleTextMax - 1] = '\0';
        strTrimRight(gTitleText);
        runDisplayCycle();
        Serial.print(F("OK: title="));
        Serial.println(gTitleText);
        return;
    }

    if (strncmp(upper, "CONTENT=", 8) == 0) {
        const char* value = input + 8;
        while (*value == ' ' || *value == '\t') value++;
        static char trimmed[kContentTextMax];
        strncpy(trimmed, value, kContentTextMax - 1);
        trimmed[kContentTextMax - 1] = '\0';
        strTrimRight(trimmed);

        if (isLogoString(trimmed)) {
            gContentText[0] = '\0';
        } else {
            strncpy(gContentText, trimmed, kContentTextMax - 1);
            gContentText[kContentTextMax - 1] = '\0';
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

    if (strncmp(upper, "STATUS=", 7) == 0) {
        if (strcmp(upper + 7, "IP") == 0) {
            setStatusToCurrentIp();
        } else {
            const char* value = input + 7;
            while (*value == ' ' || *value == '\t') value++;
            strncpy(gStatusText, value, kStatusTextMax - 1);
            gStatusText[kStatusTextMax - 1] = '\0';
            strTrimRight(gStatusText);
        }

        runDisplayCycle();
        Serial.print(F("OK: status="));
        Serial.println(gStatusText);
        return;
    }

    if (strcmp(upper, "WIFI=MODE") == 0) {
        Serial.print(F("OK: wifi_mode="));
        Serial.println(activeWifiModeLabel());
        return;
    }

    if (strcmp(upper, "WIFI=AP") == 0) {
        const bool ok = switchNetworkMode(true);
        startWebServerIfConnected();
        Serial.print(F("OK: wifi_mode="));
        Serial.println(ok ? F("AP") : F("AP (failed)"));
        return;
    }

    if (strcmp(upper, "WIFI=STA") == 0) {
        const bool ok = switchNetworkMode(false);
        startWebServerIfConnected();
        Serial.print(F("OK: wifi_mode="));
        Serial.println(ok ? F("STA") : F("STA (failed)"));
        return;
    }

    Serial.print(F("Unknown command: "));
    Serial.println(input);
    Serial.println(F("Type HELP for command list."));
}

static void pollSerialCommands(void)
{
    while (Serial.available() > 0) {
        const char ch = static_cast<char>(Serial.read());
        gLastSerialCharMs = millis();

        if (ch == '\r' || ch == '\n') {
            gSerialLine[gSerialLineLen] = '\0';
            processSerialCommand(gSerialLine);
            gSerialLineLen = 0;
            gSerialLine[0] = '\0';
            continue;
        }

        if (gSerialLineLen < kSerialLineMax) {
            gSerialLine[gSerialLineLen++] = ch;
        }
    }

    // Allow command entry when Serial Monitor is set to "No line ending".
    if (gSerialLineLen > 0 && (millis() - gLastSerialCharMs) >= kSerialIdleProcessMs) {
        gSerialLine[gSerialLineLen] = '\0';
        processSerialCommand(gSerialLine);
        gSerialLineLen = 0;
        gSerialLine[0] = '\0';
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
