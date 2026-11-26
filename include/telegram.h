#pragma once

#include <stddef.h>
#include <stdint.h>

#include "alert_monitor.h"
#include "history.h"
#include "telegram_text.h"

// ============================================================================
//  Telegram - Bot API consultada diretamente pelo ESP32
//
//  O modulo usa o cliente HTTP/1.1 proprio do projeto sobre
//  WiFiClientSecure/mbedTLS. A implementacao do transporte fica em
//  src/telegram.cpp; este header expoe somente o contrato de orquestracao e
//  diagnostico.
//
//  Comandos: /help, /status e /warnings, com as abreviacoes /h, /s e /w.
//  Alem das respostas, o modulo entrega o aviso de boot e os alertas de nivel
//  produzidos por AlertMonitor, uma mensagem por consulta.
//
//  Ciclo de vida:
//    begin(nowMs)  - inicializa somente o estado; nenhuma conexao e aberta;
//    update(nowMs, context) - executa no maximo uma consulta quando o intervalo
//                              de polling vencer.
//
//  A validade TLS deve ser mantida pela implementacao ESP32 com CA + hostname/SNI.
//  Nao ha dependencias de BearSSL ou de APIs exclusivas do ESP8266 neste contrato.
// ============================================================================
struct TelegramContext {
  // Volume filtrado atual, o mesmo valor usado pelo display e ThingsBoard.
  float volumeL = 0.0f;

  // Uptime acumulado em segundos, independente da hora civil e do rollover de
  // millis().
  uint32_t uptimeS = 0;

  // Indica se volumeL representa uma amostra valida do firmware.
  bool sampleValid = false;

  // Taxa recente do volume, em litros por minuto. hasRate permanece falso quando
  // a janela em RAM ainda nao formou uma base suficiente.
  float rateLpm = 0.0f;
  bool hasRate = false;

  // Fonte unica dos agregados usados pelo /status.
  const History *history = nullptr;

  // Estado e fila de alertas. Nao-const de proposito: o Telegram confirma o
  // alerta somente depois de sendMessage() concluir com sucesso.
  AlertMonitor *alerts = nullptr;
};

class Telegram {
 public:
  // Inicializa o estado do modulo sem abrir conexao de rede.
  void begin(uint32_t nowMs);

  // Deve ser chamado continuamente pelo loop principal.
  void update(uint32_t nowMs, const TelegramContext &context);

  bool enabled() const { return m_enabled; }
  uint32_t polls() const { return m_polls; }
  uint32_t commands() const { return m_commands; }
  uint32_t failures() const { return m_failures; }
  int64_t updateOffset() const { return m_updateOffset; }

  // Instante millis() da ultima consulta bem-sucedida (HTTP 200 + ok:true).
  // Zero significa que nenhuma consulta foi concluida com sucesso desde o boot.
  uint32_t lastSuccessMs() const { return m_lastSuccessMs; }

  // Estado legivel para o diagnostico periodico do Serial.
  // O texto deve ser escrito no buffer fornecido sem alocacao dinamica.
  void stateText(char *out, size_t cap, uint32_t nowMs) const;

 private:
  void pollOnce(uint32_t nowMs, const TelegramContext &context);
  bool sendMessage(int64_t chatId, const char *text);
  void answerCommand(TgCommand command, const TelegramContext &context,
                    int64_t chatId);

  // Aviso de boot: usa o mesmo caminho de envio das respostas de comando.
  void sendBootNotice(uint32_t nowMs, const TelegramContext &context);

  // Entrega no maximo um alerta pendente por consulta. O alerta so e confirmado
  // depois que o envio ao Telegram for confirmado.
  void flushPendingAlerts(uint32_t nowMs, const TelegramContext &context);

  bool m_enabled = false;
  bool m_firstPoll = true;
  bool m_pendingWarned = false;
  bool m_heapWarned = false;
  int64_t m_updateOffset = 0;
  uint32_t m_lastPollMs = 0;
  uint32_t m_lastHeapWarnMs = 0;
  uint32_t m_lastSuccessMs = 0;

  // Diagnostico do gate de heap: instante em que o adiamento comecou.
  bool m_gateBlocked = false;
  uint32_t m_gateBlockedSinceMs = 0;

  // Motivo do boot, classificado uma vez durante begin().
  uint8_t m_bootReason = 0;
  uint8_t m_bootNoticeAttempts = 0;
  bool m_bootNoticeSent = false;

  uint32_t m_polls = 0;
  uint32_t m_commands = 0;
  uint32_t m_failures = 0;
};