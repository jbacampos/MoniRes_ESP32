#include "github_ota.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <Update.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <esp_ota_ops.h>
#include <mbedtls/sha256.h>

#include <stdarg.h>
#include <string.h>

#include "clock.h"
#include "config.h"
#include "github_ota_ca.h"
#include "ota_marker.h"
#include "ota_version.h"

// ============================================================================
//  Atualizacao remota pelo GitHub - implementacao ESP32
//
//  Motor HTTP: HTTPClient do core (escolha aprovada na B5.2). Ele NAO segue
//  redirecionamento sozinho aqui: cada salto abre uma WiFiClientSecure nova,
//  porque a cadeia muda de operadora entre github.com e o CDN dos assets e uma
//  sessao TLS reaproveitada entre hosts diferentes e justamente o tipo de erro
//  silencioso que nao se quer num caminho que grava flash.
//
//  Gravacao: Update (API do core) na particao INATIVA. O SHA-256 e calculado
//  enquanto os bytes passam, e Update.end() - o unico ponto que marca a imagem
//  como bootavel - so roda depois de tamanho e digest conferirem. Qualquer
//  divergencia chama Update.abort(): o slot em execucao nao e tocado e o
//  otadata continua apontando para ele.
// ============================================================================
namespace {

constexpr size_t kDownloadBuffer = 1024;
constexpr size_t kJsonCapacity = 2048;
constexpr size_t kJsonFilterCapacity = 384;
constexpr uint8_t kSha256Bytes = 32;
constexpr uint8_t kRedirectStatusCount = 6;

// ---------------------------------------------------------------------------
//  Log: mesmo formato de marca de tempo do resto do firmware.
// ---------------------------------------------------------------------------
void logLine(const char *format, ...) {
  char prefix[24];
  snprintf(prefix, sizeof(prefix), "[%7.2f s] OTA: ",
           static_cast<double>(millis()) / 1000.0);
  Serial.print(prefix);

  va_list args;
  va_start(args, format);
  char message[192];
  vsnprintf(message, sizeof(message), format, args);
  va_end(args);
  Serial.println(message);
}

// ---------------------------------------------------------------------------
//  URLs
// ---------------------------------------------------------------------------
struct UrlParts {
  String host;
  String path;
};

bool parseHttpsUrl(const String &url, UrlParts &out) {
  if (!url.startsWith("https://")) {
    return false;
  }
  const int hostStart = 8;
  const int slash = url.indexOf('/', hostStart);
  const String host =
      (slash < 0) ? url.substring(hostStart) : url.substring(hostStart, slash);
  if (host.length() == 0 || host.indexOf(':') >= 0) {
    return false;  // GitHub sempre usa 443 sem porta explicita
  }
  out.host = host;
  out.path = (slash < 0) ? String("/") : url.substring(slash);
  return true;
}

// Resolve o cabecalho Location contra a URL atual (absoluto ou caminho raiz).
String resolveLocation(const UrlParts &base, const String &location) {
  if (location.startsWith("https://")) {
    return location;
  }
  if (location.startsWith("/")) {
    return String("https://") + base.host + location;
  }
  return String();
}

bool isRedirectStatus(int status) {
  return status == 301 || status == 302 || status == 303 || status == 307 ||
         status == 308;
}

void configureTls(WiFiClientSecure &client) {
  client.setCACert(GITHUB_OTA_CA);
  client.setHandshakeTimeout((GITHUB_OTA_HTTP_TIMEOUT_MS + 999UL) / 1000UL);
  client.setTimeout((GITHUB_OTA_HTTP_TIMEOUT_MS + 999UL) / 1000UL);
}

}  // namespace

