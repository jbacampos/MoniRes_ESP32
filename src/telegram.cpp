#include "telegram.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <Preferences.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <esp_system.h>

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include "boot_reason.h"
#include "clock.h"
#include "config.h"
#include "credentials.h"
#include "github_ota.h"
#include "history_record.h"
#include "ota_marker.h"
#include "queue_record.h"
#include "telegram_ca.h"

namespace {

WiFiClientSecure g_client;
bool g_clientConfigured = false;
uint32_t g_sessionStartMs = 0;

char g_body[TELEGRAM_BODY_MAX];
constexpr size_t kFormCapacity = 1280;
char g_form[kFormCapacity];
char g_message[TG_MESSAGE_MAX];
char g_request[256];
char g_line[128];
char g_normalized[TELEGRAM_TEXT_MAX];

constexpr size_t kJsonCapacity = 2048;

// ---------------------------------------------------------------------------
//  Offset persistido do getUpdates (usado apenas pelo /reboot)
//
//  Registro de 24 B em dois slots A/B, com CRC32 - mesmo padrao de
//  ALERT_FILE_A/B: a gravacao vai sempre para o slot INATIVO e a leitura
//  escolhe a maior geracao valida, de modo que uma escrita interrompida deixa o
//  registro anterior intacto. Layout:
//    0..3   magic "TGO1"
//    4      versao / 5 tamanho do registro / 6..7 reservado
//    8..11  geracao
//   12..19  proximo offset (update_id + 1, uint64 little-endian)
//   20..23  CRC32 dos 20 bytes anteriores
// ---------------------------------------------------------------------------
constexpr uint32_t kOffsetMagic = 0x314F4754UL;  // "TGO1"
constexpr uint8_t kOffsetVersion = 1;
constexpr uint8_t kOffsetRecordSize = 24;

struct OffsetRecord {
  uint32_t generation = 0;
  int64_t offset = 0;
};

void encodeOffsetRecord(uint8_t out[kOffsetRecordSize],
                        const OffsetRecord &record) {
  memset(out, 0, kOffsetRecordSize);
  queue_record::put32(out + 0, kOffsetMagic);
  out[4] = kOffsetVersion;
  out[5] = kOffsetRecordSize;
  queue_record::put32(out + 8, record.generation);
  queue_record::put64(out + 12, static_cast<uint64_t>(record.offset));
  queue_record::put32(out + 20, queue_record::crc32(out, 20));
}

// Le `out` (obrigatorio) inteiramente: o registro tem tamanho fixo e o CRC cobre
// todos os campos variaveis.
bool decodeOffsetRecord(const uint8_t in[kOffsetRecordSize],
                        OffsetRecord &record) {
  if (queue_record::get32(in + 0) != kOffsetMagic ||
      in[4] != kOffsetVersion || in[5] != kOffsetRecordSize ||
      queue_record::get32(in + 20) != queue_record::crc32(in, 20)) {
    return false;
  }
  const int64_t offset = static_cast<int64_t>(queue_record::get64(in + 12));
  if (offset < 0) {
    return false;
  }
  record.generation = queue_record::get32(in + 8);
  record.offset = offset;
  return true;
}

bool readOffsetFile(const char *path, uint8_t raw[kOffsetRecordSize]) {
  File file = LittleFS.open(path, "r");
  if (!file) {
    return false;
  }
  const size_t read = file.read(raw, kOffsetRecordSize);
  file.close();
  return read == kOffsetRecordSize;
}

// ---------------------------------------------------------------------------
//  Espelho do offset em NVS
//
//  O LittleFS A/B continua sendo o registro principal, mas ele pode estar
//  indisponivel (tabela de particoes recem-trocada, uploadfs pendente, FS
//  corrompido). Sem o offset, um /reboot ou /ota nao pode reiniciar: o Telegram
//  reenviaria o mesmo update no boot seguinte e entraria em ciclo. A NVS
//  independe do filesystem e resolve esse caso (8 bytes, escrita apenas quando
//  o valor muda).
// ---------------------------------------------------------------------------
constexpr char kNvsOffsetNamespace[] = "montg";
constexpr char kNvsOffsetKey[] = "off";

bool readNvsOffset(int64_t &value) {
  Preferences prefs;
  if (!prefs.begin(kNvsOffsetNamespace, true)) {
    return false;
  }
  const int64_t stored = prefs.getLong64(kNvsOffsetKey, -1);
  prefs.end();
  if (stored < 0) {
    return false;
  }
  value = stored;
  return true;
}

bool writeNvsOffset(int64_t value) {
  Preferences prefs;
  if (!prefs.begin(kNvsOffsetNamespace, false)) {
    return false;
  }
  if (prefs.getLong64(kNvsOffsetKey, -1) == value) {
    prefs.end();
    return true;  // nada mudou: nao regrava (evita desgaste da NVS)
  }
  const size_t written = prefs.putLong64(kNvsOffsetKey, value);
  prefs.end();
  return written == sizeof(int64_t);
}

bool deadlineExpired(uint32_t deadline) {
  return static_cast<int32_t>(millis() - deadline) >= 0;
}

void closeClient() {
  if (g_client.connected()) {
    g_client.stop();
  }
  g_sessionStartMs = 0;
}

void configureClient() {
  if (g_clientConfigured) {
    return;
  }

  g_client.setCACert(TELEGRAM_CA);
  g_client.setHandshakeTimeout(
      (TELEGRAM_RESPONSE_TIMEOUT_MS + 999UL) / 1000UL);
  g_client.setTimeout((TELEGRAM_SOCKET_TIMEOUT_MS + 999UL) / 1000UL);
  g_clientConfigured = true;
}

bool ensureConnection(uint32_t nowMs) {
  configureClient();

  if (g_client.connected()) {
    if (g_sessionStartMs == 0 ||
        static_cast<uint32_t>(nowMs - g_sessionStartMs) <
            TELEGRAM_SESSION_MAX_AGE_MS) {
      return true;
    }
    Serial.printf("[%7.2f s] TG: renovando sessao TLS por idade\n",
                  static_cast<double>(nowMs) / 1000.0);
    closeClient();
  }

  closeClient();
  const uint32_t startedMs = millis();
  if (!g_client.connect(TELEGRAM_HOST, TELEGRAM_PORT,
                        static_cast<int32_t>(TELEGRAM_SOCKET_TIMEOUT_MS))) {
    char errorText[96] = {};
    const int errorCode = g_client.lastError(errorText, sizeof(errorText));
    Serial.printf("[%7.2f s] TG: TLS falhou (codigo=%d, erro=%s)\n",
                  static_cast<double>(nowMs) / 1000.0, errorCode,
                  errorCode != 0 ? errorText : "desconhecido");
    closeClient();
    return false;
  }

  g_sessionStartMs = nowMs;
  Serial.printf("[%7.2f s] TG: TLS conectado em %lu ms\n",
                static_cast<double>(nowMs) / 1000.0,
                static_cast<unsigned long>(millis() - startedMs));
  return true;
}

int readByte(uint32_t deadline) {
  while (!deadlineExpired(deadline)) {
    if (g_client.available()) {
      return g_client.read();
    }
    if (!g_client.connected()) {
      return -1;
    }
    yield();
  }
  return -1;
}

bool readLine(uint32_t deadline, char *out, size_t capacity) {
  if (out == nullptr || capacity < 2) {
    return false;
  }

  size_t length = 0;
  for (;;) {
    const int value = readByte(deadline);
    if (value < 0) {
      return false;
    }
    if (value == '\n') {
      out[length] = '\0';
      return true;
    }
    if (value == '\r') {
      continue;
    }
    if (length + 1 >= capacity) {
      return false;
    }
    out[length++] = static_cast<char>(value);
  }
}

bool writeAll(const char *data, size_t length, uint32_t deadline) {
  size_t written = 0;
  while (written < length && !deadlineExpired(deadline)) {
    const size_t count = g_client.write(
        reinterpret_cast<const uint8_t *>(data) + written, length - written);
    if (count == 0) {
      if (!g_client.connected()) {
        return false;
      }
      yield();
      continue;
    }
    written += count;
  }
  return written == length;
}

bool containsIgnoreCase(const char *text, const char *token) {
  if (text == nullptr || token == nullptr) {
    return false;
  }
  const size_t tokenLength = strlen(token);
  for (const char *cursor = text; *cursor != '\0'; ++cursor) {
    size_t index = 0;
    while (index < tokenLength && cursor[index] != '\0' &&
           tolower(static_cast<unsigned char>(cursor[index])) ==
               tolower(static_cast<unsigned char>(token[index]))) {
      ++index;
    }
    if (index == tokenLength) {
      return true;
    }
  }
  return false;
}

bool consumeTrailers(uint32_t deadline) {
  for (;;) {
    if (!readLine(deadline, g_line, sizeof(g_line))) {
      return false;
    }
    if (g_line[0] == '\0') {
      return true;
    }
  }
}

bool parseUnsignedDecimal(const char *text, unsigned long &value) {
  if (text == nullptr || *text == '\0') {
    return false;
  }

  errno = 0;
  char *end = nullptr;
  const unsigned long parsed = strtoul(text, &end, 10);
  if (end == text || errno == ERANGE) {
    return false;
  }
  while (*end == ' ' || *end == '\t') {
    ++end;
  }
  if (*end != '\0') {
    return false;
  }
  value = parsed;
  return true;
}

bool parseChunkLength(const char *text, unsigned long &value) {
  if (text == nullptr || *text == '\0') {
    return false;
  }

  errno = 0;
  char *end = nullptr;
  const unsigned long parsed = strtoul(text, &end, 16);
  if (end == text || errno == ERANGE) {
    return false;
  }
  while (*end == ' ' || *end == '\t') {
    ++end;
  }
  if (*end != '\0') {
    return false;
  }
  value = parsed;
  return true;
}

bool readResponse(uint32_t deadline, char *body, size_t capacity,
                  size_t &bodyLength, int &status) {
  bodyLength = 0;
  status = 0;
  body[0] = '\0';

  if (!readLine(deadline, g_line, sizeof(g_line)) ||
      strncasecmp(g_line, "HTTP/", 5) != 0) {
    return false;
  }

  const char *separator = strchr(g_line, ' ');
  if (separator == nullptr) {
    return false;
  }
  status = atoi(separator + 1);

  bool chunked = false;
  long contentLength = -1;
  for (;;) {
    if (!readLine(deadline, g_line, sizeof(g_line))) {
      return false;
    }
    if (g_line[0] == '\0') {
      break;
    }

    char *colon = strchr(g_line, ':');
    if (colon == nullptr) {
      continue;
    }
    *colon = '\0';
    const char *value = colon + 1;
    while (*value == ' ' || *value == '\t') {
      ++value;
    }

    if (strcasecmp(g_line, "content-length") == 0) {
      unsigned long parsedLength = 0;
      if (!parseUnsignedDecimal(value, parsedLength) ||
          parsedLength >= capacity || parsedLength > LONG_MAX) {
        return false;
      }
      contentLength = static_cast<long>(parsedLength);
    } else if (strcasecmp(g_line, "transfer-encoding") == 0 &&
               containsIgnoreCase(value, "chunked")) {
      chunked = true;
    }
  }

  if (chunked) {
    for (;;) {
      if (!readLine(deadline, g_line, sizeof(g_line))) {
        return false;
      }
      char *extension = strchr(g_line, ';');
      if (extension != nullptr) {
        *extension = '\0';
      }
      unsigned long chunkLength = 0;
      if (!parseChunkLength(g_line, chunkLength)) {
        return false;
      }
      if (chunkLength == 0) {
        if (!consumeTrailers(deadline)) {
          return false;
        }
        body[bodyLength] = '\0';
        return true;
      }
      if (chunkLength >= capacity ||
          bodyLength > capacity - 1 - static_cast<size_t>(chunkLength)) {
        return false;
      }
      for (unsigned long index = 0; index < chunkLength; ++index) {
        const int value = readByte(deadline);
        if (value < 0) {
          return false;
        }
        body[bodyLength++] = static_cast<char>(value);
      }
      body[bodyLength] = '\0';
      if (!readLine(deadline, g_line, sizeof(g_line)) ||
          g_line[0] != '\0') {
        return false;
      }
    }
  }

  if (contentLength < 0 ||
      static_cast<size_t>(contentLength) >= capacity) {
    return false;
  }
  for (long index = 0; index < contentLength; ++index) {
    const int value = readByte(deadline);
    if (value < 0) {
      return false;
    }
    body[bodyLength++] = static_cast<char>(value);
  }
  body[bodyLength] = '\0';
  return true;
}

size_t urlEncode(char *destination, size_t capacity, const char *source) {
  static constexpr char hex[] = "0123456789ABCDEF";
  size_t length = 0;
  for (const unsigned char *cursor =
           reinterpret_cast<const unsigned char *>(source);
       cursor != nullptr && *cursor != '\0'; ++cursor) {
    const bool safe = isalnum(*cursor) != 0 || *cursor == '-' ||
                      *cursor == '_' || *cursor == '.' || *cursor == '~';
    const size_t required = safe ? 1u : 3u;
    if (length + required >= capacity) {
      return 0;
    }
    if (safe) {
      destination[length++] = static_cast<char>(*cursor);
    } else {
      destination[length++] = '%';
      destination[length++] = hex[(*cursor >> 4) & 0x0F];
      destination[length++] = hex[*cursor & 0x0F];
    }
  }
  destination[length] = '\0';
  return length;
}

bool authorizedChat(int64_t chatId) {
  if (ACTIVE_TELEGRAM_CHAT_ID[0] != '\0' &&
      chatId == strtoll(ACTIVE_TELEGRAM_CHAT_ID, nullptr, 10)) {
    return true;
  }
  return ACTIVE_TELEGRAM_CHAT_ID2[0] != '\0' &&
         chatId == strtoll(ACTIVE_TELEGRAM_CHAT_ID2, nullptr, 10);
}

uint8_t currentBootReason() {
  // B5.2: o marcador "applied" (gravado antes do reboot pos-instalacao) tem
  // PRIORIDADE sobre esp_reset_reason(), que sozinho nao distingue uma
  // atualizacao OTA de um /reboot - os dois reiniciam por software. O marcador
  // e consumido aqui: vale para ESTE boot e nao contamina os seguintes.
  if (ota_marker::consumeApplied()) {
    return BOOT_REASON_OTA;
  }
  switch (esp_reset_reason()) {
    case ESP_RST_POWERON:
      return BOOT_REASON_POWER_ON;
    case ESP_RST_INT_WDT:
    case ESP_RST_TASK_WDT:
    case ESP_RST_WDT:
      return BOOT_REASON_HARDWARE_WATCHDOG;
    case ESP_RST_PANIC:
      return BOOT_REASON_EXCEPTION;
    case ESP_RST_SW:
      return BOOT_REASON_SOFTWARE_RESTART;
    case ESP_RST_DEEPSLEEP:
      return BOOT_REASON_DEEP_SLEEP;
    case ESP_RST_EXT:
      return BOOT_REASON_EXTERNAL_SYSTEM;
    default:
      return BOOT_REASON_UNKNOWN;
  }
}

void buildStatusData(const TelegramContext &context, TelegramStatusData &data) {
  data = TelegramStatusData();
  data.hasVolume = context.sampleValid;
  data.volumeL = context.volumeL;
  data.hasRate = context.hasRate;
  data.rateLpm = context.rateLpm;

  if (context.sampleValid && context.alerts != nullptr) {
    data.hasLevel = true;
    data.level = context.alerts->levelForVolume(context.volumeL);
  }

  if (context.history == nullptr || !device_clock::synced()) {
    return;
  }
  const uint64_t epochMs = device_clock::nowMs();
  if (epochMs == 0) {
    return;
  }
  const uint32_t nowSeconds = static_cast<uint32_t>(epochMs / 1000ULL);
  const uint32_t today = history_record::dayIndex(nowSeconds);
  if (today == 0) {
    return;
  }

  HistoryDayReport day;
  if (context.history->dayReport(today, nowSeconds, day)) {
    data.hasToday = true;
    data.todayMinL = day.minL;
    data.todayMaxL = day.maxL;
  }
  if (today > 0 && context.history->dayReport(today - 1, nowSeconds, day)) {
    data.hasYesterday = true;
    data.yesterdayMinL = day.minL;
    data.yesterdayMaxL = day.maxL;
  }

  HistoryPeriodReport period;
  if (context.history->closedPeriodReport(nowSeconds, 7, period)) {
    data.hasLast7 = true;
    data.last7MinL = period.minL;
    data.last7MaxL = period.maxL;
  }
  if (context.history->closedPeriodReport(nowSeconds, 30, period)) {
    data.hasLast30 = true;
    data.last30MinL = period.minL;
    data.last30MaxL = period.maxL;
  }
}

const char *commandName(TgCommand command) {
  switch (command) {
    case TgCommand::Help: return "/help";
    case TgCommand::Status: return "/status";
    case TgCommand::Warnings: return "/warnings";
    case TgCommand::Reboot: return "/reboot";
    case TgCommand::Ota: return "/ota";
    case TgCommand::None:
    default: return "desconhecido";
  }
}

}  // namespace

