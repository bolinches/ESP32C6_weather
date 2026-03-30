#include <Arduino_GFX_Library.h>
#include <WiFi.h>
#include <DNSServer.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <HTTPUpdate.h>
#include <ArduinoJson.h>
#include <WebServer.h>
#include <Preferences.h>
#include <mbedtls/pk.h>
#include <mbedtls/ecdsa.h>
#include <mbedtls/sha256.h>
#include <mbedtls/base64.h>
#include <nvs_flash.h>
#include <esp_timer.h>
#include "time.h"
#include "config.h"
#include "logo.h"
#include "timezones.h"

#define TFT_BL 22 
#define SCK_PIN 7
#define MOSI_PIN 6
#define CS_PIN 14
#define DC_PIN 15
#define RST_PIN 21
#define MAX_ALERTS 5

Arduino_DataBus *bus = new Arduino_ESP32SPI(DC_PIN, CS_PIN, SCK_PIN, MOSI_PIN, -1);
Arduino_GFX *gfx = new Arduino_ST7789(bus, RST_PIN, 1, true, 172, 320, 34, 0, 34, 0);

Preferences prefs;
WebServer server(80);
DNSServer dnsServer;

String apiKey = "";
String city = "";
String lat = "";
String lon = "";
String tzInfo = "";
String ianaTz = "";
String region = "";
String tempUnit = "C";
String owmIp = "";
String apiVer = "3.0";

unsigned long lastOTACheck = 0;
unsigned long lastWeatherUpdate = 0;
unsigned long lastStatusPrint = 0;
unsigned long lastScreenSwap = 0;
unsigned long nextWeatherDelay = 1800000; 

bool pendingReboot = false;
bool pendingReset = false;
bool pendingRecover = false;
unsigned long actionTimer = 0;

int weatherFailStreak = 0;
bool isSetupMode = false;
String webLog = "";
String lastUpdTime = "--:--";
String sunUp = "--:--";
String sunDn = "--:--"; 

float w_curT = 0, w_flsT = 0, w_t3 = 0, w_t6 = 0, w_t9 = 0, w_t24 = 0;
String w_curS = "", w_s3 = "", w_s6 = "", w_s9 = "", w_s24 = "";

bool alertActive = false;
bool testAlertMode = false;
int activeAlertCount = 0;
int currentAlertIndex = 0;
String alertEvent[MAX_ALERTS];
String alertSender[MAX_ALERTS];
String alertDesc[MAX_ALERTS];
String logoDataUri = "";

int displayState = 0; 
unsigned long currentScreenDelay = 10000;
String alertLines[30];
int alertTotalLines = 0;
int alertTotalPages = 0;

String sanitizeInput(String str) {
  String out = "";
  for (int i = 0; i < str.length(); i++) {
    char c = str[i];
    if (c == '<' || c == '>' || c == '"' || c == '\'' || c == '\\' || c == '\r' || c == '\n') continue;
    out += c;
  }
  return out;
}

String sanitizeOutput(String str, bool htmlEscape = false) {
  String out = "";
  int i = 0;
  while (i < str.length()) {
    unsigned char c = str[i];
    if (c < 128) {
      if (htmlEscape) {
        if (c == '<') out += "&lt;";
        else if (c == '>') out += "&gt;";
        else if (c == '&') out += "&amp;";
        else if (c == '"') out += "&quot;";
        else if (c == '\'') out += "&#39;";
        else out += (char)c;
      } else {
        out += (char)c;
      }
      i++;
    } else {
      // Determine if it is a valid multi-byte UTF-8 sequence
      int bytes = 0;
      if ((c & 0xE0) == 0xC0) bytes = 2;
      else if ((c & 0xF0) == 0xE0) bytes = 3;
      else if ((c & 0xF8) == 0xF0) bytes = 4;
      
      bool valid = (bytes > 0 && i + bytes <= str.length());
      for (int j = 1; j < bytes && valid; j++) {
        if ((str[i+j] & 0xC0) != 0x80) valid = false;
      }
      
      if (valid) {
        for (int j = 0; j < bytes; j++) out += str[i+j];
        i += bytes;
      } else {
        // Fix invalid UTF-8 (assumed ISO-8859-1) by translating to UTF-8
        out += (char)(0xC0 | (c >> 6));
        out += (char)(0x80 | (c & 0x3F));
        i++;
      }
    }
  }
  return out;
}

void processAlertText(String text) {
  alertTotalLines = 0;
  String words = text;
  String currentLine = "";
  int maxCharsPerLine = 24;
  
  while (words.length() > 0 && alertTotalLines < 30) {
    int spaceIdx = words.indexOf(' ');
    String word = "";
    if (spaceIdx == -1) { word = words; words = ""; }
    else { word = words.substring(0, spaceIdx); words = words.substring(spaceIdx + 1); }
    
    if (currentLine.length() + word.length() + 1 <= maxCharsPerLine) {
      if (currentLine.length() > 0) currentLine += " ";
      currentLine += word;
    } else {
      alertLines[alertTotalLines++] = currentLine;
      currentLine = word;
    }
  }
  if (currentLine.length() > 0 && alertTotalLines < 30) {
    alertLines[alertTotalLines++] = currentLine;
  }
  
  int linesPerPage = 6;
  alertTotalPages = (alertTotalLines + linesPerPage - 1) / linesPerPage; 
  if (alertTotalPages > 5) alertTotalPages = 5; 
}

String generateLogoDataURI() {
  int width = COLYFLOR_WIDTH;
  int height = COLYFLOR_HEIGHT;
  int row_size = width * 3;
  int padded_row_size = (row_size + 3) & ~3;
  int image_size = padded_row_size * height;
  int file_size = 54 + image_size;

  unsigned char bmp_header[54] = {
    'B', 'M',
    (unsigned char)(file_size), (unsigned char)(file_size >> 8), (unsigned char)(file_size >> 16), (unsigned char)(file_size >> 24),
    0, 0, 0, 0,
    54, 0, 0, 0,
    40, 0, 0, 0,
    (unsigned char)(width), (unsigned char)(width >> 8), (unsigned char)(width >> 16), (unsigned char)(width >> 24),
    (unsigned char)(height), (unsigned char)(height >> 8), (unsigned char)(height >> 16), (unsigned char)(height >> 24),
    1, 0,
    24, 0,
    0, 0, 0, 0,
    (unsigned char)(image_size), (unsigned char)(image_size >> 8), (unsigned char)(image_size >> 16), (unsigned char)(image_size >> 24),
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
  };

  unsigned char* bmp_data = (unsigned char*)malloc(image_size);
  if (!bmp_data) return "";
  
  memset(bmp_data, 0, image_size);

  for (int y = 0; y < height; y++) {
    for (int x = 0; x < width; x++) {
      int bmp_y = height - 1 - y;
      uint16_t color16 = colyflor[y * width + x];
      
      uint8_t r = (color16 & 0xF800) >> 11;
      uint8_t g = (color16 & 0x07E0) >> 5;
      uint8_t b = (color16 & 0x001F);

      uint8_t r8 = r << 3;
      uint8_t g8 = g << 2;
      uint8_t b8 = b << 3;

      int bmp_x = x * 3;
      bmp_data[bmp_y * padded_row_size + bmp_x] = b8;
      bmp_data[bmp_y * padded_row_size + bmp_x + 1] = g8;
      bmp_data[bmp_y * padded_row_size + bmp_x + 2] = r8;
    }
  }

  size_t total_size = sizeof(bmp_header) + image_size;
  unsigned char* full_bmp = (unsigned char*)malloc(total_size);
  if (!full_bmp) {
      free(bmp_data);
      return "";
  }
  memcpy(full_bmp, bmp_header, sizeof(bmp_header));
  memcpy(full_bmp + sizeof(bmp_header), bmp_data, image_size);
  free(bmp_data);

  size_t output_len;
  mbedtls_base64_encode(NULL, 0, &output_len, full_bmp, total_size);
  
  unsigned char* base64_buf = (unsigned char*)malloc(output_len + 1);
  if (!base64_buf) {
    free(full_bmp);
    return "";
  }
  
  mbedtls_base64_encode(base64_buf, output_len, &output_len, full_bmp, total_size);
  base64_buf[output_len] = '\0';
  
  String data_uri = "data:image/bmp;base64," + String((char*)base64_buf);

  free(full_bmp);
  free(base64_buf);
  return data_uri;
}

