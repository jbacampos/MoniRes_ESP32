#pragma once

#include <stdint.h>

// ============================================================================
//  Relogio do dispositivo (NTP)
//  Adaptado para ESP32 core 2.0.17.
//  Usa configTime() e gettimeofday(). Sem setenv("TZ").
// ============================================================================
namespace device_clock {

void begin(uint32_t nowMs);
void update(uint32_t nowMs, bool wifiConnected);
bool synced();
uint64_t nowMs();

}  // namespace device_clock