void Telegram::stateText(char *out, size_t cap, uint32_t nowMs) const {
  if (out == nullptr || cap == 0) {
    return;
  }
  if (!m_enabled) {
    snprintf(out, cap, "off");
  } else if (m_gateBlocked) {
    snprintf(out, cap, "adiado");
  } else if (m_polls == 0) {
    snprintf(out, cap, "aguardando");
  } else {
    snprintf(out, cap, "ultima=%lus",
             static_cast<unsigned long>(
                 static_cast<uint32_t>(nowMs - m_lastPollMs) / 1000UL));
  }
}

void Telegram::begin(uint32_t nowMs) {
  m_enabled = !telegramCredentialsPending();
  m_firstPoll = true;
  m_updateOffset = 0;
  m_lastPollMs = nowMs -
                 (TELEGRAM_POLL_INTERVAL_MS - TELEGRAM_FIRST_POLL_DELAY_MS);
  m_lastHeapWarnMs = 0;
  m_lastSuccessMs = 0;
  m_gateBlocked = false;
  m_gateBlockedSinceMs = 0;
  m_bootReason = currentBootReason();
  m_bootNoticeAttempts = 0;
  m_bootNoticeSent = false;
  m_polls = 0;
  m_commands = 0;
  m_failures = 0;
  m_pendingWarned = false;
  m_heapWarned = false;
  m_offsetSlot = 0;
  m_offsetGeneration = 0;
  m_fsWarned = false;

  closeClient();
  if (!m_enabled) {
    m_pendingWarned = true;
    Serial.printf("Telegram: desativado (credenciais pendentes em %s).\n",
                  activeSecretsFileName());
    return;
  }
  Serial.printf("Telegram: ativo | consulta a cada %lu s | HTTPS/TLS\n",
                static_cast<unsigned long>(TELEGRAM_POLL_INTERVAL_MS / 1000UL));

  // Um /reboot anterior deixou o PROXIMO offset em flash. Retoma-lo impede que o
  // Telegram reenvie o proprio /reboot (ou qualquer update ja confirmado) depois
  // do boot. Com offset retomado existe update confirmado, portanto o backlog do
  // primeiro poll NAO e descartado - o fluxo normal de polling segue intacto.
  if (loadUpdateOffset()) {
    m_firstPoll = false;
    Serial.printf("Telegram: offset retomado=%lld (geracao=%lu)\n",
                  static_cast<long long>(m_updateOffset),
                  static_cast<unsigned long>(m_offsetGeneration));
  }
}