void logMsg(String m) {
  struct tm ti;
  String ts = "[00:00:00 0000:00:00] ";
  if (getLocalTime(&ti)) {
    char buf[32];
    snprintf(buf, sizeof(buf), "[%02d:%02d:%02d %04d:%02d:%02d] ", 
             ti.tm_hour, ti.tm_min, ti.tm_sec, ti.tm_year + 1900, ti.tm_mon + 1, ti.tm_mday);
    ts = String(buf);
  }
  
  String finalLine = ts + m;
  Serial.println(sanitizeOutput(finalLine, false));
  
  webLog += sanitizeOutput(finalLine, true) + "\n";
  
  if (webLog.length() > 8000) {
    int excess = webLog.length() - 8000;
    int cutIdx = webLog.indexOf('\n', excess);
    if (cutIdx != -1) webLog = webLog.substring(cutIdx + 1);
    else webLog = webLog.substring(excess);
  }
}

void showBootStatus(const char* msg) {
  gfx->fillRect(0, 140, 320, 32, 0x0000); 
  gfx->setTextSize(2); 
  gfx->setTextColor(0xFFE0); 
  int16_t x1, y1; uint16_t w, h;
  gfx->getTextBounds(msg, 0, 0, &x1, &y1, &w, &h);
  gfx->setCursor((320 - w) / 2, 145);
  gfx->print(msg);
  Serial.print("[LCD] "); Serial.println(msg);
}

void updateBacklight() {
  struct tm ti;
  int pct = BRIGHTNESS_DAY_PCT;
  if (getLocalTime(&ti)) {
    int h = ti.tm_hour;
    if (h >= NIGHT_MODE_START || h < NIGHT_MODE_END) pct = BRIGHTNESS_NIGHT_PCT;
  }
  ledcWrite(TFT_BL, (pct * 255) / 100);
}

int cRnd(float v) { return (v >= 0) ? (int)(v + 0.5) : (int)(v - 0.5); }

bool addOrUpdateWiFi(String s, String p) {
  int targetSlot = -1;
  for (int i = 0; i < 5; i++) {
    if (prefs.getString(("ssid" + String(i)).c_str(), "") == s) { targetSlot = i; break; }
  }
  if (targetSlot == -1) {
    for (int i = 0; i < 5; i++) {
      if (prefs.getString(("ssid" + String(i)).c_str(), "") == "") { targetSlot = i; break; }
    }
  }
  if (targetSlot != -1) {
    prefs.putString(("ssid" + String(targetSlot)).c_str(), s);
    prefs.putString(("pass" + String(targetSlot)).c_str(), p);
    return true;
  }
  return false; 
}

void loadPreferences() {
  prefs.begin("colyflor", false);
  apiKey = prefs.getString("api", "");
  city = prefs.getString("city", "");
  lat = prefs.getString("lat", "");
  lon = prefs.getString("lon", "");
  region = prefs.getString("region", "Unknown");
  tzInfo = prefs.getString("tz", "EET-2EEST,M3.5.0/3,M10.5.0/4");
  ianaTz = prefs.getString("iana_tz", "");
  apiVer = prefs.getString("api_ver", "3.0");
  owmIp = prefs.getString("owm_ip", "");
  tempUnit = prefs.getString("unit", "C");
  logMsg("NVRAM: Loaded API Target: v" + apiVer);
  if (owmIp != "") logMsg("NVRAM: Cached OWM IP: " + owmIp);
}

bool connectTargetedWiFi() {
  logMsg("WIFI: Executing environment scan...");
  WiFi.disconnect();
  delay(100);
  int n = WiFi.scanNetworks();
  
  if (n == 0) {
    logMsg("WIFI: No networks detected in range.");
    return false;
  }

  for (int i = 0; i < n; ++i) {
    String scannedSSID = WiFi.SSID(i);
    int32_t rssi = WiFi.RSSI(i);
    
    for (int j = 0; j < 5; j++) {
      String storedSSID = prefs.getString(("ssid" + String(j)).c_str(), "");
      if (storedSSID != "" && storedSSID == scannedSSID) {
        logMsg("WIFI: Detected known SSID [" + storedSSID + "] at " + String(rssi) + "dBm");
        showBootStatus(("Join: " + storedSSID).c_str());
        
        String storedPass = prefs.getString(("pass" + String(j)).c_str(), "");
        WiFi.begin(storedSSID.c_str(), storedPass.c_str());
        
        unsigned long start = millis();
        while (WiFi.status() != WL_CONNECTED && millis() - start < 10000) {
          delay(500);
        }
        
        if (WiFi.status() == WL_CONNECTED) {
          logMsg("WIFI: Successfully connected to " + storedSSID);
          delay(1000); // Allow DHCP and routing to fully stabilize before making HTTP calls
          return true;
        } else {
          logMsg("WIFI: Connection timeout for " + storedSSID);
        }
      }
    }
  }
  
  logMsg("WIFI: Scan complete. No active stored networks connected.");
  return false;
}

String urlEncode(String str) {
  String encodedString="";
  char c;
  char code0;
  char code1;
  for (int i = 0; i < str.length(); i++) {
    char c = str.charAt(i);
    if (c == ' ') {
      encodedString += '+';
    } else if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
      encodedString += c;
    } else {
      code1=(c & 0xf)+'0';
      if ((c & 0xf) > 9) {
        code1=(c & 0xf) - 10 + 'A';
      }
      c=(c>>4)&0xf;
      code0=c+'0';
      if (c > 9) {
        code0=c - 10 + 'A';
      }
      encodedString+='%';
      encodedString+=code0;
      encodedString+=code1;
    }
  }
  return encodedString;
}

bool geocodeCity(String qCity, String qApi) {
  qCity.trim();
  logMsg("GEO: Requesting coordinates for '" + qCity + "' [" + urlEncode(qCity) + "]...");
  HTTPClient http;
  http.begin("http://api.openweathermap.org/geo/1.0/direct?q=" + urlEncode(qCity) + "&limit=1&appid=" + qApi);
  http.addHeader("User-Agent", "COLYFLOR-Weather/1.6");
  int code = http.GET();
  if (code == 200) {
    DynamicJsonDocument doc(2048); deserializeJson(doc, http.getStream());
    if (doc.size() > 0) {
      lat = doc[0]["lat"].as<String>(); lon = doc[0]["lon"].as<String>();
      String st = doc[0]["state"] | ""; String co = doc[0]["country"] | "";
      region = (st != "") ? st + ", " + co : co;
      prefs.putString("lat", lat); prefs.putString("lon", lon); prefs.putString("region", region);
      logMsg("GEO: Success. Lat: " + lat + ", Lon: " + lon + " (" + region + ")");
      http.end(); return true;
    } else {
      logMsg("GEO: ERROR -> City not found by OpenWeather API.");
    }
  } else {
    logMsg("GEO: ERROR -> HTTP " + String(code));
    if (code == 401) logMsg("GEO: HINT -> HTTP 401 means the API key is invalid or hasn't activated yet (can take 2+ hours for new keys).");
  }
  http.end(); return false;
}

