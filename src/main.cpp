// LCTech ESP32 16-ch relay — fireplace / electric heating control
// Relays are driven by two 74HC595s, not raw GPIOs.

#include <Arduino.h>
#include <DallasTemperature.h>
#include <OneWire.h>
#include <Preferences.h>
#include <cstring>
#include "net.h"
#include "udp_heat.h"

// 74HC595 (already wired on the PCB)
static const int PIN_SER   = 14;  // data
static const int PIN_SRCLK = 13;  // clock
static const int PIN_RCLK  = 12;  // latch
static const int PIN_OE    = 5;   // output enable, active LOW
static const int PIN_SRCLR = 4;   // clear, active LOW — keep HIGH

// Sensors on the side headers
static const int PIN_ONEWIRE = 23;
static const int PIN_TSTAT1  = 16;  // dry contact to GND = call for heat
static const int PIN_TSTAT2  = 17;

// Physical relay index 0 = channel 1 on the board, 15 = channel 16
static const int RELAY_PUMP1   = 0;
static const int RELAY_PUMP2   = 1;
static const int RELAY_HEATER  = 2;
static const int RELAY_VALVE   = 3;  // ON = fireplace circuit, OFF = electric

static const float T_PUMP_ON  = 75.0;  // rising: start both fireplace pumps
static const float T_PUMP_OFF = 55.0;  // falling: stop both fireplace pumps
static const float T_FIRE_OFF = 35.0;  // AUTO: electric below this

static const bool BOOT_CLICK_TEST = true;

OneWire oneWire(PIN_ONEWIRE);
DallasTemperature sensors(&oneWire);

// Pipe sensor runs the latches. The other two on the same bus are report-only.
static const int SENSOR_MAX = 3;
static DeviceAddress pipeAddr;
static bool pipeKnown = false;
static DeviceAddress extraAddr[2];
static int extraCount = 0;
static int lastCount = -1;

static bool addrOk(const uint8_t *a) {
  if (a[0] != 0x28) return false;
  for (int i = 0; i < 8; i++) {
    if (a[i] != 0) return true;
  }
  return false;
}

static void printAddr(const uint8_t *a) {
  for (int i = 0; i < 8; i++) Serial.printf("%02X", a[i]);
}

static void loadPipe() {
  Preferences prefs;
  prefs.begin("ds", true);
  size_t n = prefs.getBytes("pipe", pipeAddr, sizeof(pipeAddr));
  prefs.end();
  pipeKnown = (n == sizeof(pipeAddr) && addrOk(pipeAddr));
}

static void savePipe(const uint8_t *a) {
  memcpy(pipeAddr, a, sizeof(pipeAddr));
  pipeKnown = true;
  Preferences prefs;
  prefs.begin("ds", false);
  prefs.putBytes("pipe", pipeAddr, sizeof(pipeAddr));
  prefs.end();
  Serial.print(F("Pipe sensor "));
  printAddr(pipeAddr);
  Serial.println();
}

static void assignSensors() {
  extraCount = 0;
  int n = sensors.getDeviceCount();
  if (n > SENSOR_MAX) n = SENSOR_MAX;

  DeviceAddress found[SENSOR_MAX];
  int count = 0;
  for (int i = 0; i < n; i++) {
    if (sensors.getAddress(found[count], i)) count++;
  }
  for (int i = 0; i < count; i++) {
    for (int j = i + 1; j < count; j++) {
      if (memcmp(found[j], found[i], 8) < 0) {
        DeviceAddress tmp;
        memcpy(tmp, found[i], 8);
        memcpy(found[i], found[j], 8);
        memcpy(found[j], tmp, 8);
      }
    }
  }

  bool pipeOnBus = false;
  if (pipeKnown) {
    for (int i = 0; i < count; i++) {
      if (memcmp(found[i], pipeAddr, 8) == 0) pipeOnBus = true;
    }
  }
  // Learn the pipe only while it is the only chip, and only once.
  if (!pipeKnown && count == 1) {
    savePipe(found[0]);
    pipeOnBus = true;
  }

  for (int i = 0; i < count && extraCount < 2; i++) {
    if (pipeOnBus && memcmp(found[i], pipeAddr, 8) == 0) continue;
    memcpy(extraAddr[extraCount], found[i], 8);
    extraCount++;
  }

  Serial.printf("DS18B20 count=%d pipe=%d extras=%d\n", count, pipeOnBus ? 1 : 0, extraCount);
  if (!pipeOnBus && count > 1) {
    Serial.println(F("Pipe not learned. Power up once with only the fireplace sensor connected."));
    for (int i = 0; i < count; i++) {
      Serial.print(F("  "));
      printAddr(found[i]);
      Serial.println();
    }
  }
}

