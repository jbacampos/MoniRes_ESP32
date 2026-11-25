#include "clock.h"

#include <Arduino.h>
#include <esp_sntp.h>
#include <sys/time.h>

#include "config.h"

// ============================================================================
//  Relogio do dispositivo (NTP) — adaptado para ESP32 core 2.0.17
// ============================================================================
namespace {

bool g_started = false;
bool g_synced = false;
bool g_wifiWasConnected = false;
volatile bool g_syncPending = false;
volatile int64_t g_syncSeconds = 0;
volatile int32_t g_syncUsec = 0;
uint32_t g_lastCheckMs = 0;

void sntp_sync_callback(struct timeval *tv) {
  if (tv == nullptr) return;
  g_syncSeconds = static_cast<int64_t>(tv->tv_sec);
  g_syncUsec = static_cast<int32_t>(tv->tv_usec);
  g_syncPending = true;
}

}  // namespace

void device_clock::begin(uint32_t nowMs) {
  sntp_set_time_sync_notification_cb(sntp_sync_callback);
  g_started = false;
  g_synced = false;
  g_wifiWasConnected = false;
  g_syncPending = false;
  g_lastCheckMs = nowMs;
}

void device_clock::update(uint32_t nowMs, bool wifiConnected) {
  const bool initialRequest = wifiConnected && !g_started;
  const bool wifiReconnected = wifiConnected && !g_wifiWasConnected;
  g_wifiWasConnected = wifiConnected;

  if (initialRequest || wifiReconnected) {
    configTime(UTC_OFFSET_SECONDS, 0, NTP_SERVER_1, NTP_SERVER_2, NTP_SERVER_3);
    g_started = true;
    g_lastCheckMs = nowMs;
    Serial.println(!initialRequest && wifiReconnected
      ? "Relogio: nova sincronizacao NTP solicitada apos reconexao."
      : "Relogio: sincronizacao NTP inicial solicitada.");
  }

  if (g_syncPending) {
    const int64_t seconds = g_syncSeconds;
    const int32_t usec = g_syncUsec;
    g_syncPending = false;
    if (seconds >= static_cast<int64_t>(NTP_MIN_VALID_EPOCH_SECONDS)) {
      g_synced = true;
      Serial.printf("Relogio: sincronizado (epoch=%lu.%03lu)\n",
        static_cast<unsigned long>(seconds),
        static_cast<unsigned long>(usec / 1000));
    }
  }

  if (!wifiConnected || (nowMs - g_lastCheckMs) < 1000UL) return;
  g_lastCheckMs = nowMs;
}

bool device_clock::synced() { return g_synced; }

uint64_t device_clock::nowMs() {
  struct timeval tv = {};
  if (gettimeofday(&tv, nullptr) != 0) return 0;

  int64_t seconds = static_cast<int64_t>(tv.tv_sec);
  int64_t usec = static_cast<int64_t>(tv.tv_usec);

  if (usec < 0) {
    usec += 1000000;
    seconds -= 1;
  }
  seconds += usec / 1000000;
  usec %= 1000000;

  return static_cast<uint64_t>(seconds) * 1000ULL +
         static_cast<uint32_t>(usec / 1000);
}