namespace {

// ---------------------------------------------------------------------------
//  Requisicao com um salto por vez
//
//  Cada salto usa uma WiFiClientSecure propria: a cadeia TLS muda de operadora
//  entre github.com e o CDN dos assets, e reaproveitar sessao entre hosts
//  diferentes e exatamente o tipo de erro silencioso que nao se quer num
//  caminho que grava flash.
//
//  ORDEM DE LIBERACAO (corrigida apos panic na bancada): ~HTTPClient() executa
//  _client->stop(), portanto o WiFiClientSecure TEM de estar vivo quando o
//  HTTPClient for destruido. Com o HTTPClient no heap, close() libera o
//  HTTPClient primeiro e so depois apaga o cliente. Na versao anterior o
//  cliente era apagado antes e o destrutor do HTTPClient tocava memoria
//  liberada (LoadProhibited em HTTPClient::~HTTPClient, EXCVADDR 0x3c).
// ---------------------------------------------------------------------------
struct HttpGetResult {
  HTTPClient *http = nullptr;
  WiFiClientSecure *client = nullptr;
  int status = 0;

  void close() {
    if (http != nullptr) {
      http->end();
      delete http;  // ~HTTPClient() aqui, com _client ainda valido
      http = nullptr;
    }
    if (client != nullptr) {
      delete client;
      client = nullptr;
    }
  }

  ~HttpGetResult() { close(); }
};

bool openAndGet(HttpGetResult &result, const String &url, const char *accept) {
  UrlParts parts;
  if (!parseHttpsUrl(url, parts)) {
    logLine("URL recusada: %s", url.c_str());
    return false;
  }

  result.close();
  result.http = new HTTPClient();
  result.client = new WiFiClientSecure();
  if (result.http == nullptr || result.client == nullptr) {
    logLine("sem heap para a sessao TLS");
    result.close();
    return false;
  }
  configureTls(*result.client);

  if (!result.http->begin(*result.client, parts.host.c_str(), 443,
                          parts.path.c_str(), true)) {
    logLine("falha ao iniciar o HTTPClient");
    result.close();
    return false;
  }
  result.http->setUserAgent(GITHUB_OTA_USER_AGENT);
  result.http->addHeader("Accept", accept);
  result.http->setTimeout(GITHUB_OTA_HTTP_TIMEOUT_MS);
  result.http->setConnectTimeout(GITHUB_OTA_HTTP_TIMEOUT_MS);
  result.http->setReuse(false);
  result.http->setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);

  result.status = result.http->GET();
  if (result.status <= 0) {
    logLine("falha de conexao/GET (%d)", result.status);
    result.close();
    return false;
  }
  return true;
}

// Segue os redirecionamentos ate uma resposta que nao seja 3xx.
bool httpGetFollow(const String &url, const char *accept,
                   HttpGetResult &result) {
  String current = url;
  for (uint8_t hop = 0; hop <= GITHUB_OTA_MAX_REDIRECTS; ++hop) {
    if (!openAndGet(result, current, accept)) {
      return false;
    }
    if (!isRedirectStatus(result.status)) {
      return true;
    }
    UrlParts base;
    parseHttpsUrl(current, base);
    // getLocation(), e nao header("Location"): header() so devolve as chaves
    // registradas em collectHeaders(), enquanto o parser guarda o Location em
    // _location de qualquer forma. Era exatamente isso que abortava o salto
    // github.com -> release-assets.githubusercontent.com.
    const String next = resolveLocation(base, result.http->getLocation());
    result.close();
    if (next.length() == 0) {
      logLine("redirecionamento sem Location utilizavel");
      return false;
    }
    current = next;
  }
  logLine("mais de %u redirecionamentos; desistindo",
          static_cast<unsigned>(GITHUB_OTA_MAX_REDIRECTS));
  return false;
}

// ---------------------------------------------------------------------------
//  Release publicada
// ---------------------------------------------------------------------------
// A estrutura vive no header (github_ota::Release): o /ota precisa dela fora
// deste arquivo para comparar a versao ANTES do reboot.
using ReleaseInfo = github_ota::Release;