bool Telegram::loadUpdateOffset() {
  bool found = false;
  OffsetRecord chosen;

  // 1) Registro principal: LittleFS A/B, com CRC e geracao.
  const size_t totalBytes = LittleFS.totalBytes();
  const size_t usedBytes = LittleFS.usedBytes();
  if (totalBytes != 0 && usedBytes <= totalBytes) {
    const char *paths[2] = {TELEGRAM_OFFSET_FILE_A, TELEGRAM_OFFSET_FILE_B};
    for (uint8_t index = 0; index < 2; ++index) {
      uint8_t raw[kOffsetRecordSize];
      if (!readOffsetFile(paths[index], raw)) {
        continue;
      }
      OffsetRecord decoded;
      if (!decodeOffsetRecord(raw, decoded)) {
        Serial.printf("TG: %s invalido (CRC/versao); ignorado\n", paths[index]);
        continue;
      }
      if (!found || decoded.generation >= chosen.generation) {
        chosen = decoded;
        m_offsetSlot = index;
        found = true;
      }
    }
  } else {
    Serial.println("TG: LittleFS indisponivel; usando o espelho em NVS");
  }

  // 2) Espelho em NVS: so vale se estiver a frente do registro do filesystem.
  // Slot e geracao continuam vindo do LittleFS, para a alternancia A/B nao
  // retroceder quando o FS voltar a montar.
  int64_t nvsOffset = 0;
  if (readNvsOffset(nvsOffset) && (!found || nvsOffset > chosen.offset)) {
    chosen.offset = nvsOffset;
    found = true;
  }

  if (!found) {
    return false;
  }
  m_updateOffset = chosen.offset;
  m_offsetGeneration = chosen.generation;
  return true;
}