void startCaptivePortal() {
  if (WiFi.status() == WL_CONNECTED) {
    WiFi.mode(WIFI_AP_STA); // Keep internet connection active for console debugging!
  } else {
    WiFi.mode(WIFI_AP);
  }
  WiFi.softAP("COLYFLOR_SETUP");
  showBootStatus("AP: COLYFLOR_SETUP"); dnsServer.start(53, "*", WiFi.softAPIP());
  
  server.onNotFound([]() {
    String html = "<html><head><meta charset=\"UTF-8\"><meta name='viewport' content='width=device-width,initial-scale=1'>";
    html += "<script>window.onload=function(){document.getElementById('iana_tz').value=Intl.DateTimeFormat().resolvedOptions().timeZone;}</script><style>";
    html += "body{font-family:sans-serif;padding:20px;background:#111;color:#fff;text-align:center;}";
    html += "form{max-width:400px;margin:0 auto;text-align:left;}";
    html += "input,select{width:100%;padding:10px;margin:5px 0 15px;background:#222;color:#fff;border:1px solid #444;border-radius:4px;box-sizing:border-box;}";
    html += "button{width:100%;padding:12px;background:#0f0;color:#000;font-weight:bold;border:none;border-radius:4px}</style></head><body>";
    html += "<h2>COLYFLOR Setup</h2><form method='POST' action='/save' accept-charset='UTF-8'>";
    html += "<label>WiFi SSID</label><input type='text' name='s' required>";
    html += "<label>WiFi Password</label><input type='password' name='p'>";
    html += "<input type='hidden' id='iana_tz' name='iana_tz'>";
    html += "<label>OpenWeather API Key</label><input type='text' name='k' required>";
    html += "<label>City Name</label><input type='text' name='c' required>";
    html += "<label>Unit</label><select name='u'><option value='C' selected>&deg;C (Celsius)</option><option value='F'>&deg;F (Fahrenheit)</option></select>";
    html += "<button type='submit'>Save & Restart</button></form></body></html>";
    server.send(200, "text/html", html);
  });
  
  server.on("/save", HTTP_POST, []() {
    String pass = server.arg("p");
    String newApiKey = sanitizeInput(server.arg("k"));
    String newCity = sanitizeInput(server.arg("c"));

    String error_page_start = "<html><head><meta charset=\"UTF-8\"><meta name='viewport' content='width=device-width,initial-scale=1'></head><body style='font-family:sans-serif;padding:20px;background:#111;color:#fff'><h2>Error</h2><p>";
    String error_page_end = "</p><br><button style='width:100%;padding:12px;background:#f00;color:#fff;font-weight:bold;border:none;border-radius:4px' onclick='history.back()'>Go Back</button></body></html>";

    if (hasForbiddenChars(pass)) {
      server.send(400, "text/html", error_page_start + "Password contains forbidden characters: &lt; &gt; &quot; &#39; \\" + error_page_end);
    } else if (!isValidApiKey(newApiKey)) {
      server.send(400, "text/html", error_page_start + "Invalid API Key format. It must be 32 hexadecimal characters." + error_page_end);
    } else if (newCity == "") {
      server.send(400, "text/html", error_page_start + "City name cannot be empty." + error_page_end);
    } else {
      addOrUpdateWiFi(sanitizeInput(server.arg("s")), pass);
      prefs.putString("api", newApiKey);
      prefs.putString("city", newCity);
      String iana = server.arg("iana_tz");
      prefs.putString("iana_tz", iana);
      prefs.putString("tz", getPosixFromIana(iana)); // Store a best-guess POSIX as a fallback
      prefs.putString("unit", sanitizeInput(server.arg("u")));
      server.send(200, "text/html", "<html><head><meta charset=\"UTF-8\"><meta name='viewport' content='width=device-width,initial-scale=1'></head><body style='font-family:sans-serif;padding:20px;background:#111;color:#0f0'><h2>Saved. Rebooting...</h2></body></html>");
      delay(2000); ESP.restart();
    }
  });
  
  // Also enable the standard web console during setup mode
  server.on("/console", [](){
    server.setContentLength(CONTENT_LENGTH_UNKNOWN);
    server.send(200, "text/html; charset=utf-8", "");
    String part1 = "<html><head><meta charset=\"UTF-8\"><meta http-equiv='refresh' content='30'><meta name='viewport' content='width=device-width,initial-scale=1'><style>body{background:#000;color:#0f0;font-family:monospace;padding:20px;text-align:center}pre{height:70vh;overflow-y:scroll;border:1px solid #222;padding:10px;text-align:left;white-space:pre-wrap}input{width:100%;background:#111;color:#0f0;border:1px solid #0f0;padding:10px;margin-top:10px}.logo{width:80px;height:80px;margin-bottom:10px}</style>";
    part1 += "<script>window.onload=function(){var p=document.getElementById('log');p.scrollTop=p.scrollHeight;}</script></head><body><img src='";
    server.sendContent(part1);
    server.sendContent(logoDataUri);
    server.sendContent("' class='logo'><h2>CONSOLE v" + String(CURRENT_VERSION) + " (Setup)</h2><pre id='log'>");
    server.sendContent(webLog);
    server.sendContent("</pre><form action='/cmd' method='POST' accept-charset=\"UTF-8\"><input name='c' placeholder='Type command...' autofocus autocomplete='off'></form></body></html>");
    server.sendContent("");
  });
  
  server.on("/cmd", HTTP_POST, [](){ if(server.hasArg("c")) pCmd(sanitizeInput(server.arg("c"))); server.sendHeader("Location", "/console"); server.send(303); });

  server.begin();
  logMsg("SETUP MODE: Connect to 'COLYFLOR_SETUP' WiFi.");
  logMsg("SETUP MODE: Go to any website for setup, or http://192.168.4.1/console for web terminal.");
  logMsg("SETUP MODE: Serial commands are also available.");

  while(true) { 
    dnsServer.processNextRequest(); 
    server.handleClient(); 
    if (Serial.available()) {
      pCmd(sanitizeInput(Serial.readStringUntil('\n')));
    }
    // Handle scheduled actions since we are trapped in this loop and won't return to the main loop().
    if (pendingReboot && (millis() - actionTimer >= 1500)) ESP.restart();
    if (pendingReset && (millis() - actionTimer >= 1500)) { prefs.clear(); ESP.restart(); }
    delay(10); 
  }
}

void drawWeatherUI() {
  gfx->fillScreen(0x0000);
  gfx->drawRect(0, 0, 320, 172, 0xFFFF);
  
  gfx->setTextSize(2); gfx->setTextColor(0xFDA0); gfx->setCursor(12, 10);
  gfx->printf("UP:%s DN:%s", sunUp.c_str(), sunDn.c_str()); 
  
  gfx->setTextColor(0x07E0);
  int16_t x1, y1; uint16_t w, h;
  gfx->getTextBounds(lastUpdTime, 0, 0, &x1, &y1, &w, &h);
  gfx->setCursor(308 - w, 10); gfx->print(lastUpdTime);
  
  gfx->setTextColor(0xFFFF); gfx->setTextSize(6); gfx->setCursor(12, 40);
  gfx->printf("%d %s", cRnd(w_curT), tempUnit.c_str());
  
  gfx->setTextColor(0xFDA0); gfx->setTextSize(2);
  gfx->setCursor(180, 42); gfx->print(w_curS.substring(0, 10));
  gfx->setCursor(180, 65); gfx->printf("Feels %d %s", cRnd(w_flsT), tempUnit.c_str());
  
  gfx->drawFastHLine(12, 95, 296, 0x4208);
  
  gfx->setTextColor(0xFFE0); 
  gfx->setCursor(12, 102); gfx->printf("+3H:  %d %s", cRnd(w_t3), tempUnit.c_str());
  String st3 = w_s3.substring(0, 12); gfx->getTextBounds(st3, 0, 0, &x1, &y1, &w, &h);
  gfx->setCursor(308 - w, 102); gfx->print(st3);

  gfx->setCursor(12, 119); gfx->printf("+6H:  %d %s", cRnd(w_t6), tempUnit.c_str());
  String st6 = w_s6.substring(0, 12); gfx->getTextBounds(st6, 0, 0, &x1, &y1, &w, &h);
  gfx->setCursor(308 - w, 119); gfx->print(st6);

  gfx->setCursor(12, 136); gfx->printf("+9H:  %d %s", cRnd(w_t9), tempUnit.c_str());
  String st9 = w_s9.substring(0, 12); gfx->getTextBounds(st9, 0, 0, &x1, &y1, &w, &h);
  gfx->setCursor(308 - w, 136); gfx->print(st9);

  gfx->setCursor(12, 153); gfx->printf("+24H: %d %s", cRnd(w_t24), tempUnit.c_str());
  String st24 = w_s24.substring(0, 12); gfx->getTextBounds(st24, 0, 0, &x1, &y1, &w, &h);
  gfx->setCursor(308 - w, 153); gfx->print(st24);
}

