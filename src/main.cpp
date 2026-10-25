// LCTech ESP32 16-ch relay — fireplace / electric heating control
// Relays are driven by two 74HC595s, not raw GPIOs.

#include <Arduino.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include "net.h"

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

static const float T_FIRE_ON  = 75.0;
static const float T_PUMP_OFF = 55.0;
static const float T_FIRE_OFF = 35.0;

static const bool BOOT_CLICK_TEST = true;

OneWire oneWire(PIN_ONEWIRE);
DallasTemperature sensors(&oneWire);

enum Mode { ELECTRIC, FIREPLACE };
Mode mode = ELECTRIC;
bool pumpsLatched = false;
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

void applyOutputs(bool valveFireplace, bool heater, bool pumps) {
  uint16_t mask = 0;
  if (pumps) {
    mask |= (1u << RELAY_PUMP1);
    mask |= (1u << RELAY_PUMP2);
  }
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

  sensors.begin();
  sensors.setResolution(12);
  sensors.setWaitForConversion(false);

  netSetup();
}

static char statusLine[180] = "starting";

const char *heatingStatus() { return statusLine; }

static void controlStep() {
  float t = sensors.getTempCByIndex(0);
  bool sensorOk = (t > -50.0f && t < 125.0f);

  bool call = (digitalRead(PIN_TSTAT1) == LOW) ||
              (digitalRead(PIN_TSTAT2) == LOW);

  if (!sensorOk) {
    mode = ELECTRIC;
    pumpsLatched = false;
  } else if (mode == ELECTRIC && t >= T_FIRE_ON) {
    mode = FIREPLACE;
    pumpsLatched = true;
  } else if (mode == FIREPLACE && t <= T_FIRE_OFF) {
    mode = ELECTRIC;
    pumpsLatched = false;
  }

  bool valveFireplace = false;
  bool heater = false;
  bool pumps = false;

  if (mode == FIREPLACE && sensorOk) {
    valveFireplace = true;
    heater = false;
    if (t >= T_FIRE_ON) pumpsLatched = true;
    if (t <= T_PUMP_OFF) pumpsLatched = false;
    pumps = pumpsLatched;
  } else {
    valveFireplace = false;
    heater = call;
    pumps = call;
  }

  applyOutputs(valveFireplace, heater, pumps);

  char wifi[40];
  char temp[16];
  netStatus(wifi, sizeof(wifi));
  if (sensorOk) snprintf(temp, sizeof(temp), "%.1f", t);
  else snprintf(temp, sizeof(temp), "NO_SENSOR");
  snprintf(statusLine, sizeof(statusLine),
           "T=%s  mode=%s  call=%d  valve=%s  heat=%d  pumps=%d  wifi=%s",
           temp,
           mode == FIREPLACE ? "FIREPLACE" : "ELECTRIC",
           call ? 1 : 0,
           valveFireplace ? "FP" : "EL",
           heater ? 1 : 0,
           pumps ? 1 : 0,
           wifi);
  Serial.println(statusLine);
}

void loop() {
  netLoop();
  if (netOtaBusy()) return;

  static uint32_t lastReq = 0;
  static bool waiting = false;
  uint32_t now = millis();

  if (!waiting && now - lastReq >= 1000) {
    sensors.requestTemperatures();
    lastReq = now;
    waiting = true;
  }
  if (waiting && now - lastReq >= 800) {
    waiting = false;
    controlStep();
  }
}