static void refreshBus(bool force) {
  if (force || sensors.getDeviceCount() == 0) {
    sensors.begin();
    pinMode(PIN_ONEWIRE, INPUT_PULLUP);
    if (sensors.getDeviceCount()) sensors.setResolution(12);
  }
  int n = sensors.getDeviceCount();
  if (n != lastCount) {
    assignSensors();
    lastCount = n;
  }
}

static bool tempOk(float t) { return t > -50.0f && t < 125.0f; }

static void formatTemp(char *dst, size_t n, bool present, float t) {
  if (present && tempOk(t)) snprintf(dst, n, "%.1f", t);
  else snprintf(dst, n, "--");
}

uint16_t relayMask = 0;

void writeRelays(uint16_t mask) {
  relayMask = mask;
  digitalWrite(PIN_RCLK, LOW);
  shiftOut(PIN_SER, PIN_SRCLK, MSBFIRST, highByte(mask));
  shiftOut(PIN_SER, PIN_SRCLK, MSBFIRST, lowByte(mask));
  digitalWrite(PIN_RCLK, HIGH);
}

void setRelay(int channel, bool on) {
  if (on) {
    relayMask |= (1u << channel);
  } else {
    relayMask &= ~(1u << channel);
  }
  writeRelays(relayMask);
}

void applyOutputs(bool valveFireplace, bool heater, bool pump1, bool pump2) {
  uint16_t mask = 0;
  if (pump1) mask |= (1u << RELAY_PUMP1);
  if (pump2) mask |= (1u << RELAY_PUMP2);
  if (heater) mask |= (1u << RELAY_HEATER);
  if (valveFireplace) mask |= (1u << RELAY_VALVE);
  writeRelays(mask);
}

void bootClickTest() {
  Serial.println(F("Boot test: click relays 1-4"));
  for (int ch = 0; ch < 4; ch++) {
    writeRelays(1u << ch);
    delay(250);
    writeRelays(0);
    delay(150);
  }
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println();
  Serial.println(F("LCTech 16-ch heating control"));

  pinMode(PIN_SER, OUTPUT);
  pinMode(PIN_SRCLK, OUTPUT);
  pinMode(PIN_RCLK, OUTPUT);
  pinMode(PIN_OE, OUTPUT);
  pinMode(PIN_SRCLR, OUTPUT);
  pinMode(PIN_TSTAT1, INPUT_PULLUP);
  pinMode(PIN_TSTAT2, INPUT_PULLUP);

  digitalWrite(PIN_SRCLR, HIGH);
  digitalWrite(PIN_OE, LOW);
  writeRelays(0);

  if (BOOT_CLICK_TEST) bootClickTest();

  loadPipe();
  sensors.setWaitForConversion(false);
  refreshBus(true);

  netSetup();
}

static char statusLine[240] = "starting";

const char *heatingStatus() { return statusLine; }

