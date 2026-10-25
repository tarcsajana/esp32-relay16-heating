#include "net.h"

#include <Arduino.h>
#include <ArduinoOTA.h>
#include <HTTPClient.h>
#include <Update.h>
#include <WebServer.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <WiFiManager.h>

static const char *AP_SSID = "Heating-Setup";
static const char *AP_PASS = "heatsetup";
static const char *HOSTNAME = "heating";
static const char *AUTH_USER = "admin";
static const char *AUTH_PASS = "heatota";
static const char *GITHUB_FIRMWARE =
    "https://github.com/tarcsajana/esp32-relay16-heating/releases/latest/download/firmware.bin";
static const char PORTAL_OTA_BUTTON[] =
    "<form method='POST' action='/ota-github' "
    "onsubmit=\"return confirm('Install the latest firmware from GitHub? The board will reboot.')\">"
    "<button>OTA update</button></form><br/>";

static WiFiManager wm;
static WebServer server(80);
static bool servicesStarted = false;
static bool otaBusy = false;

static bool requireAuth() {
  if (server.authenticate(AUTH_USER, AUTH_PASS)) return true;
  server.requestAuthentication(BASIC_AUTH, HOSTNAME);
  return false;
}

static void handleRoot() {
  if (!requireAuth()) return;
  char page[1600];
  snprintf(page, sizeof(page),
           "<!DOCTYPE html><meta name=viewport content=\"width=device-width,initial-scale=1\">"
           "<title>Heating</title>"
           "<style>body{font-family:sans-serif;max-width:32rem;margin:2rem auto;padding:0 1rem}"
           "pre{background:#f3f3f3;padding:.8rem;white-space:pre-wrap}"
           "button,input[type=submit]{font-size:1rem;padding:.5rem .8rem}</style>"
           "<h1>Heating</h1><pre>%s</pre>"
           "<h2>Firmware</h2>"
           "<form method=POST action=/ota-github "
           "onsubmit=\"return confirm('Install the latest firmware from GitHub? The board will reboot.')\">"
           "<button>OTA update</button></form>"
           "<p>Downloads <code>firmware.bin</code> from the latest GitHub release.</p>"
           "<form method=POST action=/update enctype=multipart/form-data>"
           "<input type=file name=firmware accept=.bin required> "
           "<input type=submit value=Upload></form>"
           "<p>Or upload a <code>firmware.bin</code> from a PlatformIO build.</p>"
           "<form method=POST action=/forget onsubmit=\"return confirm('Forget Wi-Fi and reboot into setup?')\">"
           "<button>Forget Wi-Fi</button></form>",
           heatingStatus());
  server.send(200, "text/html", page);
}

static void handleForget() {
  if (!requireAuth()) return;
  server.send(200, "text/plain", "Wi-Fi cleared. Restarting into setup.");
  delay(300);
  wm.resetSettings();
  ESP.restart();
}

static void handleUpdateDone() {
  if (!requireAuth()) return;
  bool ok = !Update.hasError();
  server.sendHeader("Connection", "close");
  server.send(ok ? 200 : 500, "text/plain", ok ? "OK, restarting" : "Update failed");
  if (ok) {
    delay(400);
    ESP.restart();
  }
  otaBusy = false;
}

static bool installGithub(String &err) {
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.setTimeout(20000);
  if (!http.begin(client, GITHUB_FIRMWARE)) {
    err = "Could not start download";
    return false;
  }
  int code = http.GET();
  if (code != HTTP_CODE_OK) {
    err = "Download failed, HTTP " + String(code);
    http.end();
    return false;
  }
  int len = http.getSize();
  WiFiClient *stream = http.getStreamPtr();
  if (!Update.begin(len > 0 ? (size_t)len : UPDATE_SIZE_UNKNOWN)) {
    err = String("Update begin failed: ") + Update.errorString();
    http.end();
    return false;
  }
  uint8_t buf[1024];
  size_t written = 0;
  uint32_t lastData = millis();
  while (http.connected() && (len < 0 || written < (size_t)len)) {
    size_t avail = stream->available();
    if (!avail) {
      if (millis() - lastData > 20000) break;
      delay(1);
      continue;
    }
    int n = stream->readBytes(buf, avail > sizeof(buf) ? sizeof(buf) : avail);
    if (n <= 0) continue;
    if (Update.write(buf, n) != (size_t)n) {
      err = String("Write failed: ") + Update.errorString();
      Update.abort();
      http.end();
      return false;
    }
    written += n;
    lastData = millis();
  }
  http.end();
  if (len > 0 && written != (size_t)len) {
    err = "Download ended early";
    Update.abort();
    return false;
  }
  if (!Update.end(true)) {
    err = String("Update end failed: ") + Update.errorString();
    return false;
  }
  Serial.printf("GitHub OTA wrote %u bytes\n", (unsigned)written);
  return true;
}

