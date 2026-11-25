#pragma once

#include <stdint.h>

#include "offline_queue.h"

// Amostra pronta para publicação. O módulo não calcula volume nem nível.
// timestampMs é epoch UTC em milissegundos; uptimeS é monotônico desde o boot.
struct TelemetrySample {
  float volumeL = 0.0f;
  uint64_t timestampMs = 0;
  uint32_t uptimeS = 0;
  bool valid = false;
};

class ThingsBoardManager {
 public:
  void begin(uint32_t nowMs);
  void update(uint32_t nowMs, const TelemetrySample &sample,
              bool wifiConnected);

  bool connected() const { return m_connected; }
  uint32_t publishCount() const { return m_publishCount; }
  bool queueReady() const { return m_queue.ready(); }
  uint32_t queuePending() const { return m_queue.pendingRecords(); }
  uint32_t queueRejected() const { return m_queue.rejectedRecords(); }
  uint32_t lastActivityMs() const {
    return m_lastPublishMs > m_lastConnectedMs ? m_lastPublishMs
                                                : m_lastConnectedMs;
  }

 private:
  bool connectSession(uint32_t nowMs);
  void drainQueue(uint32_t nowMs);
  bool publishValues(uint32_t nowMs, uint64_t timestampMs, float volumeL,
                     const char *origin);
  bool publishUptime(uint32_t nowMs, uint64_t timestampMs, uint32_t uptimeS);
  bool enqueue(uint32_t nowMs, const TelemetrySample &sample);
  void registerPublished(uint32_t nowMs, float volumeL);

  bool m_connected = false;
  bool m_hasPublished = false;
  bool m_publishAfterReconnect = false;
  bool m_tokenPendingWarned = false;
  bool m_clockWarned = false;
  bool m_queueErrorWarned = false;
  bool m_queueFullWarned = false;
  uint32_t m_lastAttemptMs = 0;
  uint32_t m_lastPublishMs = 0;
  uint32_t m_lastConnectedMs = 0;
  uint32_t m_publishCount = 0;
  uint32_t m_retryDelayMs = 0;
  float m_lastPublishedL = 0.0f;
  OfflineQueue m_queue;
};