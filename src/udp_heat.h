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

HeatRequest heatRequest();
void heatSetRequest(HeatRequest r);
const char *heatRequestName();