static void handleOtaGithub(WebServer &web) {
  if (&web == &server && !requireAuth()) return;
  if (WiFi.status() != WL_CONNECTED) {
    web.send(503, "text/html",
             "<p>The board is not on a Wi-Fi network with internet, so it cannot download the release.</p>"
             "<p><a href='/update'>Upload a firmware.bin file instead</a></p>");
    return;
  }
  web.send(200, "text/html",
           "<p>Installing the latest firmware from GitHub. Keep power on. The board reboots when it finishes.</p>");
  web.client().flush();
  delay(100);
  otaBusy = true;
  String err;
  Serial.println(F("GitHub OTA: downloading latest firmware.bin"));
  if (installGithub(err)) {
    delay(200);
    ESP.restart();
  }
  otaBusy = false;
  Serial.println(err);
}

static void handleUpdateUpload() {
  if (!server.authenticate(AUTH_USER, AUTH_PASS)) return;
  HTTPUpload &up = server.upload();
  if (up.status == UPLOAD_FILE_START) {
    otaBusy = true;
    Serial.printf("Web OTA: %s\n", up.filename.c_str());
    if (!Update.begin(UPDATE_SIZE_UNKNOWN)) Update.printError(Serial);
  } else if (up.status == UPLOAD_FILE_WRITE) {
    if (Update.write(up.buf, up.currentSize) != up.currentSize) Update.printError(Serial);
  } else if (up.status == UPLOAD_FILE_END) {
    if (Update.end(true)) Serial.printf("Web OTA wrote %u bytes\n", up.totalSize);
    else Update.printError(Serial);
  } else if (up.status == UPLOAD_FILE_ABORTED) {
    Update.abort();
    otaBusy = false;
  }
}

static void startServices() {
  if (servicesStarted) return;
  servicesStarted = true;

  ArduinoOTA.setHostname(HOSTNAME);
  ArduinoOTA.setPassword(AUTH_PASS);
  ArduinoOTA.onStart([]() {
    otaBusy = true;
    Serial.println(F("ArduinoOTA start"));
  });
  ArduinoOTA.onEnd([]() { Serial.println(F("ArduinoOTA done")); });
  ArduinoOTA.onError([](ota_error_t err) {
    otaBusy = false;
    Serial.printf("ArduinoOTA error %u\n", err);
  });
  ArduinoOTA.begin();

  server.on("/", HTTP_GET, handleRoot);
  server.on("/forget", HTTP_POST, handleForget);
  server.on("/ota-github", HTTP_POST, []() { handleOtaGithub(server); });
  server.on("/update", HTTP_POST, handleUpdateDone, handleUpdateUpload);
  server.begin();

  Serial.print(F("On the network at http://"));
  Serial.print(WiFi.localIP());
  Serial.println(F("/  and http://heating.local/"));
  Serial.println(F("Web login admin / heatota. PlatformIO OTA password heatota."));
}

static void pollIo0() {
  static uint32_t heldSince = 0;
  if (digitalRead(0) == LOW) {
    if (heldSince == 0) {
      heldSince = millis();
      Serial.println(F("IO0 held — keep holding 5s to forget Wi-Fi"));
    } else if (millis() - heldSince >= 5000) {
      Serial.println(F("Forgetting Wi-Fi"));
      wm.resetSettings();
      ESP.restart();
    }
  } else {
    heldSince = 0;
  }
}

void netSetup() {
  pinMode(0, INPUT_PULLUP);
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  wm.setHostname(HOSTNAME);
  wm.setTitle("Heating");
  wm.setDebugOutput(true);
  wm.setConfigPortalBlocking(false);
  wm.setConnectTimeout(15);
  wm.setWiFiAutoReconnect(true);
  wm.setCaptivePortalEnable(true);
  wm.setCustomMenuHTML(PORTAL_OTA_BUTTON);
  wm.setWebServerCallback([]() {
    wm.server->on("/ota-github", HTTP_POST, []() { handleOtaGithub(*wm.server); });
  });
  wm.setPreOtaUpdateCallback([]() { otaBusy = true; });
  std::vector<const char *> menu = {"wifi", "custom", "update", "info", "sep", "restart", "exit"};
  wm.setMenu(menu);

  Serial.println(F("Wi-Fi: saved network, or setup AP Heating-Setup / heatsetup at http://192.168.4.1"));
  if (wm.autoConnect(AP_SSID, AP_PASS)) {
    Serial.print(F("Wi-Fi connected "));
    Serial.println(WiFi.localIP());
    startServices();
  } else {
    Serial.println(F("Setup portal is up. Heating keeps running."));
  }
}

void netLoop() {
  pollIo0();
  if (wm.getConfigPortalActive()) wm.process();

  if (WiFi.status() == WL_CONNECTED && wm.getConfigPortalActive()) {
    wm.stopConfigPortal();
    wm.process();
  }
  if (WiFi.status() == WL_CONNECTED) startServices();

  if (servicesStarted) {
    server.handleClient();
    ArduinoOTA.handle();
  }
}

bool netOtaBusy() { return otaBusy; }

void netStatus(char *buf, size_t len) {
  if (otaBusy) {
    snprintf(buf, len, "ota");
  } else if (WiFi.status() == WL_CONNECTED) {
    snprintf(buf, len, "%s", WiFi.localIP().toString().c_str());
  } else if (wm.getConfigPortalActive()) {
    snprintf(buf, len, "setup");
  } else {
    snprintf(buf, len, "down");
  }
}