// "sha256:<64 hex>" -> 32 bytes. Formato desconhecido desliga apenas a
// verificacao por digest (a instalacao continua exigindo tamanho exato e a
// validacao de imagem do proprio Update).
bool parseDigest(const char *digest, ReleaseInfo &info) {
  if (digest == nullptr || strncmp(digest, "sha256:", 7) != 0) {
    return false;
  }
  const char *hex = digest + 7;
  if (strlen(hex) != kSha256Bytes * 2) {
    return false;
  }

  uint8_t parsed[kSha256Bytes];
  for (uint8_t i = 0; i < kSha256Bytes; ++i) {
    uint8_t value = 0;
    for (uint8_t nibble = 0; nibble < 2; ++nibble) {
      const char c = hex[i * 2 + nibble];
      uint8_t digit = 0;
      if (c >= '0' && c <= '9') {
        digit = static_cast<uint8_t>(c - '0');
      } else if (c >= 'a' && c <= 'f') {
        digit = static_cast<uint8_t>(c - 'a' + 10);
      } else if (c >= 'A' && c <= 'F') {
        digit = static_cast<uint8_t>(c - 'A' + 10);
      } else {
        return false;
      }
      value = static_cast<uint8_t>((value << 4) | digit);
    }
    parsed[i] = value;
  }

  memcpy(info.digest, parsed, kSha256Bytes);
  return true;
}

}  // namespace

