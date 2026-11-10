#pragma once

#include <stdint.h>

// UDP remote protocol between the LCTech relay board and the JC3248W535C panel.
//
// Relay listens on HEAT_UDP_RELAY_PORT. Remote listens on HEAT_UDP_REMOTE_PORT.
//
// Status (relay -> remote), one ASCII line:
//   HEAT T=45.2 mode=FIREPLACE req=AUTO t1=1 t2=0 valve=FP heat=0 p1=1 p2=0
//   T=-- when the DS18B20 is missing.
//   mode = live output (FIRE_DUMP | ELECTRIC | FIREPLACE)
//   req  = last remote request (AUTO | ELECTRIC | FIREPLACE)
//
// Commands (remote -> relay):
//   MODE AUTO
//   MODE ELECTRIC
//   MODE FIREPLACE
//   HEAT?            ask for a status reply now
//
// AUTO is the built-in logic (fireplace default, electric backup).
// ELECTRIC / FIREPLACE force the circuit; fireDump still overrides both.

static const uint16_t HEAT_UDP_RELAY_PORT  = 4210;
static const uint16_t HEAT_UDP_REMOTE_PORT = 4211;
static const uint32_t HEAT_UDP_STALE_MS    = 30000;
