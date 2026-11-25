#include "wifi_manager.h"
#include <Arduino.h>
#include <WiFi.h>
#include "config.h"
#include "credentials.h"

// Global state for disconnection reason
static volatile uint8_t g_reasonCode = 0;
static volatile int8_t g_reasonRssi = 0;
static volatile bool g_reasonReceived = false;
static volatile bool g_reasonPending = false;

// Helper: nome do status WL (compatível com WiFiType.h)
static const char *wifiStatusName(wl_status_t status) {
  switch (status) {
    case WL_NO_SHIELD: return "WL_NO_SHIELD";
    case WL_IDLE_STATUS: return "WL_IDLE_STATUS";
    case WL_NO_SSID_AVAIL: return "WL_NO_SSID_AVAIL";
    case WL_SCAN_COMPLETED: return "WL_SCAN_COMPLETED";
    case WL_CONNECTED: return "WL_CONNECTED";
    case WL_CONNECT_FAILED: return "WL_CONNECT_FAILED";
    case WL_CONNECTION_LOST: return "WL_CONNECTION_LOST";
    case WL_DISCONNECTED: return "WL_DISCONNECTED";
  }
  return "WL_STATUS_DESCONHECIDO";
}

// Event handler for Wi-Fi STA disconnection (ESP32)
static void wifi_sta_disconnected_handler(arduino_event_t *event) {
  const wifi_event_sta_disconnected_t *info =
      &event->event_info.wifi_sta_disconnected;
  g_reasonCode = static_cast<uint8_t>(info->reason);
  g_reasonRssi = info->rssi;
  g_reasonReceived = true;
  g_reasonPending = true;
}

// WiFiManager constructor — inicializa todos os members
WiFiManager::WiFiManager() {
  m_started = false;
  m_credentialsPending = false;
  m_wasConnected = false;
  m_lastAttemptMs = 0;
  m_attemptCount = 0;
}

// begin() — chamada única no setup()
void WiFiManager::begin(uint32_t nowMs) {
  m_credentialsPending = wifiCredentialsPending();
  if (m_credentialsPending) {
    Serial.printf("Wi-Fi: credenciais pendentes em include/secrets.h (preencha e recompile).\n");
    m_started = false;
    return;
  }
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  // Registra handler de desconexão via evento do core (ESP32)
  WiFi.onEvent(wifi_sta_disconnected_handler, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
  m_started = true;
  startConnection(nowMs);
}

// startConnection() — dispara WiFi.begin()
void WiFiManager::startConnection(uint32_t nowMs) {
  m_lastAttemptMs = nowMs;
  ++m_attemptCount;
  Serial.printf("[%7.2f s] Wi-Fi: conectando a '%s' (tentativa %u)\n",
    static_cast<double>(nowMs) / 1000.0, ACTIVE_WIFI_SSID, m_attemptCount);
  WiFi.begin(ACTIVE_WIFI_SSID, ACTIVE_WIFI_PASSWORD);
}

// update() — chamada contínua no loop()
void WiFiManager::update(uint32_t nowMs) {
  if (!m_started) return;
  const bool isConnected = connected();
  if (isConnected != m_wasConnected) {
    m_wasConnected = isConnected;
    if (isConnected) logConnected(nowMs);
    else logDisconnected(nowMs);
  }
  if (isConnected) return;
  if ((nowMs - m_lastAttemptMs) < WIFI_RETRY_MS) return;
  WiFi.disconnect(false);
  startConnection(nowMs);
}

// connected() — wraparound WiFi.status()
bool WiFiManager::connected() const { return WiFi.status() == WL_CONNECTED; }

bool WiFiManager::credentialsPending() const { return m_credentialsPending; }

int32_t WiFiManager::rssi() const { return WiFi.RSSI(); }

// logConnected() — Serial.printf idêntico ao original
void WiFiManager::logConnected(uint32_t nowMs) {
  Serial.printf("[%7.2f s] Wi-Fi: conectado (ip=%s, rssi=%d dBm)\n",
    static_cast<double>(nowMs) / 1000.0, WiFi.localIP().toString().c_str(),
    static_cast<int>(rssi()));
}

// logDisconnected() — Serial.printf idêntico ao original
void WiFiManager::logDisconnected(uint32_t nowMs) {
  const wl_status_t status = WiFi.status();
  unsigned reasonCode = g_reasonCode;
  int eventRssi = g_reasonRssi;
  const bool eventReceived = g_reasonReceived;
  const char *reasonName = "NAO_MAPEADO";
  const char *eventStatus = eventReceived ? "recebido" : "ausente";
  if (g_reasonPending) {
    switch (reasonCode) {
      case 1: reasonName = "UNSPECIFIED"; break;
      case 2: reasonName = "AUTH_EXPIRE"; break;
      case 3: reasonName = "AUTH_LEAVE"; break;
      case 4: reasonName = "ASSOC_EXPIRE"; break;
      case 5: reasonName = "ASSOC_TOOMANY"; break;
      case 8: reasonName = "ASSOC_LEAVE"; break;
      case 15: reasonName = "4WAY_HANDSHAKE_TIMEOUT"; break;
      case 200: reasonName = "BEACON_TIMEOUT"; break;
      case 201: reasonName = "NO_AP_FOUND"; break;
      case 202: reasonName = "AUTH_FAIL"; break;
      case 203: reasonName = "ASSOC_FAIL"; break;
      case 204: reasonName = "HANDSHAKE_TIMEOUT"; break;
      default: reasonName = "RAZAO_ESP32"; break;
    }
  }
  if (eventReceived) {
    Serial.printf("[%7.2f s] Wi-Fi: desconectado (evento=%s, motivo=%s, codigo=%u, rssi_evento=%d dBm, status=%s) - nova tentativa em %lu ms\n",
      static_cast<double>(nowMs) / 1000.0, eventStatus, reasonName, reasonCode,
      eventRssi, wifiStatusName(status), static_cast<unsigned long>(WIFI_RETRY_MS));
  } else {
    Serial.printf("[%7.2f s] Wi-Fi: desconectado (evento=%s, motivo=%s, codigo=%s, status=%s) - nova tentativa em %lu ms\n",
      static_cast<double>(nowMs) / 1000.0, eventStatus, reasonName,
      "ausente", wifiStatusName(status),
      static_cast<unsigned long>(WIFI_RETRY_MS));
  }
  g_reasonPending = false;
  g_reasonReceived = false;
  g_reasonCode = 0;
  g_reasonRssi = 0;
}
