#include "udp_heat.h"
#include "heat_udp.h"

#include <Arduino.h>
#include <Preferences.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <strings.h>

static WiFiUDP udp;
static Preferences prefs;
static bool udpReady = false;
static HeatRequest req = HEAT_REQ_AUTO;
static IPAddress lastRemote;
static bool haveRemote = false;
static char udpLine[180] =
    "HEAT T=-- mode=FIREPLACE req=AUTO t1=0 t2=0 valve=FP heat=0 p1=0 p2=0";

static bool sameToken(const char *s, const char *want) {
  size_t n = strlen(want);
  if (strncasecmp(s, want, n) != 0) return false;
  char c = s[n];
  return c == 0 || c == ' ' || c == '\r' || c == '\n' || c == '\t';
}

void heatUdpSetup() {
  prefs.begin("heat", false);
  uint8_t v = prefs.getUChar("req", HEAT_REQ_AUTO);
  if (v > HEAT_REQ_FIREPLACE) v = HEAT_REQ_AUTO;
  req = (HeatRequest)v;
  Serial.printf("Remote request: %s\n", heatRequestName());
}

void heatUdpOnWifi() {
  if (udpReady) return;
  if (udp.begin(HEAT_UDP_RELAY_PORT)) {
    udpReady = true;
    Serial.printf("UDP remote on port %u\n", HEAT_UDP_RELAY_PORT);
  }
}

HeatRequest heatRequest() { return req; }

void heatSetRequest(HeatRequest r) {
  if (r > HEAT_REQ_FIREPLACE) r = HEAT_REQ_AUTO;
  if (r == req) return;
  req = r;
  prefs.putUChar("req", (uint8_t)r);
  Serial.printf("Remote request -> %s\n", heatRequestName());
}

const char *heatRequestName() {
  switch (req) {
    case HEAT_REQ_ELECTRIC:
      return "ELECTRIC";
    case HEAT_REQ_FIREPLACE:
      return "FIREPLACE";
    default:
      return "AUTO";
  }
}

void heatUdpSetPacket(const char *line) {
  strncpy(udpLine, line, sizeof(udpLine) - 1);
  udpLine[sizeof(udpLine) - 1] = 0;
}

static void sendTo(IPAddress ip, uint16_t port) {
  if (!udpReady) return;
  udp.beginPacket(ip, port);
  udp.write(reinterpret_cast<const uint8_t *>(udpLine), strlen(udpLine));
  udp.endPacket();
}

void heatUdpSendStatus() {
  if (!udpReady || WiFi.status() != WL_CONNECTED) return;
  IPAddress bcast = WiFi.localIP();
  bcast[3] = 255;
  sendTo(bcast, HEAT_UDP_REMOTE_PORT);
  if (haveRemote && lastRemote != bcast) sendTo(lastRemote, HEAT_UDP_REMOTE_PORT);
}

static void handleCmd(char *buf, IPAddress ip) {
  haveRemote = true;
  lastRemote = ip;
  if (strncmp(buf, "MODE ", 5) == 0) {
    const char *m = buf + 5;
    while (*m == ' ' || *m == '\t') m++;
    if (sameToken(m, "AUTO")) heatSetRequest(HEAT_REQ_AUTO);
    else if (sameToken(m, "ELECTRIC")) heatSetRequest(HEAT_REQ_ELECTRIC);
    else if (sameToken(m, "FIREPLACE")) heatSetRequest(HEAT_REQ_FIREPLACE);
  }
  sendTo(ip, HEAT_UDP_REMOTE_PORT);
}

void heatUdpLoop() {
  if (!udpReady) return;
  int n = udp.parsePacket();
  if (n <= 0) return;
  char buf[96];
  int r = udp.read(buf, sizeof(buf) - 1);
  if (r <= 0) return;
  buf[r] = 0;
  for (int i = 0; i < r; i++) {
    if (buf[i] == '\r' || buf[i] == '\n') buf[i] = 0;
  }
  handleCmd(buf, udp.remoteIP());
}
