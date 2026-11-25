#pragma once

#include <stdint.h>

// ============================================================================
//  Gerenciamento da conexão Wi-Fi
//
//  Derivado do WiFiManager original (Monitora_Reserv, ESP8266).
//  Adaptado para ESP32: usa WiFi.onEvent() e as APIs do core 2.0.17.
//  NÃO inicializa hardware nem faz bridge ESP8266↔ESP32.
// ============================================================================
class WiFiManager {
 public:
  WiFiManager();

  void begin(uint32_t nowMs);
  void update(uint32_t nowMs);

  bool connected() const;
  bool credentialsPending() const;
  int32_t rssi() const;

 private:
  void startConnection(uint32_t nowMs);
  void logConnected(uint32_t nowMs);
  void logDisconnected(uint32_t nowMs);

  bool m_started;
  bool m_credentialsPending;
  bool m_wasConnected;
  uint32_t m_lastAttemptMs;
  uint32_t m_attemptCount;
};