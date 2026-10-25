#pragma once

#include <stddef.h>

void netSetup();
void netLoop();
bool netOtaBusy();
void netStatus(char *buf, size_t len);
const char *heatingStatus();