void drawAlertUI() {
  gfx->fillScreen(0x0000);
  gfx->drawRect(0, 0, 320, 172, 0xF800); 
  
  gfx->setTextColor(0xF800); gfx->setTextSize(2); 
  gfx->setCursor(12, 10); 
  
  String aEvent = alertEvent[currentAlertIndex];
  
  String header = "";
  if (activeAlertCount > 1) header += "[" + String(currentAlertIndex + 1) + "/" + String(activeAlertCount) + "] ";
  if (alertTotalPages > 1) header += String(displayState) + "/" + String(alertTotalPages) + " ";
  header += "WX ALERT";
  if (header == "WX ALERT") header = "! WEATHER ALERT !";
  
  gfx->print(header);
  
  int yPos = 35;
  gfx->setTextColor(0xFFFF); 
  gfx->setCursor(12, yPos); 
  gfx->print(aEvent.substring(0, 24));
  
  yPos += 25;
  gfx->setTextColor(0xFFE0); 
  
  int linesPerPage = 6;
  int startIdx = (displayState - 1) * linesPerPage;
  int endIdx = startIdx + linesPerPage;
  if (endIdx > alertTotalLines) endIdx = alertTotalLines;
  
  for (int i = startIdx; i < endIdx; i++) {
    gfx->setCursor(12, yPos);
    gfx->print(alertLines[i]);
    yPos += 18;
  }
}

void renderCurrentScreen() {
  if (displayState > 0 && (alertActive || testAlertMode)) {
    drawAlertUI();
  } else {
    drawWeatherUI();
  }
}

bool runDeepProbe() {
  logMsg("PROBE: Executing network diagnostics...");
  IPAddress p1, p2, p3;
  bool d1 = WiFi.hostByName("api.openweathermap.org", p1);
  if (d1) {
    String newIp = p1.toString();
    if (newIp != "0.0.0.0" && newIp != owmIp) {
      logMsg("PROBE: OpenWeatherMap IP resolved to " + newIp + ". Updating NVRAM.");
      owmIp = newIp;
      prefs.putString("owm_ip", owmIp);
    }
  }
  bool d2 = WiFi.hostByName("www.colyflor.com", p2);
  bool d3 = WiFi.hostByName("worldtimeapi.org", p3);
  WiFiClient c; c.setTimeout(1500);
  bool t = c.connect("1.1.1.1", 80); c.stop();
  
  if (!d1) logMsg("PROBE: CRITICAL -> OpenWeatherMap DNS failed.");
  if (!t)  logMsg("PROBE: CRITICAL -> TCP routing to 1.1.1.1 failed.");
  if (!d2) logMsg("PROBE: WARNING -> OTA manifest domain unreachable.");
  if (!d3) logMsg("PROBE: WARNING -> WorldTimeAPI domain unreachable (Blocked?).");
  
  return (d1 && t); // Only fail recovery if primary API or basic routing is down
}

void performNetworkRecovery(int streak, bool force) {
  if (!force && runDeepProbe()) { weatherFailStreak = 0; return; }
  if (streak == 1) { 
    logMsg("RECOVERY: T1 Targeted Rescan"); 
    connectTargetedWiFi(); 
  } 
  else if (streak == 2) { 
    logMsg("RECOVERY: T2 Radio Reset"); 
    WiFi.mode(WIFI_OFF); 
    delay(2000); 
    WiFi.mode(WIFI_STA); 
    connectTargetedWiFi(); 
  } 
  else if (streak >= 3) { 
    logMsg("RECOVERY: T3 Reboot"); 
    ESP.restart(); 
  }
}

bool verifySignature(String data, String sigB64) {
  uint8_t sig[128]; size_t sLen;
  if (mbedtls_base64_decode(sig, 128, &sLen, (const uint8_t*)sigB64.c_str(), sigB64.length()) != 0) return false;
  uint8_t hash[32]; mbedtls_sha256((const uint8_t*)data.c_str(), data.length(), hash, 0);
  mbedtls_pk_context pk; mbedtls_pk_init(&pk);
  uint8_t key[65]; key[0] = 0x04; memcpy(key + 1, PUBLIC_KEY, 64);
  mbedtls_pk_setup(&pk, mbedtls_pk_info_from_type(MBEDTLS_PK_ECKEY));
  mbedtls_ecp_keypair *eck = (mbedtls_ecp_keypair *)pk.MBEDTLS_PRIVATE(pk_ctx);
  mbedtls_ecp_group_load(&(eck->MBEDTLS_PRIVATE(grp)), MBEDTLS_ECP_DP_SECP256R1);
  mbedtls_ecp_point_read_binary(&(eck->MBEDTLS_PRIVATE(grp)), &(eck->MBEDTLS_PRIVATE(Q)), key, 65);
  int r = mbedtls_pk_verify(&pk, MBEDTLS_MD_SHA256, hash, 32, sig, sLen);
  mbedtls_pk_free(&pk); return r == 0;
}

int compareVersion(String localV, String remoteV) {
  float loc = localV.toFloat(); float rem = remoteV.toFloat();
  if (rem > loc) return 1; if (rem < loc) return -1; return 0;
}

void runOTA(bool force) {
  logMsg("OTA: Target URL -> " + String(manifestUrl));
  WiFiClientSecure client; client.setInsecure(); HTTPClient http;
  
  logMsg("OTA: Executing Manifest Fetch...");
  if (http.begin(client, manifestUrl)) {
    int code = http.GET();
    logMsg("OTA: Manifest Request -> HTTP " + String(code));
    
    if (code == 200) {
      DynamicJsonDocument doc(2048); deserializeJson(doc, http.getStream());
      String nV = doc["version"] | "0.0"; String binUrl = doc["url"] | "";
      String hash = doc["bin_hash"] | "N/A"; size_t expectedSize = doc["size"] | 0;
      bool manifestForce = doc["force"] | false;
      
      logMsg("OTA: Local Version: v" + String(CURRENT_VERSION));
      logMsg("OTA: Remote Version: v" + nV);
      
      int vCheck = compareVersion(String(CURRENT_VERSION), nV);
      bool proceed = false;
      
      if (vCheck > 0) {
        logMsg("OTA: EVAL -> Newer version detected.");
        proceed = true;
      } else if (force || manifestForce) {
        logMsg("OTA: EVAL -> Forced execution override active.");
        proceed = true;
      } else {
        logMsg("OTA: EVAL -> System up to date. Execution aborted.");
      }
      
      if (proceed) {
        size_t freeSpace = ESP.getFreeSketchSpace();
        logMsg("OTA: Integrity -> Expected Size: " + String(expectedSize) + " bytes | Free Partition: " + String(freeSpace) + " bytes");
        
        if (expectedSize > 0 && expectedSize > freeSpace) { 
          logMsg("OTA: CRITICAL ERROR -> Binary too large for partition."); 
          http.end(); 
          return; 
        }
        
        logMsg("OTA: Security -> Verifying ECDSA Signature...");
        if (verifySignature(nV + "|" + hash, doc["signature"] | "")) {
          logMsg("OTA: Security -> Signature VALID.");
          logMsg("OTA: Network -> Binary URL resolved: " + binUrl);
          logMsg("OTA: STATUS -> STARTING BINARY DOWNLOAD...");
          
          httpUpdate.rebootOnUpdate(false);
          t_httpUpdate_return ret = httpUpdate.update(client, binUrl);
          
          if (ret == HTTP_UPDATE_OK) { 
            logMsg("OTA: SUCCESS -> Binary fully verified and written. Rebooting."); 
            delay(2000); 
            ESP.restart(); 
          }
          else { 
            logMsg("OTA: ERROR -> Flash/Download failed: " + httpUpdate.getLastErrorString()); 
          }
        } else {
          logMsg("OTA: SECURITY ERROR -> ECDSA Signature Mismatch.");
        }
      }
    } else {
      logMsg("OTA: ERROR -> Failed to retrieve manifest.");
    }
    http.end();
  } else {
    logMsg("OTA: ERROR -> Secure client connection to host failed.");
  }
}

