#include <Arduino.h>
#include <ArduinoOTA.h>
#include <HTTPClient.h>
#include <Update.h>
#include <WebServer.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <WiFiManager.h>
#include <WiFiUdp.h>
#include <cstring>
#include <vector>

#include "heat_udp.h"
#include "remote/ui.h"

static const char *AP_SSID = "HeatRemote-Setup";
static const char *AP_PASS = "heatsetup";
static const char *HOSTNAME = "heat-remote";
static const char *AUTH_USER = "admin";
static const char *AUTH_PASS = "heatota";
static const char *GITHUB_FIRMWARE =
    "https://github.com/tarcsajana/esp32-relay16-heating/releases/latest/download/remote.bin";
static const char PORTAL_OTA_BUTTON[] =
    "<form method='POST' action='/ota-github' "
    "onsubmit=\"return confirm('Install the latest remote.bin from GitHub? The panel will reboot.')\">"
    "<button>OTA update</button></form><br/>";

static WiFiManager wm;
static WiFiUDP udp;
static WebServer server(80);
static bool servicesStarted = false;
static bool otaBusy = false;
static IPAddress relayIp;
static bool haveRelay = false;
static uint32_t lastUdpMs = 0;
static uint32_t lastDiscoverMs = 0;
static RemoteStatus status;

static bool kv(const char *s, const char *key, char *dst, size_t n) {
  const char *p = strstr(s, key);
  if (!p) return false;
  p += strlen(key);
  size_t i = 0;
  while (p[i] && p[i] != ' ' && i + 1 < n) {
    dst[i] = p[i];
    i++;
  }
  dst[i] = 0;
  return true;
}

static int kvInt(const char *s, const char *key) {
  char buf[12];
  if (!kv(s, key, buf, sizeof(buf))) return 0;
  return atoi(buf);
}

static void parseStatus(const char *line) {
  if (strncmp(line, "HEAT ", 5) != 0) return;
  kv(line, "T=", status.temp, sizeof(status.temp));
  kv(line, "mode=", status.mode, sizeof(status.mode));
  kv(line, "req=", status.req, sizeof(status.req));
  kv(line, "valve=", status.valve, sizeof(status.valve));
  status.t1 = kvInt(line, "t1=");
  status.t2 = kvInt(line, "t2=");
  status.heat = kvInt(line, "heat=");
  status.p1 = kvInt(line, "p1=");
  status.p2 = kvInt(line, "p2=");
  lastUdpMs = millis();
  status.udpFresh = true;
}

static void sendToRelay(const char *msg) {
  if (WiFi.status() != WL_CONNECTED) return;
  IPAddress dest = haveRelay ? relayIp : WiFi.localIP();
  if (!haveRelay) dest[3] = 255;
  udp.beginPacket(dest, HEAT_UDP_RELAY_PORT);
  udp.write(reinterpret_cast<const uint8_t *>(msg), strlen(msg));
  udp.endPacket();
}

static void discover() {
  sendToRelay("HEAT?");
  lastDiscoverMs = millis();
}

static void setNote(const char *msg) {
  strncpy(status.note, msg ? msg : "", sizeof(status.note) - 1);
  status.note[sizeof(status.note) - 1] = 0;
  status.otaBusy = otaBusy;
  uiSetStatus(status);
  uiDraw();
}

static void forgetWifi() {
  Serial.println("Forgetting Wi-Fi");
  setNote("Resetting Wi-Fi...");
  delay(200);
  wm.resetSettings();
  delay(200);
  ESP.restart();
}