namespace {

// Espera Wi-Fi e NTP bombeando os modulos. Sem relogio valido a cadeia de
// certificados nao pode ser checada (o ESP32 nasce em 1970 e todo certificado
// pareceria "futuro"), por isso a sincronizacao e requisito, nao preferencia.
bool waitForNetwork(uint32_t bootMs, WiFiManager &wifi) {
  const uint32_t startedMs = millis();
  while (millis() - startedMs < GITHUB_OTA_NETWORK_WAIT_MS) {
    const uint32_t nowMs = millis();
    wifi.update(nowMs);
    device_clock::update(nowMs, wifi.connected());
    if (wifi.connected() && device_clock::synced()) {
      logLine("rede e relogio prontos em %lu ms",
              static_cast<unsigned long>(millis() - startedMs));
      return true;
    }
    delay(100);
  }
  logLine("Wi-Fi/NTP indisponiveis apos %lu ms (setup iniciou em %.2f s)",
          static_cast<unsigned long>(GITHUB_OTA_NETWORK_WAIT_MS),
          static_cast<double>(bootMs) / 1000.0);
  return false;
}

// Consulta a ultima release. OK = publicada, valida, mais nova e com o asset
// esperado; `info` fica preenchida apenas nesse caso.
github_ota::Result fetchLatestRelease(ReleaseInfo &info) {
  HttpGetResult response;
  if (!httpGetFollow(GITHUB_OTA_RELEASE_API, "application/vnd.github+json",
                     response)) {
    return github_ota::Result::BAD_RESPONSE;
  }
  if (response.status == 404) {
    logLine("nenhuma release publicada (HTTP 404)");
    return github_ota::Result::NO_RELEASE;
  }
  if (response.status == 403 || response.status == 429) {
    logLine("API recusou a consulta (HTTP %d)", response.status);
    return github_ota::Result::FORBIDDEN;
  }
  if (response.status != 200) {
    logLine("API respondeu HTTP %d", response.status);
    return github_ota::Result::BAD_RESPONSE;
  }

  const int announced = response.http->getSize();
  if (announced <= 0 ||
      static_cast<size_t>(announced) > GITHUB_OTA_API_MAX_BYTES) {
    logLine("corpo da API ausente ou acima do limite (%d B)", announced);
    return github_ota::Result::BAD_RESPONSE;
  }
  WiFiClient *stream = response.http->getStreamPtr();
  if (stream == nullptr) {
    return github_ota::Result::BAD_RESPONSE;
  }

  // O JSON bruto da release tem ~9 KB (autor, uploader, URLs...). O filtro
  // deixa entrar APENAS os campos usados, e o corpo ainda e lido em fluxo.
  DynamicJsonDocument filter(kJsonFilterCapacity);
  filter["tag_name"] = true;
  filter["draft"] = true;
  filter["prerelease"] = true;
  JsonObject assetFilter = filter["assets"][0].to<JsonObject>();
  assetFilter["name"] = true;
  assetFilter["size"] = true;
  assetFilter["digest"] = true;
  assetFilter["browser_download_url"] = true;

  DynamicJsonDocument document(kJsonCapacity);
  const DeserializationError error = deserializeJson(
      document, *stream, DeserializationOption::Filter(filter));
  response.close();
  if (error) {
    logLine("JSON invalido: %s", error.c_str());
    return github_ota::Result::BAD_RESPONSE;
  }

  const char *tag = document["tag_name"] | "";
  const bool draft = document["draft"] | false;
  const bool prerelease = document["prerelease"] | false;
  logLine("release=%s draft=%u prerelease=%u", tag, draft ? 1U : 0U,
          prerelease ? 1U : 0U);

  // A tag e copiada ANTES das recusas: quando a release e draft, pre-release ou
  // nao posterior, o /ota mostra ao operador qual versao esta publicada e por
  // que nada sera instalado.
  strncpy(info.tag, tag, sizeof(info.tag) - 1);

  if (draft || prerelease) {
    return github_ota::Result::NOT_NEWER;
  }
  if (!ota_version::isNewer(tag, FIRMWARE_VERSION)) {
    logLine("versao publicada nao e posterior a instalada (%s)",
            FIRMWARE_VERSION);
    return github_ota::Result::NOT_NEWER;
  }
  strncpy(info.tag, tag, sizeof(info.tag) - 1);

  for (JsonObject asset : document["assets"].as<JsonArray>()) {
    const char *name = asset["name"] | "";
    if (strcmp(name, GITHUB_OTA_ASSET_NAME) != 0) {
      continue;
    }
    const uint32_t size = asset["size"] | 0U;
    const char *url = asset["browser_download_url"] | "";
    const char *digest = asset["digest"] | "";
    if (size == 0 || strncmp(url, "https://github.com/", 19) != 0) {
      logLine("asset %s com tamanho/URL invalidos", name);
      return github_ota::Result::NO_ASSET;
    }
    info.assetSize = size;
    info.assetUrl = url;
    info.hasDigest = parseDigest(digest, info);
    logLine("asset=%s bytes=%lu digest=%s", name,
            static_cast<unsigned long>(size),
            info.hasDigest ? "sim" : "ausente");
    return github_ota::Result::OK;
  }

  logLine("asset %s ausente na release %s", GITHUB_OTA_ASSET_NAME, tag);
  return github_ota::Result::NO_ASSET;
}

}  // namespace
namespace {

// Baixa, confere e grava. Nada e marcado como bootavel antes de tamanho e
// digest conferirem: Update.end() e o unico ponto que aponta o otadata para a
// imagem nova. Em qualquer falha, Update.abort() deixa o slot em execucao
// intacto.
github_ota::Result downloadAndInstall(const ReleaseInfo &info) {
  const esp_partition_t *target = esp_ota_get_next_update_partition(nullptr);
  if (target == nullptr) {
    logLine("sem particao OTA de destino (tabela atual nao tem app1)");
    return github_ota::Result::NO_PARTITION;
  }
  if (info.assetSize < GITHUB_OTA_MIN_IMAGE_BYTES) {
    logLine("imagem pequena demais (%lu B)",
            static_cast<unsigned long>(info.assetSize));
    return github_ota::Result::NO_ASSET;
  }
  if (info.assetSize > target->size) {
    logLine("imagem (%lu B) maior que o slot %s (%lu B)",
            static_cast<unsigned long>(info.assetSize), target->label,
            static_cast<unsigned long>(target->size));
    return github_ota::Result::TARGET_SIZE;
  }

  HttpGetResult response;
  if (!httpGetFollow(info.assetUrl, "application/octet-stream", response)) {
    return github_ota::Result::DOWNLOAD_FAILED;
  }
  if (response.status != 200) {
    logLine("download respondeu HTTP %d", response.status);
    return github_ota::Result::DOWNLOAD_FAILED;
  }
  const int announced = response.http->getSize();
  if (announced > 0 && static_cast<uint32_t>(announced) != info.assetSize) {
    logLine("Content-Length=%d difere do anunciado na API (%lu)",
            announced, static_cast<unsigned long>(info.assetSize));
    return github_ota::Result::SIZE_MISMATCH;
  }
  WiFiClient *stream = response.http->getStreamPtr();
  if (stream == nullptr) {
    return github_ota::Result::DOWNLOAD_FAILED;
  }

  if (!Update.begin(info.assetSize)) {
    logLine("Update.begin recusou: %s", Update.errorString());
    return github_ota::Result::INSTALL_FAILED;
  }
  logLine("gravando %lu B no slot %s",
          static_cast<unsigned long>(info.assetSize), target->label);

  mbedtls_sha256_context sha;
  mbedtls_sha256_init(&sha);
  if (mbedtls_sha256_starts_ret(&sha, 0) != 0) {
    mbedtls_sha256_free(&sha);
    Update.abort();
    logLine("falha ao iniciar o SHA-256");
    return github_ota::Result::INSTALL_FAILED;
  }
  uint8_t buffer[kDownloadBuffer];
  uint32_t received = 0;
  uint32_t lastDataMs = millis();
  const uint32_t startedMs = millis();
  bool failed = false;

  while (received < info.assetSize) {
    const size_t available = stream->available();
    if (available == 0) {
      if (!response.http->connected()) {
        failed = true;
        break;
      }
      if (millis() - lastDataMs > GITHUB_OTA_IDLE_TIMEOUT_MS) {
        logLine("download parado por %lu ms",
                static_cast<unsigned long>(millis() - lastDataMs));
        failed = true;
        break;
      }
      if (millis() - startedMs > GITHUB_OTA_TOTAL_BUDGET_MS) {
        logLine("orcamento de tempo da tentativa esgotado");
        failed = true;
        break;
      }
      delay(5);
      continue;
    }

    const size_t chunk =
        (available < sizeof(buffer)) ? available : sizeof(buffer);
    const int read = stream->readBytes(buffer, chunk);
    if (read <= 0) {
      failed = true;
      break;
    }
    if (Update.write(buffer, static_cast<size_t>(read)) !=
        static_cast<size_t>(read)) {
      logLine("Update.write falhou: %s", Update.errorString());
      failed = true;
      break;
    }
    mbedtls_sha256_update_ret(&sha, buffer, static_cast<size_t>(read));
    received += static_cast<uint32_t>(read);
    lastDataMs = millis();
  }

  uint8_t computed[kSha256Bytes] = {0};
  const bool hashOk = mbedtls_sha256_finish_ret(&sha, computed) == 0;
  mbedtls_sha256_free(&sha);
  response.close();

  if (failed || !hashOk) {
    Update.abort();
    logLine("download interrompido em %lu de %lu B",
            static_cast<unsigned long>(received),
            static_cast<unsigned long>(info.assetSize));
    return github_ota::Result::DOWNLOAD_FAILED;
  }
  if (received != info.assetSize) {
    Update.abort();
    logLine("recebidos %lu B, esperados %lu B",
            static_cast<unsigned long>(received),
            static_cast<unsigned long>(info.assetSize));
    return github_ota::Result::SIZE_MISMATCH;
  }
  if (info.hasDigest && memcmp(computed, info.digest, kSha256Bytes) != 0) {
    Update.abort();
    logLine("SHA-256 divergente do publicado; imagem recusada");
    return github_ota::Result::DIGEST_MISMATCH;
  }

  if (!Update.end(true) || Update.hasError()) {
    logLine("Update.end falhou: %s", Update.errorString());
    Update.abort();
    return github_ota::Result::INSTALL_FAILED;
  }
  logLine("imagem validada e gravada no slot %s", target->label);
  return github_ota::Result::OK;
}

}  // namespace
github_ota::Result github_ota::check(Release &out) {
  logLine("consulta de versao (instalado=%s)", FIRMWARE_VERSION);
  if (WiFi.status() != WL_CONNECTED || !device_clock::synced()) {
    logLine("Wi-Fi/NTP ainda nao prontos; consulta nao realizada");
    return Result::NO_NETWORK;
  }
  const Result fetched = fetchLatestRelease(out);
  if (fetched == Result::OK) {
    logLine("ha versao posterior: %s (%lu B)", out.tag,
            static_cast<unsigned long>(out.assetSize));
  }
  return fetched;
}