void syncTime() {
  logMsg("NTP: Syncing..."); configTzTime(tzInfo.c_str(), ntpServer);
  unsigned long start = millis();
  while (time(nullptr) < 100000 && millis() - start < 10000) delay(100);
  struct tm ti;
  if (getLocalTime(&ti)) {
     char b[32]; strftime(b, 32, "%H:%M:%S %Y:%m:%d", &ti); logMsg("NTP: OK " + String(b)); updateBacklight();
  } else { logMsg("NTP: FAILED."); }
}

bool updateWeather(bool draw) {
  if (lat == "" || lon == "") return false;
  
  String apiUnit = (tempUnit == "F") ? "imperial" : "metric";
  HTTPClient http;
  String host = "api.openweathermap.org";
  String url;
  
  IPAddress hostIp; // Temporary variable for DNS resolution
  bool dnsResolved = WiFi.hostByName(host.c_str(), hostIp);

  if (dnsResolved) {
    String resolvedIp = hostIp.toString();
    if (resolvedIp != "0.0.0.0") { // Valid IP resolved
      if (owmIp == "") { // First time resolving or previous cache was empty
        logMsg("API: OWM IP resolved to " + resolvedIp + ". Caching in NVRAM.");
        owmIp = resolvedIp;
        prefs.putString("owm_ip", owmIp);
      } else if (resolvedIp != owmIp) { // IP changed
        logMsg("API: OWM IP changed from " + owmIp + " to " + resolvedIp + ". Updating NVRAM.");
        owmIp = resolvedIp;
        prefs.putString("owm_ip", owmIp);
      } else { // IP is the same
        logMsg("API: OWM IP still " + resolvedIp + " (cached).");
      }
    } else { // hostByName succeeded but returned 0.0.0.0, which is usually a failure
      logMsg("API: WARNING -> DNS resolved " + host + " to 0.0.0.0. Proceeding with cached IP if available.");
    }
  }
  else {
    logMsg("API: CRITICAL -> DNS resolution for " + host + " failed. Proceeding with cached IP if available.");
  }

  execute_request:
  if (apiVer == "3.0") {
    url = "http://" + host + "/data/3.0/onecall?lat=" + lat + "&lon=" + lon + "&exclude=minutely,daily&units=" + apiUnit + "&appid=" + apiKey;
  } else {
    url = "http://" + host + "/data/2.5/forecast?lat=" + lat + "&lon=" + lon + "&units=" + apiUnit + "&appid=" + apiKey;
  }

  http.begin(url);
  http.addHeader("User-Agent", "COLYFLOR-Weather/1.6");
  int code = http.GET();
  
  if (code < 0 && owmIp != "") {
    logMsg("API: Host request failed (" + String(code) + "). Falling back to direct IP: " + owmIp);
    http.end();
    
    if (apiVer == "3.0") {
      url = "http://" + owmIp + "/data/3.0/onecall?lat=" + lat + "&lon=" + lon + "&exclude=minutely,daily&units=" + apiUnit + "&appid=" + apiKey;
    } else {
      url = "http://" + owmIp + "/data/2.5/forecast?lat=" + lat + "&lon=" + lon + "&units=" + apiUnit + "&appid=" + apiKey;
    }
    
    http.begin(url);
    http.addHeader("Host", host);
    http.addHeader("User-Agent", "COLYFLOR-Weather/1.6");
    code = http.GET();
  }

  if ((code == 401 || code == 404) && apiVer == "3.0") {
    logMsg("API: 3.0 Subscription validation failed (" + String(code) + "). Executing 2.5 Downgrade.");
    apiVer = "2.5";
    prefs.putString("api_ver", apiVer);
    http.end();
    goto execute_request; 
  }

  if (code == 200) {
    weatherFailStreak = 0; 
    DynamicJsonDocument doc(24576); 
    deserializeJson(doc, http.getStream());
    
    struct tm ti; 
    if (getLocalTime(&ti)) { 
      char b[8]; snprintf(b, 8, "%02d:%02d", ti.tm_hour, ti.tm_min); lastUpdTime = String(b); 
    }

    time_t sr, ss;

        if (apiVer == "3.0") {
          w_curT = doc["current"]["temp"]; w_flsT = doc["current"]["feels_like"];
          w_curS = doc["current"]["weather"][0]["main"].as<String>();
          sr = doc["current"]["sunrise"]; ss = doc["current"]["sunset"];
          
          w_t3 = doc["hourly"][3]["temp"]; w_s3 = doc["hourly"][3]["weather"][0]["main"].as<String>();
          w_t6 = doc["hourly"][6]["temp"]; w_s6 = doc["hourly"][6]["weather"][0]["main"].as<String>();
          w_t9 = doc["hourly"][9]["temp"]; w_s9 = doc["hourly"][9]["weather"][0]["main"].as<String>();
          w_t24 = doc["hourly"][24]["temp"]; w_s24 = doc["hourly"][24]["weather"][0]["main"].as<String>();
    
          if (doc.containsKey("alerts") && doc["alerts"].size() > 0) {
            alertActive = true;
            activeAlertCount = doc["alerts"].size();
            if (activeAlertCount > MAX_ALERTS) activeAlertCount = MAX_ALERTS;
            
            for (int i = 0; i < activeAlertCount; i++) {
              alertSender[i] = doc["alerts"][i]["sender_name"].as<String>();
              alertEvent[i] = doc["alerts"][i]["event"].as<String>();
              alertDesc[i] = doc["alerts"][i]["description"].as<String>();
            }
            
            if (currentAlertIndex >= activeAlertCount) {
              displayState = 0;
              currentAlertIndex = 0;
            }
            if (displayState > 0) {
              processAlertText(alertDesc[currentAlertIndex]);
            }
          } else {
            alertActive = false;
            activeAlertCount = 0;
            displayState = 0;
            currentAlertIndex = 0;
          }
          logMsg("API: Weather updated for " + city + ". Temp: " + String(w_curT) + tempUnit + ", Feels: " + String(w_flsT) + tempUnit + ", Sky: " + w_curS + ", Alerts: " + String(activeAlertCount) + ", Using API v3.0");
        } else {
          JsonArray list = doc["list"];
          w_curT = list[0]["main"]["temp"]; w_flsT = list[0]["main"]["feels_like"];
          w_curS = list[0]["weather"][0]["main"].as<String>();
          sr = doc["city"]["sunrise"]; ss = doc["city"]["sunset"];
          
          w_t3 = list[1]["main"]["temp"]; w_s3 = list[1]["weather"][0]["main"].as<String>();
          w_t6 = list[2]["main"]["temp"]; w_s6 = list[2]["weather"][0]["main"].as<String>();
          w_t9 = list[3]["main"]["temp"]; w_s9 = list[3]["weather"][0]["main"].as<String>();
          w_t24 = list[8]["main"]["temp"]; w_s24 = list[8]["weather"][0]["main"].as<String>();
          
          alertActive = false;
          activeAlertCount = 0;
          displayState = 0;
          currentAlertIndex = 0;
          String responseCity = doc["city"]["name"].as<String>();
          logMsg("API: Weather updated for " + responseCity + ". Temp: " + String(w_curT) + tempUnit + ", Feels: " + String(w_flsT) + tempUnit + ", Sky: " + w_curS + ", Using API v2.5");
        }
    
        struct tm *tmSr = localtime(&sr); char bR[8]; snprintf(bR, 8, "%02d:%02d", tmSr->tm_hour, tmSr->tm_min); sunUp = bR;
        struct tm *tmSs = localtime(&ss); char bS[8]; snprintf(bS, 8, "%02d:%02d", tmSs->tm_hour, tmSs->tm_min); sunDn = bS;    
    if (draw) renderCurrentScreen();
    http.end(); 
    return true;
  }

  logMsg("API: ERROR -> Final request attempt failed with code " + String(code));
  weatherFailStreak++;
  http.end();
  performNetworkRecovery(weatherFailStreak, false); 
  return false;
}