bool Telegram::persistUpdateOffset(int64_t nextOffset) {
  // 1) Espelho em NVS: sempre tentado, inclusive com o LittleFS fora do ar
  // (tabela de particoes nova, uploadfs pendente, FS corrompido).
  bool stored = writeNvsOffset(nextOffset);
  if (!stored) {
    Serial.println("TG: NVS indisponivel; offset somente no LittleFS");
  }

  // 2) Registro principal em LittleFS: slots A/B, CRC e releitura.
  const size_t totalBytes = LittleFS.totalBytes();
  const size_t usedBytes = LittleFS.usedBytes();
  if (totalBytes == 0 || usedBytes > totalBytes) {
    if (!m_fsWarned) {
      m_fsWarned = true;
      Serial.println(
          "TG: LittleFS indisponivel; offset mantido apenas em NVS.");
    }
    return stored;
  }

  const uint32_t generation = m_offsetGeneration + 1;
  uint8_t raw[kOffsetRecordSize];
  encodeOffsetRecord(raw, OffsetRecord{generation, nextOffset});

  const char *path =
      (m_offsetSlot == 0) ? TELEGRAM_OFFSET_FILE_B : TELEGRAM_OFFSET_FILE_A;
  File file = LittleFS.open(path, "w");
  if (!file) {
    Serial.printf("TG: falha ao gravar o offset em %s\n", path);
    return stored;
  }
  const size_t written = file.write(raw, sizeof(raw));
  file.close();
  if (written != sizeof(raw)) {
    Serial.printf("TG: gravacao parcial do offset em %s (%u/%u B)\n", path,
                  static_cast<unsigned>(written),
                  static_cast<unsigned>(sizeof(raw)));
    return stored;
  }

  // Releitura de verificacao: o reboot so prossegue com o offset confirmado em
  // flash, porque e esse valor que impede o reenvio do mesmo update no boot.
  uint8_t check[kOffsetRecordSize];
  OffsetRecord verified;
  if (!readOffsetFile(path, check) || !decodeOffsetRecord(check, verified) ||
      verified.offset != nextOffset) {
    Serial.printf("TG: offset em %s nao confere apos a gravacao\n", path);
    return stored;
  }

  m_offsetGeneration = generation;
  m_offsetSlot = static_cast<uint8_t>(m_offsetSlot == 0 ? 1 : 0);
  return true;
}

