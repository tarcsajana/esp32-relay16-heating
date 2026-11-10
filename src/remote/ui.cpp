#include "ui.h"

#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include <Wire.h>
#include <string.h>

static const int PIN_BL = 1;
static const int PIN_SDA = 4;
static const int PIN_SCL = 8;
static const int PIN_INT = 3;
static const int W = 320;
static const int H = 480;

static uint16_t rgb(uint8_t r, uint8_t g, uint8_t b) {
  return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

static const uint16_t COL_BG     = rgb(11, 16, 28);
static const uint16_t COL_CARD   = rgb(22, 32, 52);
static const uint16_t COL_TEXT   = rgb(236, 241, 247);
static const uint16_t COL_MUTED  = rgb(138, 154, 178);
static const uint16_t COL_OK     = rgb(61, 220, 151);
static const uint16_t COL_BAD    = rgb(255, 90, 90);
static const uint16_t COL_AUTO   = rgb(46, 196, 182);
static const uint16_t COL_ELEC   = rgb(245, 166, 35);
static const uint16_t COL_FIRE   = rgb(232, 93, 4);
static const uint16_t COL_DUMP   = rgb(220, 38, 38);

static Arduino_ESP32QSPI *bus;
static Arduino_AXS15231B *panel;
static Arduino_Canvas *gfx;
static uint8_t touchAddr = 0;
static RemoteStatus st;
static bool haveStatus = false;
static bool dirty = true;
static bool touching = false;
static uint32_t lastTapMs = 0;

struct Btn {
  int x, y, w, h;
  const char *label;
  const char *token;
  uint16_t accent;
};

static Btn btns[3] = {
    {16, 268, 288, 62, "AUTO", "AUTO", COL_AUTO},
    {16, 340, 288, 62, "ELECTRIC", "ELECTRIC", COL_ELEC},
    {16, 412, 288, 62, "FIREPLACE", "FIREPLACE", COL_FIRE},
};

static void centerText(const char *s, int y, int size, uint16_t color) {
  gfx->setTextSize(size);
  gfx->setTextColor(color);
  int16_t x1, y1;
  uint16_t tw, th;
  gfx->getTextBounds(s, 0, 0, &x1, &y1, &tw, &th);
  gfx->setCursor((W - (int)tw) / 2, y);
  gfx->print(s);
}

static void labelAt(const char *s, int x, int y, int size, uint16_t color) {
  gfx->setTextSize(size);
  gfx->setTextColor(color);
  gfx->setCursor(x, y);
  gfx->print(s);
}

static bool probeAddr(uint8_t addr) {
  Wire.beginTransmission(addr);
  return Wire.endTransmission() == 0;
}

static bool readTouch(int16_t &x, int16_t &y) {
  if (!touchAddr) return false;
  uint8_t data[8] = {0};
  const uint8_t cmd[11] = {0xb5, 0xab, 0xa5, 0x5a, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00};
  Wire.beginTransmission(touchAddr);
  Wire.write(cmd, sizeof(cmd));
  if (Wire.endTransmission() != 0) return false;
  if (Wire.requestFrom((int)touchAddr, 8) != 8) return false;
  for (int i = 0; i < 8; i++) data[i] = Wire.read();
  if (data[1] == 0 || data[1] > 5) return false;
  uint16_t rawX = ((data[2] & 0x0F) << 8) | data[3];
  uint16_t rawY = ((data[4] & 0x0F) << 8) | data[5];
  if (rawX > 500 || rawY > 500) return false;
  if (rawX >= 320 && rawY < 320) {
    x = (int16_t)rawY;
    y = (int16_t)rawX;
  } else {
    x = (int16_t)rawX;
    y = (int16_t)rawY;
  }
  if (x < 0) x = 0;
  if (y < 0) y = 0;
  if (x > 319) x = 319;
  if (y > 479) y = 479;
  return true;
}

static void drawButton(const Btn &b, bool selected) {
  uint16_t fill = selected ? b.accent : COL_CARD;
  uint16_t text = selected ? COL_BG : COL_TEXT;
  uint16_t border = b.accent;
  gfx->fillRoundRect(b.x, b.y, b.w, b.h, 14, fill);
  gfx->drawRoundRect(b.x, b.y, b.w, b.h, 14, border);
  gfx->drawRoundRect(b.x + 1, b.y + 1, b.w - 2, b.h - 2, 13, border);
  gfx->setTextSize(3);
  gfx->setTextColor(text);
  int16_t x1, y1;
  uint16_t tw, th;
  gfx->getTextBounds(b.label, 0, 0, &x1, &y1, &tw, &th);
  gfx->setCursor(b.x + (b.w - (int)tw) / 2, b.y + (b.h - (int)th) / 2);
  gfx->print(b.label);
}

void uiBegin() {
  pinMode(PIN_BL, OUTPUT);
  digitalWrite(PIN_BL, HIGH);
  pinMode(PIN_INT, INPUT_PULLUP);

  bus = new Arduino_ESP32QSPI(45, 47, 21, 48, 40, 39);
  panel = new Arduino_AXS15231B(bus, GFX_NOT_DEFINED, 0, false, W, H);
  gfx = new Arduino_Canvas(W, H, panel, 0, 0, 0);
  if (!gfx->begin()) {
    Serial.println("Display init failed");
  }
  gfx->fillScreen(COL_BG);
  centerText("Heating", 200, 3, COL_TEXT);
  centerText("starting...", 250, 2, COL_MUTED);
  gfx->flush();

  Wire.begin(PIN_SDA, PIN_SCL);
  Wire.setClock(400000);
  delay(50);
  if (probeAddr(0x3B)) touchAddr = 0x3B;
  else if (probeAddr(0x38)) touchAddr = 0x38;
  Serial.printf("Touch I2C addr 0x%02X\n", touchAddr);

  memset(&st, 0, sizeof(st));
  strncpy(st.temp, "--", sizeof(st.temp));
  strncpy(st.mode, "--", sizeof(st.mode));
  strncpy(st.req, "AUTO", sizeof(st.req));
  strncpy(st.valve, "--", sizeof(st.valve));
  strncpy(st.wifiInfo, "wifi", sizeof(st.wifiInfo));
  haveStatus = true;
  dirty = true;
}

void uiSetStatus(const RemoteStatus &s) {
  if (memcmp(&st, &s, sizeof(RemoteStatus)) != 0) {
    st = s;
    haveStatus = true;
    dirty = true;
  }
}

const char *uiPollTap() {
  int16_t x, y;
  bool down = readTouch(x, y);
  const char *hit = nullptr;
  if (down && !touching) {
    uint32_t now = millis();
    if (now - lastTapMs > 250) {
      for (int i = 0; i < 3; i++) {
        if (x >= btns[i].x && x < btns[i].x + btns[i].w &&
            y >= btns[i].y && y < btns[i].y + btns[i].h) {
          hit = btns[i].token;
          lastTapMs = now;
          strncpy(st.req, hit, sizeof(st.req) - 1);
          dirty = true;
          break;
        }
      }
    }
  }
  touching = down;
  return hit;
}

void uiDraw() {
  if (!dirty || !haveStatus) return;
  dirty = false;

  gfx->fillScreen(COL_BG);

  labelAt("Heating", 14, 12, 2, COL_TEXT);
  const char *conn = st.udpFresh ? "Connected" : "No connection";
  uint16_t connCol = st.udpFresh ? COL_OK : COL_BAD;
  gfx->setTextSize(2);
  gfx->setTextColor(connCol);
  int16_t x1, y1;
  uint16_t tw, th;
  gfx->getTextBounds(conn, 0, 0, &x1, &y1, &tw, &th);
  gfx->setCursor(W - 12 - (int)tw, 12);
  gfx->print(conn);

  gfx->fillRoundRect(16, 48, 288, 150, 16, COL_CARD);
  const char *tempLabel = st.temp[0] ? st.temp : "--";
  centerText(tempLabel, 78, 6, COL_TEXT);
  if (strcmp(tempLabel, "--") != 0) {
    gfx->setTextSize(3);
    gfx->setTextColor(COL_MUTED);
    int16_t bx, by;
    uint16_t bw, bh;
    gfx->getTextBounds(tempLabel, 0, 0, &bx, &by, &bw, &bh);
    gfx->setCursor((W + (int)bw) / 2 + 4, 92);
    gfx->print("C");
  }
  centerText("fireplace pipe", 148, 2, COL_MUTED);

  uint16_t modeCol = COL_MUTED;
  if (strcmp(st.mode, "FIRE_DUMP") == 0) modeCol = COL_DUMP;
  else if (strcmp(st.mode, "ELECTRIC") == 0) modeCol = COL_ELEC;
  else if (strcmp(st.mode, "FIREPLACE") == 0) modeCol = COL_FIRE;

  char live[40];
  if (!st.wifiUp) snprintf(live, sizeof(live), "Wi-Fi down");
  else if (st.portal) snprintf(live, sizeof(live), "Join HeatRemote-Setup");
  else if (!st.udpFresh) snprintf(live, sizeof(live), "waiting for relay");
  else snprintf(live, sizeof(live), "LIVE  %s", st.mode);
  centerText(live, 208, 2, modeCol);

  char bits[48];
  if (st.udpFresh) {
    snprintf(bits, sizeof(bits), "valve %s  heat %s  P1 %s  P2 %s",
             st.valve,
             st.heat ? "on" : "off",
             st.p1 ? "on" : "off",
             st.p2 ? "on" : "off");
  } else if (st.portal) {
    snprintf(bits, sizeof(bits), "http://192.168.4.1");
  } else {
    snprintf(bits, sizeof(bits), "%s", st.wifiInfo);
  }
  centerText(bits, 236, 1, COL_MUTED);

  for (int i = 0; i < 3; i++) {
    bool sel = (strcmp(st.req, btns[i].token) == 0);
    drawButton(btns[i], sel);
  }

  gfx->flush();
}