bool updatePosixFromIana(String iana) {
  if (iana == "") {
    logMsg("TZ: No IANA timezone set in NVRAM. Skipping dynamic update.");
    return false;
  }
  logMsg("TZ: Resolving POSIX string for IANA zone: " + iana);
  HTTPClient http;
  String url = "http://worldtimeapi.org/api/timezone/" + iana;
  http.setConnectTimeout(4000);
  
  int code = 0;
  int retries = 0;
  while (retries < 2 && code <= 0) { // Reduced retries to speed up boot on blocked networks
    if (retries > 0) {
      logMsg("TZ: Retrying worldtimeapi.org request (" + String(retries) + "/1)...");
      delay(2000);
    }
    http.begin(url);
    http.addHeader("User-Agent", "COLYFLOR-Weather/1.6");
    code = http.GET();
    if (code <= 0) http.end();
    retries++;
  }
  if (code > 0 && code < 400) {
    DynamicJsonDocument doc(1024);
    deserializeJson(doc, http.getStream());
    String newPosix = doc["posix_tz_string"];
    if (newPosix.length() > 0) {
      logMsg("TZ: API returned POSIX: " + newPosix);
      if (newPosix != tzInfo) {
        logMsg("TZ: POSIX string has changed. Updating NVRAM.");
        tzInfo = newPosix;
        prefs.putString("tz", tzInfo);
      }
      http.end();
      return true;
    } else {
      logMsg("TZ: ERROR -> API response did not contain POSIX string.");
    }
  } else {
    logMsg("TZ: ERROR -> worldtimeapi.org request failed with HTTP " + String(code));
  }
  http.end();
  logMsg("TZ: Update failed. Using last known POSIX string from NVRAM.");
  return false;
}

bool hasForbiddenChars(String str) {
  return (str.indexOf('<') != -1 || str.indexOf('>') != -1 || str.indexOf('"') != -1 || str.indexOf('\'') != -1 || str.indexOf('\\') != -1);
}

bool isValidApiKey(String key) {
  if (key.length() != 32) return false;
  for (int i = 0; i < key.length(); i++) {
    if (!isxdigit(key.charAt(i))) return false;
  }
  return true;
}

