#pragma once

enum HeatRequest {
  HEAT_REQ_AUTO = 0,
  HEAT_REQ_ELECTRIC = 1,
  HEAT_REQ_FIREPLACE = 2,
};

void heatUdpSetup();
void heatUdpOnWifi();
void heatUdpLoop();
void heatUdpSetPacket(const char *line);
void heatUdpSendStatus();

struct HeatLimits {
  int hi;  // fireplace pumps on, rising
  int lo;  // fireplace pumps off, falling
  int el;  // AUTO electric below this
};

HeatRequest heatRequest();
void heatSetRequest(HeatRequest r);
const char *heatRequestName();
HeatLimits heatLimits();
void heatSetLimit(char kind, int value);