void Telegram::update(uint32_t nowMs, const TelegramContext &context) {
  if (!m_enabled) {
    return;
  }
  if (WiFi.status() != WL_CONNECTED || !device_clock::synced()) {
    closeClient();
    return;
  }
  if (static_cast<uint32_t>(nowMs - m_lastPollMs) <
      TELEGRAM_POLL_INTERVAL_MS) {
    return;
  }

  m_lastPollMs = nowMs;
  pollOnce(nowMs, context);
}

void Telegram::setOtaFailure(const char *reason) {
  if (reason == nullptr || reason[0] == '\0') {
    m_otaFailure[0] = '\0';
    return;
  }
  snprintf(m_otaFailure, sizeof(m_otaFailure),
           TG_TEXT("OTA: não atualizado (%s)"), reason);
}

void Telegram::sendBootNotice(uint32_t nowMs,
                              const TelegramContext &context) {
  if (m_bootNoticeSent ||
      m_bootNoticeAttempts >= TELEGRAM_BOOT_NOTICE_MAX_ATTEMPTS) {
    return;
  }
  const int64_t chatId = strtoll(ACTIVE_TELEGRAM_CHAT_ID, nullptr, 10);
  if (chatId == 0 || !ensureConnection(nowMs)) {
    ++m_bootNoticeAttempts;
    return;
  }

  const bool hasVolume = context.sampleValid && context.alerts != nullptr;
  const alerts::Level level =
      hasVolume ? context.alerts->levelForVolume(context.volumeL)
                : alerts::Level::NORMAL;
  buildBootMessage(m_bootReason, FIRMWARE_VERSION, level, hasVolume,
                   context.volumeL, g_message, sizeof(g_message),
                   m_otaFailure);
  ++m_bootNoticeAttempts;
  if (sendMessage(chatId, g_message)) {
    m_bootNoticeSent = true;
    Serial.println("TG: aviso de boot enviado");
  }
}

void Telegram::flushPendingAlerts(uint32_t nowMs,
                                  const TelegramContext &context) {
  if (context.alerts == nullptr ||
      !context.alerts->peekPending(context.rateLpm, context.hasRate, g_message,
                                   sizeof(g_message))) {
    return;
  }
  const int64_t chatId = strtoll(ACTIVE_TELEGRAM_CHAT_ID, nullptr, 10);
  if (chatId == 0 || !ensureConnection(nowMs)) {
    return;
  }
  if (!sendMessage(chatId, g_message)) {
    ++m_failures;
    return;
  }
  context.alerts->confirmPending();
  Serial.printf("TG: alerta enviado (pendentes=%u)\n",
                static_cast<unsigned>(context.alerts->pendingCount()));
}