static bool requireAuth() {
  if (server.authenticate(AUTH_USER, AUTH_PASS)) return true;
  server.requestAuthentication(BASIC_AUTH, HOSTNAME);
  return false;
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
  uint32_t lastUi = 0;
  while (http.connected() && (len < 0 || written < (size_t)len)) {
    size_t avail = stream->available();
    if (!avail) {
      if (millis() - lastData > 20000) break;
      delay(1);
      continue;
    }
    int n = stream->readBytes(buf, avail > sizeof(buf) ? sizeof(buf) : avail);
    if (n <= 0) continue;
    if (Update.write(buf, (size_t)n) != (size_t)n) {
      err = String("Write failed: ") + Update.errorString();
      Update.abort();
      http.end();
      return false;
    }
    written += (size_t)n;
    lastData = millis();
    if (millis() - lastUi > 400) {
      char line[40];
      if (len > 0) {
        snprintf(line, sizeof(line), "Downloading %u%%",
                 (unsigned)((written * 100) / (size_t)len));
      } else {
        snprintf(line, sizeof(line), "Downloading %u kB", (unsigned)(written / 1024));
      }
      setNote(line);
      lastUi = millis();
    }
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

static void runGithubOta() {
  if (otaBusy) return;
  if (WiFi.status() != WL_CONNECTED) {
    setNote("Need internet Wi-Fi");
    return;
  }
  otaBusy = true;
  setNote("Downloading remote.bin...");
  String err;
  Serial.println("GitHub OTA: downloading latest remote.bin");
  if (installGithub(err)) {
    setNote("Rebooting...");
    delay(200);
    ESP.restart();
  }
  otaBusy = false;
  setNote(err.c_str());
  Serial.println(err);
}

static void handleOtaGithub(WebServer &web) {
  if (&web == &server && !requireAuth()) return;
  if (WiFi.status() != WL_CONNECTED) {
    web.send(503, "text/html",
             "<p>Not on a network with internet.</p>"
             "<p><a href='/update'>Upload a remote.bin instead</a></p>");
    return;
  }
  web.send(200, "text/html",
           "<p>Installing latest remote.bin from GitHub. Keep power on. The panel reboots when it finishes.</p>");
  web.client().flush();
  delay(100);
  runGithubOta();
}

static void handleRoot() {
  if (!requireAuth()) return;
  char page[1400];
  snprintf(page, sizeof(page),
           "<!DOCTYPE html><meta name=viewport content=\"width=device-width,initial-scale=1\">"
           "<title>Heat remote</title>"
           "<style>body{font-family:sans-serif;max-width:32rem;margin:2rem auto;padding:0 1rem}"
           "pre{background:#f3f3f3;padding:.8rem;white-space:pre-wrap}"
           "button,input[type=submit]{font-size:1rem;padding:.5rem .8rem}</style>"
           "<h1>Heat remote</h1>"
           "<pre>IP %s\nSSID %s\nrelay %s\nmode %s  req %s  T %s</pre>"
           "<h2>Firmware</h2>"
           "<form method=POST action=/ota-github "
           "onsubmit=\"return confirm('Install the latest remote.bin from GitHub? The panel will reboot.')\">"
           "<button>OTA update</button></form>"
           "<p>Downloads <code>remote.bin</code> from the latest GitHub release.</p>"
           "<form method=POST action=/update enctype=multipart/form-data>"
           "<input type=file name=update accept=.bin> "
           "<input type=submit value='Upload .bin'></form>"
           "<h2>Wi-Fi</h2>"
           "<form method=POST action=/forget "
           "onsubmit=\"return confirm('Forget this network and reboot into HeatRemote-Setup?')\">"
           "<button>Forget Wi-Fi</button></form>",
           WiFi.localIP().toString().c_str(),
           WiFi.SSID().c_str(),
           haveRelay ? relayIp.toString().c_str() : "searching",
           status.mode, status.req, status.temp);
  server.send(200, "text/html", page);
}

static void handleForget() {
  if (!requireAuth()) return;
  server.send(200, "text/html", "<p>Forgetting Wi-Fi. Rebooting into HeatRemote-Setup.</p>");
  server.client().flush();
  delay(200);
  forgetWifi();
}

static void handleUpdateDone() {
  if (!requireAuth()) return;
  if (Update.hasError()) {
    otaBusy = false;
    server.send(500, "text/plain", Update.errorString());
    return;
  }
  server.send(200, "text/plain", "OK, rebooting");
  delay(200);
  ESP.restart();
}

static void handleUpdateUpload() {
  if (!server.authenticate(AUTH_USER, AUTH_PASS)) return;
  HTTPUpload &up = server.upload();
  if (up.status == UPLOAD_FILE_START) {
    otaBusy = true;
    setNote("Uploading firmware...");
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
    setNote("Upload aborted");
  }
}

static void startServices() {
  if (servicesStarted) return;
  servicesStarted = true;
  udp.begin(HEAT_UDP_REMOTE_PORT);
  discover();

  ArduinoOTA.setHostname(HOSTNAME);
  ArduinoOTA.setPassword(AUTH_PASS);
  ArduinoOTA.onStart([]() {
    otaBusy = true;
    setNote("ArduinoOTA...");
  });
  ArduinoOTA.onEnd([]() { Serial.println("ArduinoOTA done"); });
  ArduinoOTA.onError([](ota_error_t err) {
    otaBusy = false;
    setNote("ArduinoOTA error");
    Serial.printf("ArduinoOTA error %u\n", err);
  });
  ArduinoOTA.begin();

  server.on("/", HTTP_GET, handleRoot);
  server.on("/forget", HTTP_POST, handleForget);
  server.on("/ota-github", HTTP_POST, []() { handleOtaGithub(server); });
  server.on("/update", HTTP_POST, handleUpdateDone, handleUpdateUpload);
  server.begin();

  Serial.print("Remote on http://");
  Serial.print(WiFi.localIP());
  Serial.println("/  and http://heat-remote.local/");
}

static void fillWifi() {
  status.wifiUp = (WiFi.status() == WL_CONNECTED);
  status.portal = wm.getConfigPortalActive();
  status.otaBusy = otaBusy;
  if (status.wifiUp) {
    strncpy(status.wifiInfo, WiFi.localIP().toString().c_str(), sizeof(status.wifiInfo) - 1);
    strncpy(status.ssid, WiFi.SSID().c_str(), sizeof(status.ssid) - 1);
  } else if (status.portal) {
    strncpy(status.wifiInfo, "HeatRemote-Setup", sizeof(status.wifiInfo) - 1);
    strncpy(status.ssid, "HeatRemote-Setup", sizeof(status.ssid) - 1);
  } else {
    strncpy(status.wifiInfo, "wifi down", sizeof(status.wifiInfo) - 1);
    status.ssid[0] = 0;
  }
}

static void applyTimeout() {
  bool fresh = status.wifiUp && lastUdpMs != 0 && (millis() - lastUdpMs) < HEAT_UDP_STALE_MS;
  if (status.udpFresh != fresh) status.udpFresh = fresh;
  if (!fresh && lastUdpMs != 0 && (millis() - lastUdpMs) >= HEAT_UDP_STALE_MS) {
    haveRelay = false;
  }
}

static void pollBoot() {
  static uint32_t heldSince = 0;
  if (digitalRead(0) == LOW) {
    if (heldSince == 0) {
      heldSince = millis();
      Serial.println("BOOT held — keep holding 5s to forget Wi-Fi");
    } else if (millis() - heldSince >= 5000) {
      forgetWifi();
    }
  } else {
    heldSince = 0;
  }
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("Heating remote (JC3248W535C)");

  pinMode(0, INPUT_PULLUP);

  memset(&status, 0, sizeof(status));
  strncpy(status.temp, "--", sizeof(status.temp));
  strncpy(status.mode, "--", sizeof(status.mode));
  strncpy(status.req, "AUTO", sizeof(status.req));
  strncpy(status.valve, "--", sizeof(status.valve));

  uiBegin();

  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  wm.setHostname(HOSTNAME);
  wm.setTitle("Heat remote");
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

  Serial.println("Wi-Fi: saved network, or setup AP HeatRemote-Setup / heatsetup");
  if (wm.autoConnect(AP_SSID, AP_PASS)) {
    Serial.println(WiFi.localIP());
    startServices();
  } else {
    Serial.println("Setup portal is up.");
  }
  fillWifi();
  uiSetStatus(status);
  uiDraw();
}

void loop() {
  pollBoot();
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

  if (servicesStarted && !otaBusy) {
    int n = udp.parsePacket();
    if (n > 0) {
      char buf[200];
      int r = udp.read(buf, sizeof(buf) - 1);
      if (r > 0) {
        buf[r] = 0;
        parseStatus(buf);
        relayIp = udp.remoteIP();
        haveRelay = true;
      }
    }

    uint32_t now = millis();
    uint32_t interval = haveRelay ? 5000 : 2000;
    if (now - lastDiscoverMs >= interval) discover();
  }

  const char *tap = uiPollTap();
  if (tap && !otaBusy) {
    if (strcmp(tap, "OTA") == 0) {
      runGithubOta();
    } else if (strcmp(tap, "WIFI_RESET") == 0) {
      forgetWifi();
    } else {
      char cmd[32];
      snprintf(cmd, sizeof(cmd), "MODE %s", tap);
      sendToRelay(cmd);
      Serial.println(cmd);
    }
  }

  fillWifi();
  applyTimeout();
  status.lastUdpMs = lastUdpMs;
  uiSetStatus(status);
  uiDraw();
  delay(20);
}