github_ota::Result github_ota::attempt(uint32_t bootMs, WiFiManager &wifi) {
  logLine("verificacao solicitada (instalado=%s)", FIRMWARE_VERSION);
  if (!waitForNetwork(bootMs, wifi)) {
    return Result::NO_NETWORK;
  }

  ReleaseInfo info;
  const Result fetched = fetchLatestRelease(info);
  if (fetched != Result::OK) {
    return fetched;
  }
  logLine("atualizando %s -> %s", FIRMWARE_VERSION, info.tag);

  const Result installed = downloadAndInstall(info);
  if (installed != Result::OK) {
    return installed;
  }

  // Marca o proximo boot como "atualizacao OTA" para o aviso do Telegram. Se a
  // NVS falhar a atualizacao continua valida: apenas o aviso sai como reinicio
  // comum, nunca como OTA.
  if (!ota_marker::setApplied()) {
    logLine("instalado, mas o marcador de boot OTA nao pode ser gravado");
  }
  logLine("concluido; reiniciando com %s", info.tag);
  Serial.flush();
  delay(100);
  ESP.restart();
  return Result::OK;
}

const char *github_ota::resultTextTelegram(Result result) {
  switch (result) {
    case Result::OK:
      return "atualizado";
    case Result::NO_NETWORK:
      return "sem Wi-Fi ou NTP";
    case Result::NO_RELEASE:
      return "nenhuma release publicada (HTTP 404)";
    case Result::FORBIDDEN:
      return "consulta recusada (HTTP 403/429)";
    case Result::BAD_RESPONSE:
      return "resposta da API inválida";
    case Result::NOT_NEWER:
      return "a versão publicada não é posterior à instalada (ou é "
             "rascunho/pré-lançamento)";
    case Result::NO_ASSET:
      return "imagem MoniRes.bin ausente ou inválida";
    case Result::NO_PARTITION:
      return "partição OTA ausente";
    case Result::TARGET_SIZE:
      return "imagem maior que a partição de destino";
    case Result::DOWNLOAD_FAILED:
      return "falha no download";
    case Result::SIZE_MISMATCH:
      return "tamanho recebido diferente do anunciado";
    case Result::DIGEST_MISMATCH:
      return "SHA-256 divergente";
    case Result::INSTALL_FAILED:
      return "falha ao gravar a imagem";
    default:
      return "motivo desconhecido";
  }
}

const char *github_ota::resultText(Result result) {
  switch (result) {
    case Result::OK:
      return "atualizado";
    case Result::NO_NETWORK:
      return "sem Wi-Fi ou NTP";
    case Result::NO_RELEASE:
      return "nenhuma release publicada (HTTP 404)";
    case Result::FORBIDDEN:
      return "consulta recusada (HTTP 403/429)";
    case Result::BAD_RESPONSE:
      return "resposta da API invalida";
    case Result::NOT_NEWER:
      return "versao publicada nao e posterior (ou draft/pre-release)";
    case Result::NO_ASSET:
      return "asset MoniRes.bin ausente ou invalido";
    case Result::NO_PARTITION:
      return "particao OTA ausente";
    case Result::TARGET_SIZE:
      return "imagem maior que a particao de destino";
    case Result::DOWNLOAD_FAILED:
      return "falha no download";
    case Result::SIZE_MISMATCH:
      return "tamanho recebido diferente do anunciado";
    case Result::DIGEST_MISMATCH:
      return "SHA-256 divergente";
    case Result::INSTALL_FAILED:
      return "falha do Update ao gravar";
    default:
      return "desconhecido";
  }
}