void Telegram::pollOnce(uint32_t nowMs, const TelegramContext &context) {
  ++m_polls;
  if (!ensureConnection(nowMs)) {
    ++m_failures;
    return;
  }

  sendBootNotice(nowMs, context);
  flushPendingAlerts(nowMs, context);

  const char *offset = m_firstPoll ? "-1" : nullptr;
  char offsetText[32];
  if (!m_firstPoll) {
    snprintf(offsetText, sizeof(offsetText), "%lld",
             static_cast<long long>(m_updateOffset));
    offset = offsetText;
  }

  Serial.printf("TG: getUpdates poll=%lu first=%u offset=%s\n",
                static_cast<unsigned long>(m_polls),
                m_firstPoll ? 1U : 0U, offset);

  const int requestLength = snprintf(
      g_request, sizeof(g_request),
      "GET /bot%s/getUpdates?offset=%s&limit=1&timeout=0 HTTP/1.1\r\n"
      "Host: %s\r\nConnection: keep-alive\r\n\r\n",
      ACTIVE_TELEGRAM_BOT_TOKEN, offset, TELEGRAM_HOST);
  if (requestLength <= 0 ||
      static_cast<size_t>(requestLength) >= sizeof(g_request)) {
    ++m_failures;
    closeClient();
    return;
  }

  const uint32_t deadline = millis() + TELEGRAM_RESPONSE_TIMEOUT_MS;
  if (!writeAll(g_request, static_cast<size_t>(requestLength), deadline)) {
    ++m_failures;
    closeClient();
    return;
  }

  int status = 0;
  size_t bodyLength = 0;
  if (!readResponse(deadline, g_body, sizeof(g_body), bodyLength, status) ||
      status != 200) {
    ++m_failures;
    Serial.printf("TG: getUpdates falhou (HTTP=%d)\n", status);
    closeClient();
    return;
  }

  Serial.printf("TG: getUpdates HTTP=200 body=%u\n",
                static_cast<unsigned>(bodyLength));

  DynamicJsonDocument document(kJsonCapacity);
  const DeserializationError error =
      deserializeJson(document, g_body, bodyLength);
  if (error || !document["ok"].as<bool>()) {
    ++m_failures;
    Serial.printf("TG: JSON getUpdates invalido: %s overflow=%u uso=%u\n",
                  error.c_str(), document.overflowed() ? 1U : 0U,
                  static_cast<unsigned>(document.memoryUsage()));
    closeClient();
    return;
  }

  Serial.printf("TG: JSON ok uso=%u overflow=%u\n",
                static_cast<unsigned>(document.memoryUsage()),
                document.overflowed() ? 1U : 0U);

  JsonArray result = document["result"].as<JsonArray>();
  if (result.isNull() || result.size() == 0) {
    Serial.printf("TG: getUpdates ok result=0 first=%u offset_atual=%lld\n",
                  m_firstPoll ? 1U : 0U,
                  static_cast<long long>(m_updateOffset));
    m_firstPoll = false;
    m_lastSuccessMs = nowMs;
    return;
  }

  Serial.printf("TG: getUpdates ok result=%u\n",
                static_cast<unsigned>(result.size()));

  JsonObject update = result[0].as<JsonObject>();
  if (update.isNull() || !update.containsKey("update_id")) {
    ++m_failures;
    Serial.println("TG: update sem update_id");
    return;
  }
  const int64_t updateId = update["update_id"].as<int64_t>();
  Serial.printf("TG: update_id=%lld first=%u offset_anterior=%lld\n",
                static_cast<long long>(updateId), m_firstPoll ? 1U : 0U,
                static_cast<long long>(m_updateOffset));

  if (m_firstPoll) {
    m_updateOffset = updateId + 1;
    m_firstPoll = false;
    m_lastSuccessMs = nowMs;
    Serial.printf("TG: backlog descartado | proximo_offset=%lld\n",
                  static_cast<long long>(m_updateOffset));
    return;
  }

  m_updateOffset = updateId + 1;
  m_lastSuccessMs = nowMs;
  Serial.printf("TG: offset avancado para %lld\n",
                static_cast<long long>(m_updateOffset));

  Serial.printf(
      "TG: tipos update message=%u edited=%u channel_post=%u "
      "edited_channel_post=%u callback=%u my_chat_member=%u\n",
      update.containsKey("message") ? 1U : 0U,
      update.containsKey("edited_message") ? 1U : 0U,
      update.containsKey("channel_post") ? 1U : 0U,
      update.containsKey("edited_channel_post") ? 1U : 0U,
      update.containsKey("callback_query") ? 1U : 0U,
      update.containsKey("my_chat_member") ? 1U : 0U);

  JsonVariant messageVariant = update["message"];
  JsonObject message = messageVariant.as<JsonObject>();
  const bool hasMessage = !message.isNull();
  const bool hasText = hasMessage && message.containsKey("text");
  const bool hasChat = hasMessage && message.containsKey("chat");
  const bool hasChatId = hasChat && message["chat"].containsKey("id");
  const char *text = hasText ? message["text"].as<const char *>() : nullptr;
  Serial.printf(
      "TG: message valido=%u text=%u chat=%u chat_id=%u msg_id=%lld date=%lld\n",
      hasMessage ? 1U : 0U, hasText ? 1U : 0U, hasChat ? 1U : 0U,
      hasChatId ? 1U : 0U,
      static_cast<long long>(message["message_id"].as<int64_t>()),
      static_cast<long long>(message["date"].as<int64_t>()));
  if (text == nullptr || !hasChat || !hasChatId) {
    Serial.println("TG: update sem message.text ou message.chat.id");
    return;
  }
  const int64_t chatId = message["chat"]["id"].as<int64_t>();
  if (!authorizedChat(chatId)) {
    Serial.printf("TG: update ignorado (chat nao autorizado=%lld)\n",
                  static_cast<long long>(chatId));
    return;
  }

  const TgCommand command =
      parseTgCommand(text, g_normalized, sizeof(g_normalized));
  if (command == TgCommand::None) {
    Serial.printf("TG: texto ignorado (chat=%lld)\n",
                  static_cast<long long>(chatId));
    return;
  }
  ++m_commands;
  Serial.printf("TG: comando recebido=%s (chat=%lld)\n", commandName(command),
                static_cast<long long>(chatId));
  answerCommand(command, context, chatId);
}

