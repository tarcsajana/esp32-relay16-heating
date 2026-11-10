#include <Arduino.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <WiFiUdp.h>
#include <cstring>

#include "heat_udp.h"
#include "remote/ui.h"

static const char *AP_SSID = "HeatRemote-Setup";
static const char *AP_PASS = "heatsetup";
static const char *HOSTNAME = "heat-remote";

static WiFiManager wm;
static WiFiUDP udp;
static bool servicesStarted = false;
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

static void startServices() {
  if (servicesStarted) return;
  servicesStarted = true;
  udp.begin(HEAT_UDP_REMOTE_PORT);
  discover();
  Serial.print("Remote on ");
  Serial.println(WiFi.localIP());
}

static void fillWifi() {
  status.wifiUp = (WiFi.status() == WL_CONNECTED);
  status.portal = wm.getConfigPortalActive();
  if (status.wifiUp) {
    strncpy(status.wifiInfo, WiFi.localIP().toString().c_str(), sizeof(status.wifiInfo) - 1);
  } else if (status.portal) {
    strncpy(status.wifiInfo, "HeatRemote-Setup", sizeof(status.wifiInfo) - 1);
  } else {
    strncpy(status.wifiInfo, "wifi down", sizeof(status.wifiInfo) - 1);
  }
}

static void applyTimeout() {
  bool fresh = status.wifiUp && lastUdpMs != 0 && (millis() - lastUdpMs) < HEAT_UDP_STALE_MS;
  if (status.udpFresh != fresh) {
    status.udpFresh = fresh;
  }
  if (!fresh && lastUdpMs != 0 && (millis() - lastUdpMs) >= HEAT_UDP_STALE_MS) {
    haveRelay = false;
  }
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("Heating remote (JC3248W535C)");

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
  if (wm.getConfigPortalActive()) wm.process();
  if (WiFi.status() == WL_CONNECTED && wm.getConfigPortalActive()) {
    wm.stopConfigPortal();
    wm.process();
  }
  if (WiFi.status() == WL_CONNECTED) startServices();

  if (servicesStarted) {
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
  if (tap) {
    char cmd[32];
    snprintf(cmd, sizeof(cmd), "MODE %s", tap);
    sendToRelay(cmd);
    Serial.println(cmd);
  }

  fillWifi();
  applyTimeout();
  status.lastUdpMs = lastUdpMs;
  uiSetStatus(status);
  uiDraw();
  delay(20);
}
