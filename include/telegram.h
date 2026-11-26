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
//  Comandos: /help, /status e /warnings, com as abreviacoes /h, /s e /w, e
//  /reboot e /ota (sem abreviacao). Alem das respostas, o modulo entrega o
//  aviso de boot e os alertas de nivel produzidos por AlertMonitor, uma
//  mensagem por consulta.
//
//  /reboot grava o PROXIMO update_id antes de reiniciar: nos slots A/B de 24 B
//  em LittleFS (TELEGRAM_OFFSET_FILE_A/B) e em um espelho na NVS, e retoma o
//  maior valor no begin() seguinte. O espelho existe porque o LittleFS pode
//  estar indisponivel (tabela de particoes recem-trocada, uploadfs pendente);
//  sem o offset o Telegram reenviaria o comando e o dispositivo entraria em
//  ciclo. Nada e escrito em flash durante o polling normal.
//
//  /ota segue o mesmo preparo e ainda grava o marcador de pedido em NVS
//  (include/ota_marker.h). A verificacao no GitHub acontece no boot seguinte,
//  em src/main.cpp, ANTES dos servicos de aplicacao.
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

  // Motivo (curto) de uma verificacao OTA que rodou no boot e nao instalou.
  // Entra como linha extra no aviso de boot; sem chamada, o aviso nao muda.
  void setOtaFailure(const char *reason);

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

  // /reboot: offset persistido -> confirmacao enviada -> ESP.restart(). Se
  // qualquer etapa falhar o dispositivo NAO reinicia.
  void handleReboot(int64_t chatId);

  // /ota: offset persistido -> marcador NVS -> confirmacao -> ESP.restart().
  // Mesma regra: falha em qualquer etapa cancela o pedido sem reiniciar.
  void handleOta(int64_t chatId);
  void rejectOta(int64_t chatId, const char *reason);

  // Resposta informativa do /ota (sem reiniciar): reusa o caminho de envio, mas
  // nao conta como falha do comando.
  void sendOtaInfo(int64_t chatId, const char *text, const char *label);

  // Offset do getUpdates que sobrevive ao reboot (arquivos A/B, 24 B, CRC32).
  bool loadUpdateOffset();
  bool persistUpdateOffset(int64_t nextOffset);

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

  // Estado dos slots A/B do offset persistido (formato em src/telegram.cpp).
  // m_offsetSlot e o slot que contem o registro carregado; a proxima gravacao
  // sempre usa o outro.
  uint8_t m_offsetSlot = 0;
  uint32_t m_offsetGeneration = 0;
  bool m_fsWarned = false;

  // Diagnostico do gate de heap: instante em que o adiamento comecou.
  bool m_gateBlocked = false;
  uint32_t m_gateBlockedSinceMs = 0;

  // Motivo do boot, classificado uma vez durante begin().
  uint8_t m_bootReason = 0;

  // Linha extra do aviso de boot quando a verificacao OTA do boot nao instalou
  // (texto pronto, montado por setOtaFailure()).
  char m_otaFailure[96] = {0};
  uint8_t m_bootNoticeAttempts = 0;
  bool m_bootNoticeSent = false;

  uint32_t m_polls = 0;
  uint32_t m_commands = 0;
  uint32_t m_failures = 0;
};