void Telegram::answerCommand(TgCommand command,
                             const TelegramContext &context, int64_t chatId) {
  if (command == TgCommand::Reboot) {
    // Fluxo proprio: persiste o offset, confirma e so entao reinicia.
    handleReboot(chatId);
    return;
  }
  if (command == TgCommand::Ota) {
    // Fluxo proprio: offset + marcador NVS, confirma e reinicia. A consulta ao
    // GitHub acontece no boot, nunca aqui.
    handleOta(chatId);
    return;
  }
  if (command == TgCommand::Help) {
    buildHelpMessage(g_message, sizeof(g_message));
  } else if (command == TgCommand::Status) {
    TelegramStatusData data;
    buildStatusData(context, data);
    buildStatusMessage(data, FIRMWARE_VERSION, context.uptimeS, g_message,
                       sizeof(g_message));
  } else if (command == TgCommand::Warnings) {
    if (context.alerts == nullptr) {
      return;
    }
    alerts::Config requested = context.alerts->config();
    const WarningsParse parsed = parseWarnings(tgArgsOf(g_normalized), requested);
    const alerts::Level currentLevel =
        context.sampleValid
            ? context.alerts->levelForVolume(context.volumeL)
            : alerts::Level::NORMAL;

    if (parsed == WarningsParse::SHOW) {
      buildWarningsMessage(requested, currentLevel, context.volumeL,
                           context.sampleValid, g_message, sizeof(g_message));
    } else if (parsed == WarningsParse::INVALID) {
      buildWarningsErrorMessage(
          "informe três números: /w lr lc hist", context.alerts->config(),
          g_message, sizeof(g_message));
    } else {
      const alerts::ConfigError validation = alerts::validate(requested);
      if (validation != alerts::ConfigError::OK) {
        buildWarningsErrorMessage(warningsErrorText(validation),
                                  context.alerts->config(), g_message,
                                  sizeof(g_message));
      } else if (context.alerts->applyConfig(requested, context.volumeL,
                                              context.sampleValid) !=
                 alerts::ConfigError::OK) {
        buildWarningsErrorMessage("não foi possível aplicar os limites",
                                  context.alerts->config(), g_message,
                                  sizeof(g_message));
      } else {
        buildWarningsAppliedMessage(context.alerts->config(),
                                    context.alerts->level(), context.volumeL,
                                    context.sampleValid, g_message,
                                    sizeof(g_message));
      }
    }
  } else {
    return;
  }

  // A consulta getUpdates pode deixar uma conexao keep-alive stale. Renove a
  // sessao antes do unico POST: repetir depois de uma falha de leitura e
  // ambiguo, pois o Telegram pode ja ter aceitado a primeira mensagem.
  closeClient();
  const bool connected = ensureConnection(millis());
  const bool sent = connected && sendMessage(chatId, g_message);
  if (!sent) {
    ++m_failures;
    Serial.printf("TG: falha ao enviar %s\n", commandName(command));
  } else {
    Serial.printf("TG: resposta enviada %s\n", commandName(command));
  }
}

void Telegram::handleReboot(int64_t chatId) {
  // pollOnce() ja avancou m_updateOffset para update_id + 1. Gravar esse valor
  // ANTES de reiniciar e o que impede o Telegram de reenviar o proprio /reboot
  // depois do boot: a primeira getUpdates nasce com o offset ja confirmado.
  if (!persistUpdateOffset(m_updateOffset)) {
    ++m_failures;
    Serial.printf("TG: /reboot cancelado (offset %lld nao persistido)\n",
                  static_cast<long long>(m_updateOffset));
    buildRebootAbortMessage(g_message, sizeof(g_message));
    closeClient();
    const bool abortConnected = ensureConnection(millis());
    if (abortConnected && sendMessage(chatId, g_message)) {
      Serial.println("TG: resposta enviada /reboot cancelado");
    } else {
      ++m_failures;
      Serial.println("TG: falha ao enviar /reboot cancelado");
    }
    return;
  }
  Serial.printf("TG: offset persistido=%lld (geracao=%lu)\n",
                static_cast<long long>(m_updateOffset),
                static_cast<unsigned long>(m_offsetGeneration));

  buildRebootMessage(g_message, sizeof(g_message));
  closeClient();
  const bool connected = ensureConnection(millis());
  const bool sent = connected && sendMessage(chatId, g_message);
  if (!sent) {
    ++m_failures;
    // Sem a confirmacao o operador nao saberia que o pedido foi aceito. O
    // offset ja persistido NAO e revertido de proposito: reenviar o /reboot no
    // proximo poll reiniciaria o dispositivo sem ninguem saber quando.
    Serial.println("TG: /reboot cancelado (confirmacao nao enviada)");
    return;
  }

  Serial.println("TG: /reboot confirmado; reiniciando");
  closeClient();
  Serial.flush();
  delay(TELEGRAM_REBOOT_SETTLE_MS);
  ESP.restart();
}

void Telegram::sendOtaInfo(int64_t chatId, const char *text,
                           const char *label) {
  closeClient();
  if (ensureConnection(millis()) && sendMessage(chatId, text)) {
    Serial.printf("TG: resposta enviada %s\n", label);
  } else {
    ++m_failures;
    Serial.printf("TG: falha ao enviar %s\n", label);
  }
}