static void controlStep() {
  float t = pipeKnown ? sensors.getTempC(pipeAddr) : DEVICE_DISCONNECTED_C;
  bool sensorOk = tempOk(t);
  float ta = extraCount > 0 ? sensors.getTempC(extraAddr[0]) : DEVICE_DISCONNECTED_C;
  float tb = extraCount > 1 ? sensors.getTempC(extraAddr[1]) : DEVICE_DISCONNECTED_C;
  char aTxt[8];
  char bTxt[8];
  formatTemp(aTxt, sizeof(aTxt), extraCount > 0, ta);
  formatTemp(bTxt, sizeof(bTxt), extraCount > 1, tb);

  bool tstat1 = (digitalRead(PIN_TSTAT1) == LOW);
  bool tstat2 = (digitalRead(PIN_TSTAT2) == LOW);
  bool call = tstat1 || tstat2;
  HeatRequest req = heatRequest();

  bool valveFireplace;
  bool heater;
  bool pump1;
  bool pump2;

  // Fireplace pumps: ON at ≥ 75 °C (rising), OFF at ≤ 55 °C (falling).
  // Between 55 and 75 keep the last state so a warming fire is not cooled at 55.
  static bool dumpPumps = false;
  if (!sensorOk) dumpPumps = false;
  else if (t >= T_PUMP_ON) dumpPumps = true;
  else if (t <= T_PUMP_OFF) dumpPumps = false;

  // Electric valve: heater and each pump follow their thermostat. No temp control.
  bool electricValve = (req == HEAT_REQ_ELECTRIC) ||
      (req == HEAT_REQ_AUTO && (!sensorOk || t < T_FIRE_OFF));

  if (electricValve) {
    valveFireplace = false;
    heater = call;
    pump1 = tstat1;
    pump2 = tstat2;
  } else {
    valveFireplace = true;
    heater = false;
    pump1 = dumpPumps;
    pump2 = dumpPumps;
  }

  applyOutputs(valveFireplace, heater, pump1, pump2);

  const char *modeName = valveFireplace ? "FIREPLACE" : "ELECTRIC";

  char wifi[40];
  char temp[16];
  char udpLine[240];
  netStatus(wifi, sizeof(wifi));
  if (sensorOk) snprintf(temp, sizeof(temp), "%.1f", t);
  else snprintf(temp, sizeof(temp), "--");
  if (sensorOk) {
    snprintf(statusLine, sizeof(statusLine),
             "T=%s  a=%s  b=%s  mode=%s  req=%s  t1=%d t2=%d  valve=%s  heat=%d  p1=%d p2=%d  wifi=%s",
             temp, aTxt, bTxt, modeName, heatRequestName(),
             tstat1 ? 1 : 0, tstat2 ? 1 : 0,
             valveFireplace ? "FP" : "EL", heater ? 1 : 0, pump1 ? 1 : 0, pump2 ? 1 : 0, wifi);
  } else {
    snprintf(statusLine, sizeof(statusLine),
             "T=NO_SENSOR ds=%u  a=%s  b=%s  mode=%s  req=%s  t1=%d t2=%d  valve=%s  heat=%d  p1=%d p2=%d  wifi=%s",
             sensors.getDeviceCount(), aTxt, bTxt, modeName, heatRequestName(),
             tstat1 ? 1 : 0, tstat2 ? 1 : 0,
             valveFireplace ? "FP" : "EL", heater ? 1 : 0, pump1 ? 1 : 0, pump2 ? 1 : 0, wifi);
  }
  snprintf(udpLine, sizeof(udpLine),
           "HEAT T=%s a=%s b=%s mode=%s req=%s t1=%d t2=%d valve=%s heat=%d p1=%d p2=%d",
           temp, aTxt, bTxt,
           modeName,
           heatRequestName(),
           tstat1 ? 1 : 0,
           tstat2 ? 1 : 0,
           valveFireplace ? "FP" : "EL",
           heater ? 1 : 0,
           pump1 ? 1 : 0,
           pump2 ? 1 : 0);
  heatUdpSetPacket(udpLine);
  heatUdpSendStatus();
  Serial.println(statusLine);
}

void loop() {
  netLoop();
  if (netOtaBusy()) return;

  static uint32_t lastReq = 0;
  static bool waiting = false;
  uint32_t now = millis();

  if (!waiting && now - lastReq >= 1000) {
    static uint32_t lastScan = 0;
    refreshBus(now - lastScan >= 10000);
    if (now - lastScan >= 10000) lastScan = now;
    sensors.requestTemperatures();
    lastReq = now;
    waiting = true;
  }
  if (waiting && now - lastReq >= 800) {
    waiting = false;
    controlStep();
  }
}