void pCmd(String in) {
  in.trim(); String lowIn = in; lowIn.toLowerCase();
  if (lowIn == "help") {
    logMsg("--- COMMAND LIST ---");
    logMsg("help      - Show this list");
    logMsg("status    - Probe network and NVS health");
    logMsg("version   - Print firmware version");
    logMsg("heartbeat - Trigger health log");
    logMsg("alerts    - Print active weather alerts");
    logMsg("recover   - Force tiered network recovery");
    logMsg("ota       - Check and apply OTA (append 'force' to bypass version check)");
    logMsg("weather   - Force immediate API fetch and UI refresh");
    logMsg("net       - Show SSID, RSSI, IP, GW, and DNS");
    logMsg("heap      - Display free heap memory");
    logMsg("nvram     - Dump stored preferences");
    logMsg("addwifi   - Syntax: addwifi [ssid] <optional_pass> (Adds/updates WiFi slot)");
    logMsg("rmwifi    - Syntax: rmwifi [slot] (Removes WiFi from a slot 0-4)");
    logMsg("reset     - Wipe NVRAM and execute hardware reboot");
    logMsg("dim       - Override backlight to night threshold");
    logMsg("bright    - Override backlight to day threshold");
    logMsg("auto      - Restore schedule-based backlight logic");
    logMsg("reboot    - Execute software restart");
    logMsg("testalert - Force Alert State (testalert [on|off])");
    logMsg("setapiver - Set Target API (setapiver [2.5|3.0])");
    logMsg("setcity   - Update city name");
    logMsg("setregion - Manually set region string");
    logMsg("setcoords - Set Lat/Lon (setcoords [lat] [lon])");
    logMsg("setiana   - Update IANA TZ (setiana [Region/City])");
    logMsg("setunit   - Set Temp Unit (setunit [C|F])");
    logMsg("settz     - Update TZ string");
    logMsg("setapi    - Update API key");
    logMsg("clear     - Clear terminal history");
  }
  else if (lowIn == "status") {
    bool netOk = runDeepProbe();
    nvs_stats_t n; nvs_get_stats(NULL, &n);
    int pct = (n.total_entries > 0) ? (n.used_entries * 100) / n.total_entries : 0;
    struct tm ti; getLocalTime(&ti); char b[32]; strftime(b, 32, "%H:%M:%S %Y-%m-%d", &ti);
    logMsg("STATUS: Time=" + String(b) + " | API=" + apiVer + " | Unit=" + tempUnit + " | Net=" + String(netOk ? "OK" : "FAIL"));
    logMsg("STATUS: CPU=" + String(temperatureRead(), 1) + "C | NVS=" + String(n.used_entries) + "/" + String(n.total_entries) + " (" + String(pct) + "%)");
  }
  else if (lowIn == "version") {
    logMsg("Firmware: v" + String(CURRENT_VERSION));
  }
  else if (lowIn == "clear") {
    webLog = "";
    Serial.print("\033[2J\033[H");
    logMsg("CMD: Terminal buffer cleared.");
  }
  else if (lowIn.startsWith("addwifi ")) {
    String sub = in.substring(8);
    sub.trim();
    String newSsid = sub;
    String newPass = "";
    int spaceIdx = sub.indexOf(' ');
    
    if (spaceIdx > 0) {
      newSsid = sub.substring(0, spaceIdx);
      newPass = sub.substring(spaceIdx + 1);
      newPass.trim();
    }
    
    if (newSsid.length() > 0) {
      if (hasForbiddenChars(newPass)) {
        logMsg("CMD: ERROR -> Password contains forbidden characters (<, >, \", ', \\).");
      } else {
        if (addOrUpdateWiFi(newSsid, newPass)) logMsg("CMD: WiFi profile updated. SSID: " + newSsid);
        else logMsg("CMD: ERROR -> WiFi slots full.");
      }
    } else {
      logMsg("CMD: ERROR -> Invalid syntax. Use addwifi [ssid] <optional_pass>");
    }
  }
  else if (lowIn.startsWith("rmwifi ")) {
    String sub = in.substring(7);
    sub.trim();
    
    int slot = -1;
    bool confirm = false;
    String slotArg;

    int spaceIdx = sub.indexOf(' ');
    if (spaceIdx != -1) {
      slotArg = sub.substring(0, spaceIdx);
      String confirmArg = sub.substring(spaceIdx + 1);
      confirmArg.trim();
      if (confirmArg.equalsIgnoreCase("confirm")) {
        confirm = true;
      }
    } else {
      slotArg = sub;
    }

    if (slotArg.length() == 1 && isDigit(slotArg.charAt(0))) {
      slot = slotArg.toInt();
    }

    if (slot < 0 || slot > 4) {
      logMsg("CMD: ERROR -> Invalid syntax. Use: rmwifi [slot 0-4]");
      return;
    }

    String ssidToRemove = prefs.getString(("ssid" + String(slot)).c_str(), "");
    if (ssidToRemove == "") {
      logMsg("CMD: Slot " + String(slot) + " is already empty.");
      return;
    }

    if (ssidToRemove == WiFi.SSID() && !confirm) {
      logMsg("CMD: WARNING -> This is the currently active WiFi network.");
      logMsg("CMD: To proceed, run 'rmwifi " + String(slot) + " confirm'");
      return;
    }

    int populatedSlots = 0;
    for (int i = 0; i < 5; i++) {
      if (prefs.getString(("ssid" + String(i)).c_str(), "") != "") {
        populatedSlots++;
      }
    }
    if (populatedSlots == 1) {
      logMsg("CMD: WARNING -> This is the last stored WiFi network.");
      logMsg("CMD: Deleting it will cause the device to fall back to Captive Portal (COLYFLOR_SETUP) mode on next connection loss or reboot.");
    }

    prefs.remove(("ssid" + String(slot)).c_str());
    prefs.remove(("pass" + String(slot)).c_str());
    logMsg("CMD: WiFi profile in slot " + String(slot) + " (" + ssidToRemove + ") has been removed.");
  }
  else if (lowIn.startsWith("setapiver ")) {
    String v = in.substring(10); v.trim();
    if (v == "3.0") { apiVer = "3.0"; } else { apiVer = "2.5"; }
    prefs.putString("api_ver", apiVer);
    logMsg("CMD: API Target updated to " + apiVer);
    updateWeather(true);
  }
  else if (lowIn.startsWith("testalert ")) {
    String v = in.substring(10); v.trim();
    if (v == "on") {
      testAlertMode = true;
      activeAlertCount = 2;
      
      alertSender[0] = "National Weather Test Service Center";
      alertEvent[0] = "Severe Testing Warning";
      alertDesc[0] = "This is a synthetic alert payload injected via console command to verify text wrapping, color definitions, and the pagination state machine functionality on the TFT display. It forces multiple pages to evaluate line break math and array capacity constraints up to five maximum pages. Additional text is appended here specifically to overflow into the fifth page, ensuring the thirty line array bounds and pointer math operate flawlessly without triggering a buffer overflow or hardware panic.";
      
      alertSender[1] = "Local Weather Authority";
      alertEvent[1] = "Minor Wind Advisory";
      alertDesc[1] = "This is the second alert payload in the queue to verify multi-alert transition mechanics. It tests the array iteration and resets the display state correctly.";
      
      processAlertText(alertDesc[0]);
      logMsg("CMD: Test alert MODE ON");
    } else {
      testAlertMode = false;
      displayState = 0;
      activeAlertCount = 0;
      currentAlertIndex = 0;
      currentScreenDelay = 10000;
      logMsg("CMD: Test alert MODE OFF");
    }
    renderCurrentScreen();
  }
  else if (lowIn == "net") {
    logMsg("NET: SSID=" + WiFi.SSID() + " | IP=" + WiFi.localIP().toString() + " | GW=" + WiFi.gatewayIP().toString() + " | DNS=" + WiFi.dnsIP().toString() + " | RSSI=" + String(WiFi.RSSI()) + "dBm"); 
  }
  else if (lowIn.startsWith("setcity ")) {
    String newCity = in.substring(8);
    newCity.trim();
    logMsg("CMD: Request to update city to " + newCity);
    if (geocodeCity(newCity, apiKey)) {
      city = newCity;
      prefs.putString("city", city);
      logMsg("CMD: Geocode success. New location stored. Region: " + region);
      updateWeather(true);
    } else {
      logMsg("CMD: ERROR -> Geocode failed for " + newCity + ". Location unchanged.");
    }
  }
  else if (lowIn.startsWith("setcoords ")) {
    String sub = in.substring(10);
    sub.trim();
    int spaceIdx = sub.indexOf(' ');
    if (spaceIdx > 0) {
      String newLatStr = sub.substring(0, spaceIdx);
      String newLonStr = sub.substring(spaceIdx + 1);
      newLatStr.trim();
      newLonStr.trim();
      float newLat = newLatStr.toFloat();
      float newLon = newLonStr.toFloat();

      if (newLat >= -90.0 && newLat <= 90.0 && newLon >= -180.0 && newLon <= 180.0) {
        lat = newLatStr;
        lon = newLonStr;
        prefs.putString("lat", lat);
        prefs.putString("lon", lon);
        logMsg("CMD: Coordinates updated to Lat: " + lat + ", Lon: " + lon);
        updateWeather(true);
      } else {
        logMsg("CMD: ERROR -> Invalid coordinates. Lat must be -90 to 90, Lon must be -180 to 180.");
      }
    } else {
      logMsg("CMD: ERROR -> Invalid syntax. Use setcoords [latitude] [longitude]");
    }
  }
  else if (lowIn.startsWith("setregion ")) {
    region = in.substring(10);
    region.trim();
    prefs.putString("region", region);
    logMsg("CMD: Region manually updated to: " + region);
  }
  else if (lowIn.startsWith("setiana ")) {
    String newIana = in.substring(8);
    newIana.trim();
    if (newIana.indexOf('/') != -1) { // Basic validation for "Region/City"
      ianaTz = newIana;
      prefs.putString("iana_tz", ianaTz);
      logMsg("CMD: IANA timezone set to " + ianaTz + ". Fetching POSIX string...");
      if (updatePosixFromIana(ianaTz)) {
        logMsg("CMD: POSIX string updated successfully via web API.");
        syncTime();
        updateWeather(true);
      } else {
        logMsg("CMD: ERROR -> Could not resolve new IANA timezone online.");
      }
    } else {
      logMsg("CMD: ERROR -> Invalid IANA format. Use Region/City format (e.g., America/New_York).");
    }
  }
  else if (lowIn.startsWith("setunit ")) {
    String newUnit = in.substring(8);
    newUnit.trim();
    newUnit.toUpperCase();
    if (newUnit == "C" || newUnit == "F") {
      tempUnit = newUnit;
      prefs.putString("unit", tempUnit);
      logMsg("CMD: Temperature unit set to " + tempUnit + ". Refreshing weather...");
      updateWeather(true);
    } else {
      logMsg("CMD: ERROR -> Invalid unit. Use 'C' for Celsius or 'F' for Fahrenheit.");
    }
  }
  else if (lowIn.startsWith("settz ")) { 
    tzInfo = in.substring(6); tzInfo.trim(); prefs.putString("tz", tzInfo); 
    logMsg("CMD: Timezone string updated.");
    syncTime(); 
    updateWeather(true); 
  }
  else if (lowIn.startsWith("setapi ")) { 
    String newApiKey = in.substring(7);
    newApiKey.trim();
    if (isValidApiKey(newApiKey)) {
      apiKey = newApiKey;
      prefs.putString("api", apiKey); 
      logMsg("CMD: API Key updated.");
      updateWeather(true);
    } else {
      logMsg("CMD: ERROR -> Invalid API Key format. Must be 32 hex characters.");
    }
  }
  else if (lowIn == "sync") { logMsg("CMD: Forcing NTP synchronization."); syncTime(); }
  else if (lowIn == "heartbeat") {
    updateBacklight();
    unsigned long n = millis();
    int wMin = (nextWeatherDelay - (n - lastWeatherUpdate)) / 60000;
    int oMin = (21600000UL - (n - lastOTACheck)) / 60000;
    
    uint64_t upSec = esp_timer_get_time() / 1000000;
    int upD = upSec / 86400;
    int upH = (upSec % 86400) / 3600;
    int upM = (upSec % 3600) / 60;
    
    logMsg("HEARTBEAT: SSID=" + WiFi.SSID() + " | RSSI=" + String(WiFi.RSSI()) + "dBm | CPU=" + String(temperatureRead(), 1) + "C | Heap=" + String(ESP.getFreeHeap()) + " | WxIn=" + String(wMin) + "m | OTAIn=" + String(oMin) + "m | Up=" + String(upD) + "d " + String(upH) + "h " + String(upM) + "m");
  }
  else if (lowIn == "alerts") {
    logMsg("CMD: Dumping active alerts...");
    if ((alertActive || testAlertMode) && activeAlertCount > 0) {
      for (int i = 0; i < activeAlertCount; i++) {
        logMsg("--- ALERT " + String(i + 1) + "/" + String(activeAlertCount) + " ---");
        logMsg("  Sender: " + alertSender[i]);
        logMsg("  Event:  " + alertEvent[i]);
        logMsg("  Desc:   " + alertDesc[i]);
      }
      logMsg("--- END OF ALERTS ---");
    } else {
      logMsg("CMD: No active alerts.");
    }
  }
  else if (lowIn == "heap") { logMsg("Heap: " + String(ESP.getFreeHeap())); }
  else if (lowIn == "reboot") { pendingReboot = true; actionTimer = millis(); logMsg("CMD: Reboot scheduled..."); }
  else if (lowIn == "reset") { pendingReset = true; actionTimer = millis(); logMsg("CMD: Reset scheduled..."); }
  else if (lowIn == "weather") { logMsg("CMD: Forcing immediate weather payload execution."); updateWeather(true); }
  else if (lowIn == "recover") { pendingRecover = true; actionTimer = millis(); logMsg("CMD: Recovery scheduled..."); }
  else if (lowIn.startsWith("ota")) { runOTA(lowIn.indexOf("force") != -1); }
  else if (lowIn == "nvram") { 
    logMsg("NVRAM: API Ver=" + apiVer);
    logMsg("NVRAM: API Key=" + apiKey);
    logMsg("NVRAM: Loc=" + city + " (" + region + ")");
    logMsg("NVRAM: Coords=" + lat + "," + lon);
    if (owmIp != "") logMsg("NVRAM: OWM IP=" + owmIp);
    logMsg("NVRAM: TZ=" + tzInfo);
    if (ianaTz != "") logMsg("NVRAM: IANA TZ=" + ianaTz);
    logMsg("NVRAM: Unit=" + tempUnit);
    for (int i=0; i<5; i++) {
      String s = prefs.getString(("ssid"+String(i)).c_str(),"");
      if (s != "") logMsg("NVRAM: Slot " + String(i) + "=" + s);
    }
  }
  else if (lowIn == "dim") { ledcWrite(TFT_BL, (BRIGHTNESS_NIGHT_PCT * 255)/100); logMsg("CMD: Backlight DIM."); }
  else if (lowIn == "bright") { ledcWrite(TFT_BL, (BRIGHTNESS_DAY_PCT * 255)/100); logMsg("CMD: Backlight BRIGHT."); }
  else if (lowIn == "auto") { updateBacklight(); logMsg("CMD: Backlight AUTO."); }
  else if (lowIn != "") {
    logMsg("CMD: ERROR -> Unknown command '" + in + "'. Type 'help' for a list of valid commands.");
  }
}