void Telegram::rejectOta(int64_t chatId, const char *reason) {
  ++m_failures;
  Serial.printf("TG: /ota cancelado (%s)\n", reason);
  buildOtaAbortMessage(g_message, sizeof(g_message));
  closeClient();
  if (ensureConnection(millis()) && sendMessage(chatId, g_message)) {
    Serial.println("TG: resposta enviada /ota cancelado");
  } else {
    ++m_failures;
    Serial.println("TG: falha ao enviar /ota cancelado");
  }
}

void Telegram::handleOta(int64_t chatId) {
  // Sequencia: consulta a release com o Telegram ainda atendendo o comando ->
  // responde sem reiniciar quando nao ha versao posterior ou quando a consulta
  // falha -> so reinicia quando ha versao posterior, e entao o boot seguinte
  // baixa, grava e reinicia na imagem nova.
  //
  // A sessao TLS do Telegram e fechada antes da consulta: o GitHub abre a dele,
  // e duas sessoes TLS simultaneas no ESP32 seriam heap desperdicado.
  closeClient();

  github_ota::Release release;
  const github_ota::Result check = github_ota::check(release);

  if (check == github_ota::Result::NOT_NEWER) {
    buildOtaUpToDateMessage(release.tag, FIRMWARE_VERSION,
                            github_ota::resultText(check), g_message,
                            sizeof(g_message));
    sendOtaInfo(chatId, g_message, "/ota sem atualizacao");
    return;
  }
  if (check != github_ota::Result::OK) {
    Serial.printf("TG: /ota cancelado (consulta: %s)\n",
                  github_ota::resultText(check));
    buildOtaCheckFailedMessage(github_ota::resultText(check), g_message,
                               sizeof(g_message));
    sendOtaInfo(chatId, g_message, "/ota consulta nao concluida");
    return;
  }

  // Ha versao posterior: os dois estados precisam sobreviver ao reinicio - o
  // offset do update (para o Telegram nao reenviar /ota) e o marcador do pedido
  // (para o boot saber que deve instalar). O offset vem primeiro: sem ele o
  // comando voltaria e o dispositivo entraria em ciclo.
  if (!persistUpdateOffset(m_updateOffset)) {
    rejectOta(chatId, "offset nao persistido");
    return;
  }
  if (!ota_marker::setRequest()) {
    rejectOta(chatId, "marcador NVS nao gravado");
    return;
  }
  Serial.printf("TG: marcador OTA gravado (offset=%lld, %s -> %s)\n",
                static_cast<long long>(m_updateOffset), FIRMWARE_VERSION,
                release.tag);

  buildOtaFoundMessage(release.tag, FIRMWARE_VERSION, release.assetSize,
                       g_message, sizeof(g_message));
  closeClient();
  const bool connected = ensureConnection(millis());
  const bool sent = connected && sendMessage(chatId, g_message);
  if (!sent) {
    ++m_failures;
    // Sem aviso o operador nao saberia que o pedido foi aceito. O marcador e
    // removido para o proximo boot NAO tentar instalar.
    ota_marker::clearRequest();
    Serial.println(
        "TG: /ota cancelado (aviso nao enviado); marcador removido");
    return;
  }

  Serial.println("TG: /ota confirmado; reiniciando para instalar");
  closeClient();
  Serial.flush();
  delay(TELEGRAM_REBOOT_SETTLE_MS);
  ESP.restart();
}

bool Telegram::sendMessage(int64_t chatId, const char *text) {
  if (text == nullptr || !g_client.connected()) {
    return false;
  }

  const int prefix = snprintf(g_form, sizeof(g_form),
                              "chat_id=%lld&parse_mode=HTML&text=",
                              static_cast<long long>(chatId));
  if (prefix <= 0 || static_cast<size_t>(prefix) >= sizeof(g_form)) {
    return false;
  }
  const size_t encoded = urlEncode(
      g_form + prefix, sizeof(g_form) - static_cast<size_t>(prefix), text);
  if (encoded == 0) {
    return false;
  }
  const size_t formLength = static_cast<size_t>(prefix) + encoded;

  const int requestLength = snprintf(
      g_request, sizeof(g_request),
      "POST /bot%s/sendMessage HTTP/1.1\r\n"
      "Host: %s\r\nContent-Type: application/x-www-form-urlencoded\r\n"
      "Content-Length: %u\r\nConnection: keep-alive\r\n\r\n",
      ACTIVE_TELEGRAM_BOT_TOKEN, TELEGRAM_HOST,
      static_cast<unsigned>(formLength));
  if (requestLength <= 0 ||
      static_cast<size_t>(requestLength) >= sizeof(g_request)) {
    return false;
  }

  const uint32_t deadline = millis() + TELEGRAM_RESPONSE_TIMEOUT_MS;
  if (!writeAll(g_request, static_cast<size_t>(requestLength), deadline) ||
      !writeAll(g_form, formLength, deadline)) {
    closeClient();
    return false;
  }

  int status = 0;
  size_t bodyLength = 0;
  if (!readResponse(deadline, g_body, sizeof(g_body), bodyLength, status) ||
      status != 200) {
    closeClient();
    return false;
  }

  DynamicJsonDocument document(512);
  const DeserializationError error = deserializeJson(document, g_body,
                                                      bodyLength);
  if (error || !document["ok"].as<bool>()) {
    closeClient();
    return false;
  }
  return true;
}