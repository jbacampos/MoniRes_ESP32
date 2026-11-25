#include "thingsboard_manager.h"

#include <Arduino.h>
#include <PubSubClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>

#include "config.h"
#include "clock.h"
#include "credentials.h"
#include "publish_policy.h"
#include "tb_ca_cert.h"

namespace {

WiFiClientSecure g_transport;
PubSubClient g_mqtt(g_transport);
char g_payload[192];

double seconds(uint32_t nowMs) {
  return static_cast<double>(nowMs) / 1000.0;
}

const char *mqttStateName(int state) {
  switch (state) {
    case MQTT_CONNECTED: return "conectado";
    case MQTT_DISCONNECTED: return "desconectado";
    case MQTT_CONNECT_FAILED: return "falha de rede";
    case MQTT_CONNECTION_TIMEOUT: return "timeout de conexao";
    case MQTT_CONNECTION_LOST: return "conexao perdida";
    case MQTT_CONNECT_BAD_PROTOCOL: return "protocolo invalido";
    case MQTT_CONNECT_BAD_CLIENT_ID: return "client id rejeitado";
    case MQTT_CONNECT_UNAVAILABLE: return "servidor indisponivel";
    case MQTT_CONNECT_BAD_CREDENTIALS: return "credenciais invalidas";
    case MQTT_CONNECT_UNAUTHORIZED: return "nao autorizado";
  }
  return "estado desconhecido";
}

bool validClock(uint64_t timestampMs) {
  return device_clock::synced() &&
         (timestampMs / 1000ULL) >=
             static_cast<uint64_t>(NTP_MIN_VALID_EPOCH_SECONDS);
}

}  // namespace

void ThingsBoardManager::begin(uint32_t nowMs) {
  g_transport.setCACert(DATALOG_MQTT_CA);
  const uint32_t socketTimeoutSeconds =
      (MQTT_SOCKET_TIMEOUT_MS + 999UL) / 1000UL;
  g_transport.setHandshakeTimeout(10UL);
  g_transport.setTimeout(socketTimeoutSeconds);

  g_mqtt.setServer(TB_HOST, TB_PORT);
  g_mqtt.setSocketTimeout(MQTT_CONNACK_TIMEOUT_S);
  g_mqtt.setBufferSize(MQTT_BUFFER_SIZE);

  const bool queueOk = m_queue.begin();
  Serial.printf("TB: fila offline=%s", queueOk ? "pronta" : "indisponivel");
  if (queueOk) {
    Serial.printf(" pendentes=%lu",
                  static_cast<unsigned long>(m_queue.pendingRecords()));
  }
  Serial.println();

  m_connected = false;
  m_hasPublished = false;
  m_publishAfterReconnect = false;
  m_tokenPendingWarned = false;
  m_clockWarned = false;
  m_lastAttemptMs = nowMs - MQTT_RECONNECT_MS;
  m_lastPublishMs = 0;
  m_lastConnectedMs = 0;
  m_publishCount = 0;
  m_retryDelayMs = MQTT_RECONNECT_MS;
  m_lastPublishedL = 0.0f;

  Serial.printf("TB: TLS configurado | broker=%s:%u | buffer MQTT=%u B\n",
                TB_HOST, static_cast<unsigned>(TB_PORT),
                static_cast<unsigned>(MQTT_BUFFER_SIZE));
}

bool ThingsBoardManager::connectSession(uint32_t nowMs) {
  if (!validClock(device_clock::nowMs())) {
    return false;
  }

  if (tbTokenPending()) {
    if (!m_tokenPendingWarned) {
      m_tokenPendingWarned = true;
      Serial.printf("TB: token pendente em %s (preencha e recompile). "
                    "Conexao MQTT/TLS nao sera tentada.\n",
                    activeSecretsFileName());
    }
    return false;
  }

  char clientId[32];
  const uint32_t chipId = static_cast<uint32_t>(ESP.getEfuseMac());
  snprintf(clientId, sizeof(clientId), "%s-%06lx", DEVICE_ID,
           static_cast<unsigned long>(chipId & 0xFFFFFFUL));

  Serial.printf("[%7.2f s] TB: conectando a %s:%u (clientId=%s)\n",
                seconds(nowMs), TB_HOST, static_cast<unsigned>(TB_PORT),
                clientId);

  const uint32_t startedMs = millis();
  const bool ok = g_mqtt.connect(clientId, ACTIVE_TB_TOKEN, nullptr);
  const uint32_t elapsedMs = millis() - startedMs;

  if (!ok) {
    char tlsError[96] = {0};
    const int tlsCode = g_transport.lastError(tlsError, sizeof(tlsError));
    Serial.printf("[%7.2f s] TB: falha ao conectar (motivo=%s, estado=%d, "
                  "tls=%d:%s) em %lu ms\n",
                  seconds(nowMs), mqttStateName(g_mqtt.state()),
                  g_mqtt.state(), tlsCode, tlsError,
                  static_cast<unsigned long>(elapsedMs));
    g_transport.stop();
    return false;
  }

  Serial.printf("[%7.2f s] TB: conectado em %lu ms\n", seconds(nowMs),
                static_cast<unsigned long>(elapsedMs));
  m_lastConnectedMs = nowMs;
  return true;
}