void setup() {
  Serial.begin(115200); unsigned long sw = millis(); while (!Serial && millis() - sw < 2000) delay(10);
  delay(500); ledcAttach(TFT_BL, 5000, 8); ledcWrite(TFT_BL, (BRIGHTNESS_DAY_PCT * 255) / 100);
  gfx->begin(); gfx->fillScreen(0x0000); gfx->draw16bitRGBBitmap(20, 20, (uint16_t*)colyflor, 120, 120);
  gfx->setTextSize(2); gfx->setTextColor(0xFFFF); gfx->setCursor(160, 40); gfx->print("COLYFLOR");
  gfx->setCursor(160, 65); gfx->print("Weather"); gfx->setCursor(160, 95); gfx->printf("v%s", CURRENT_VERSION);
  Serial.println("\n***************************************");
  Serial.printf("* COLYFLOR Weather System v%s        *\n", CURRENT_VERSION);
  Serial.println("***************************************\n");
  
  loadPreferences();
  logoDataUri = generateLogoDataURI();
  if (apiKey.length() != 32) { showBootStatus("API Fail"); prefs.clear(); isSetupMode = true; }
  if (isSetupMode) startCaptivePortal();
  
  showBootStatus("WiFi Connecting..."); 
  WiFi.mode(WIFI_STA); 
  
  if (!connectTargetedWiFi()) {
    showBootStatus("Net Fail. Portal.");
    startCaptivePortal();
  }
  
  showBootStatus("Updating TZ...");
  updatePosixFromIana(ianaTz);
  showBootStatus("Time Syncing..."); syncTime();
  if (lat == "") { showBootStatus("Locating..."); if (!geocodeCity(city, apiKey)) startCaptivePortal(); }
  showBootStatus("Loading Weather..."); updateWeather(false); 
  server.on("/", HTTP_GET, [](){ server.sendHeader("Location", "/console"); server.send(301); });

  server.on("/console", [](){
    // Use chunked transfer to avoid building a massive string in memory, which can cause heap allocation failures.
    server.setContentLength(CONTENT_LENGTH_UNKNOWN);
    server.send(200, "text/html; charset=utf-8", "");

    String part1 = "<html><head><meta charset=\"UTF-8\"><meta http-equiv='refresh' content='30'><meta name='viewport' content='width=device-width,initial-scale=1'><style>body{background:#000;color:#0f0;font-family:monospace;padding:20px;text-align:center}pre{height:70vh;overflow-y:scroll;border:1px solid #222;padding:10px;text-align:left;white-space:pre-wrap}input{width:100%;background:#111;color:#0f0;border:1px solid #0f0;padding:10px;margin-top:10px}.logo{width:80px;height:80px;margin-bottom:10px}</style>";
    part1 += "<script>window.onload=function(){var p=document.getElementById('log');p.scrollTop=p.scrollHeight;}</script></head><body><img src='";
    server.sendContent(part1);

    server.sendContent(logoDataUri);

    server.sendContent("' class='logo'><h2>CONSOLE v" + String(CURRENT_VERSION) + "</h2><pre id='log'>");
    server.sendContent(webLog);
    server.sendContent("</pre><form action='/cmd' method='POST' accept-charset=\"UTF-8\"><input name='c' placeholder='Type command...' autofocus autocomplete='off'></form></body></html>");

    server.sendContent(""); // Finalize chunked response
  });
  
  server.on("/cmd", HTTP_POST, [](){ if(server.hasArg("c")) pCmd(sanitizeInput(server.arg("c"))); server.sendHeader("Location", "/console"); server.send(303); });
  server.begin();
  logMsg("WEB CONSOLE ACTIVE: http://" + WiFi.localIP().toString() + "/console");
  showBootStatus(("Console: " + WiFi.localIP().toString()).c_str());
  delay(5000); updateWeather(true); 
}

void loop() {
  server.handleClient(); if (Serial.available()) pCmd(sanitizeInput(Serial.readStringUntil('\n')));
  
  unsigned long n = millis();
  
  if (actionTimer > 0 && (n - actionTimer >= 1500)) {
    if (pendingReboot) ESP.restart();
    if (pendingReset) { prefs.clear(); ESP.restart(); }
    if (pendingRecover) {
      pendingRecover = false;
      actionTimer = 0;
      weatherFailStreak++;
      logMsg("CMD: Manual recovery tier " + String(weatherFailStreak) + " triggered.");
      performNetworkRecovery(weatherFailStreak, true);
      if (runDeepProbe()) {
        weatherFailStreak = 0;
        logMsg("CMD: Recovery SUCCESS. Deep probe passed.");
      } else {
        logMsg("CMD: Recovery FAILED. Deep probe failed.");
      }
    }
  }
  
  if (n - lastWeatherUpdate >= nextWeatherDelay) { 
    lastWeatherUpdate = n; 
    updateWeather(true); 
    nextWeatherDelay = random(WEATHER_UPDATE_MIN * 60000, WEATHER_UPDATE_MAX * 60000); 
  }
  
  if (n - lastScreenSwap >= currentScreenDelay) {
    lastScreenSwap = n;
    bool needsRedraw = false;
    if (displayState == 0 && (alertActive || testAlertMode)) {
      displayState = 1;
      currentAlertIndex = 0;
      processAlertText(alertDesc[currentAlertIndex]);
      currentScreenDelay = ALERT_SWAP_DELAY;
      needsRedraw = true;
    } else if (displayState > 0 && displayState < alertTotalPages && displayState < 5) {
      displayState++;
      currentScreenDelay = ALERT_PAGE_DELAY;
      needsRedraw = true;
    } else if (displayState > 0 && currentAlertIndex + 1 < activeAlertCount) {
      currentAlertIndex++;
      displayState = 1;
      processAlertText(alertDesc[currentAlertIndex]);
      currentScreenDelay = ALERT_SWAP_DELAY;
      needsRedraw = true;
    } else {
      if (displayState != 0) { // If we were on an alert screen, we need to redraw to show the weather screen.
        needsRedraw = true;
      }
      displayState = 0;
      currentScreenDelay = NORMAL_SCREEN_DELAY;
    }
    if (needsRedraw) {
      renderCurrentScreen();
    }
  }
  
  if (n - lastStatusPrint >= 300000UL) { lastStatusPrint = n; pCmd("heartbeat"); }
  if (n - lastOTACheck >= 21600000UL) { lastOTACheck = n; runOTA(false); }
  
  // Weekly proactive reboot to prevent heap fragmentation and reset network stack (604800 seconds = 7 days)
  if (esp_timer_get_time() / 1000000ULL >= 604800ULL) {
    logMsg("SYS: Scheduled weekly reboot initiated to maintain optimal stability.");
    delay(2000);
    ESP.restart();
  }
}