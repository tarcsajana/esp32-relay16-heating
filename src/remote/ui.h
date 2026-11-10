#pragma once

#include <stdint.h>

struct RemoteStatus {
  bool wifiUp;
  bool portal;
  char wifiInfo[40];
  char ssid[33];
  bool udpFresh;
  uint32_t lastUdpMs;
  char temp[16];
  char mode[16];
  char req[16];
  char valve[8];
  int t1;
  int t2;
  int heat;
  int p1;
  int p2;
  bool otaBusy;
  char note[48];
};

void uiBegin();
void uiSetStatus(const RemoteStatus &s);
void uiSetNote(const char *msg);
bool uiOnSetup();
const char *uiPollTap();
void uiDraw();