bool ThingsBoardManager::publishValues(uint32_t nowMs, uint64_t timestampMs,
                                        float volumeL, const char *origin) {
  if (!validClock(timestampMs)) {
    return false;
  }

  const unsigned long secondsPart =
      static_cast<unsigned long>(timestampMs / 1000ULL);
  const unsigned long millisPart =
      static_cast<unsigned long>(timestampMs % 1000ULL);

  const int written = snprintf(
      g_payload, sizeof(g_payload),
      "{\"%s\":%lu%03lu,\"values\":{\"%s\":%.1f}}", TB_KEY_TS,
      secondsPart, millisPart, TB_KEY_VOLUME, static_cast<double>(volumeL));

  if (written <= 0 || static_cast<size_t>(written) >= sizeof(g_payload)) {
    Serial.printf("TB: payload nao cabe no buffer (%d bytes)\n", written);
    return false;
  }

  if (!g_mqtt.publish(TB_TELEMETRY_TOPIC, g_payload, false)) {
    Serial.printf("[%7.2f s] TB: falha ao publicar (motivo=%s, estado=%d)\n",
                  seconds(nowMs), mqttStateName(g_mqtt.state()),
                  g_mqtt.state());
    m_connected = false;
    m_publishAfterReconnect = true;
    return false;
  }

  Serial.printf("[%7.2f s] TB: publicado (%s): %s\n", seconds(nowMs), origin,
                g_payload);
  return true;
}

bool ThingsBoardManager::publishUptime(uint32_t nowMs, uint64_t timestampMs,
                                       uint32_t uptimeS) {
  if (!validClock(timestampMs)) {
    return false;
  }

  char payload[48];
  const int written = snprintf(payload, sizeof(payload), "{\"%s\":%lu}",
                               TB_KEY_UPTIME,
                               static_cast<unsigned long>(uptimeS));
  if (written <= 0 || static_cast<size_t>(written) >= sizeof(payload)) {
    return false;
  }

  if (!g_mqtt.publish(TB_ATTRIBUTES_TOPIC, payload, false)) {
    Serial.printf("[%7.2f s] TB: falha ao publicar atributo %s\n",
                  seconds(nowMs), TB_KEY_UPTIME);
    return false;
  }

  Serial.printf("[%7.2f s] TB: atributo %s\n", seconds(nowMs), payload);
  return true;
}

bool ThingsBoardManager::enqueue(uint32_t nowMs,
                                 const TelemetrySample &sample) {
  if (!m_queue.append(sample.timestampMs, sample.volumeL)) {
    if (!m_queueFullWarned) {
      m_queueFullWarned = true;
      Serial.printf("[%7.2f s] TB: fila offline recusou a amostra; "
                    "registros existentes preservados\n",
                    seconds(nowMs));
    }
    return false;
  }

  registerPublished(nowMs, sample.volumeL);
  m_publishAfterReconnect = false;
  return true;
}

void ThingsBoardManager::drainQueue(uint32_t nowMs) {
  if (!m_queue.ready()) {
    return;
  }

  const uint32_t startedMs = millis();
  uint8_t budget = TB_QUEUE_REPLAY_MAX_RECORDS;
  while (budget > 0 && (millis() - startedMs) < TB_QUEUE_REPLAY_BUDGET_MS) {
    queue_record::Record record;
    const OfflineQueue::PeekResult result = m_queue.peek(record);
    if (result == OfflineQueue::PeekResult::EMPTY) {
      return;
    }
    if (result == OfflineQueue::PeekResult::ERROR) {
      if (!m_queueErrorWarned) {
        m_queueErrorWarned = true;
        Serial.printf("[%7.2f s] TB: falha na fila offline; "
                      "registros preservados\n",
                      seconds(nowMs));
      }
      return;
    }

    if (!publishValues(nowMs, record.timestampMs, record.volumeL,
                       "fila offline")) {
      return;
    }
    if (!m_queue.confirm()) {
      return;
    }
    registerPublished(nowMs, record.volumeL);
    --budget;
  }
}

void ThingsBoardManager::registerPublished(uint32_t nowMs, float volumeL) {
  m_hasPublished = true;
  m_lastPublishedL = volumeL;
  m_lastPublishMs = nowMs;
  ++m_publishCount;
}

void ThingsBoardManager::update(uint32_t nowMs, const TelemetrySample &sample,
                                bool wifiConnected) {
  if (!wifiConnected) {
    if (m_connected) {
      Serial.printf("[%7.2f s] TB: sessao encerrada (Wi-Fi indisponivel)\n",
                    seconds(nowMs));
      m_connected = false;
      m_publishAfterReconnect = true;
    }
  } else if (!g_mqtt.connected()) {
    if (m_connected) {
      Serial.printf("[%7.2f s] TB: desconectado (motivo=%s, estado=%d)\n",
                    seconds(nowMs), mqttStateName(g_mqtt.state()),
                    g_mqtt.state());
      m_connected = false;
      m_publishAfterReconnect = true;
    }

    if (!validClock(sample.timestampMs)) {
      if (!m_clockWarned) {
        m_clockWarned = true;
        Serial.printf("[%7.2f s] TB: aguardando NTP; telemetria nao sera "
                      "publicada\n", seconds(nowMs));
      }
      return;
    }

    if ((nowMs - m_lastAttemptMs) >= m_retryDelayMs) {
      m_lastAttemptMs = nowMs;
      if (connectSession(nowMs)) {
        m_connected = true;
        m_retryDelayMs = MQTT_RECONNECT_MS;
        m_publishAfterReconnect = true;
        publishUptime(nowMs, sample.timestampMs, sample.uptimeS);
      } else {
        m_retryDelayMs *= 2UL;
        if (m_retryDelayMs > MQTT_MAX_RECONNECT_MS) {
          m_retryDelayMs = MQTT_MAX_RECONNECT_MS;
        }
      }
    }
  } else {
    m_connected = true;
  }

  const bool online = m_connected && g_mqtt.connected();
  if (online) {
    g_mqtt.loop();
    drainQueue(nowMs);
  }

  if (!sample.valid || !validClock(sample.timestampMs)) {
    return;
  }

  publish_policy::Input input;
  input.sampleValid = sample.valid;
  input.hasPublished = m_hasPublished;
  input.publishAfterReconnect = m_publishAfterReconnect;
  input.volumeL = sample.volumeL;
  input.lastPublishedL = m_lastPublishedL;
  input.nowMs = nowMs;
  input.lastPublishMs = m_lastPublishMs;
  input.deltaLiters = TB_PUBLISH_DELTA_LITERS;
  input.heartbeatMs = TB_HEARTBEAT_INTERVAL_MS;
  input.minIntervalMs = TB_MIN_PUBLISH_INTERVAL_MS;

  const publish_policy::Reason reason = publish_policy::decide(input);
  if (reason == publish_policy::Reason::NONE) {
    return;
  }

  const bool pendingBacklog = m_queue.hasPending();
  if (online && !pendingBacklog &&
      publishValues(nowMs, sample.timestampMs, sample.volumeL,
                    publish_policy::reasonName(reason))) {
    registerPublished(nowMs, sample.volumeL);
    m_publishAfterReconnect = false;
    publishUptime(nowMs, sample.timestampMs, sample.uptimeS);
    return;
  }

  if (enqueue(nowMs, sample)) {
    Serial.printf("[%7.2f s] TB: amostra na fila (%s), pendentes=%lu\n",
                  seconds(nowMs), publish_policy::reasonName(reason),
                  static_cast<unsigned long>(m_queue.pendingRecords()));
  }
}