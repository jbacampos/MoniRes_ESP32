#pragma once

#include <Arduino.h>
#include <stdint.h>

#include "wifi_manager.h"

// ============================================================================
//  Atualizacao remota pelo GitHub (B5.2) - contrato do motor
//
//  attempt() e BLOQUEANTE e roda uma unica vez por pedido, no boot, ANTES dos
//  servicos de aplicacao (ThingsBoard, History, AlertMonitor, Telegram). Ele:
//
//    1. espera Wi-Fi e NTP (sem relogio, a validade dos certificados nao pode
//       ser checada);
//    2. consulta a ultima release (api.github.com, TLS com CA propria);
//    3. recusa draft, pre-release, tag invalida e versao nao posterior;
//    4. localiza o asset MoniRes.bin e valida tamanho/digest publicados;
//    5. baixa em fluxo continuo para a particao INATIVA, calculando SHA-256;
//    6. aborta o Update em qualquer divergencia (o firmware atual continua
//       bootavel, porque Update.end() so e chamado depois de tudo conferido);
//    7. em sucesso grava o marcador "applied" e reinicia.
//
//  Em QUALQUER falha retorna e o boot normal prossegue; nada e tentado de novo
//  automaticamente (o marcador de pedido ja foi consumido pelo chamador).
//
//  O /ota usa DUAS portas de entrada:
//    - check(), durante o atendimento do comando, para saber se ha versao
//      posterior e responder com a verdade (sem reboot quando nao ha);
//    - attempt(), no boot seguinte, que repete a consulta e instala a imagem.
// ============================================================================
namespace github_ota {

enum class Result : uint8_t {
  OK = 0,             // imagem instalada (attempt() nao retorna neste caso)
  NO_NETWORK,         // Wi-Fi ou NTP indisponiveis dentro do tempo previsto
  NO_RELEASE,         // API respondeu 404 (nenhuma release publicada)
  FORBIDDEN,          // 403/429 (acesso recusado ou limite de taxa)
  BAD_RESPONSE,       // status/JSON/campos obrigatorios invalidos
  NOT_NEWER,          // versao igual/anterior, draft ou pre-release
  NO_ASSET,           // asset MoniRes.bin ausente ou com tamanho invalido
  NO_PARTITION,       // tabela sem segundo slot de aplicacao
  TARGET_SIZE,        // imagem maior que a particao de destino
  DOWNLOAD_FAILED,    // erro de rede/HTTP durante o download
  SIZE_MISMATCH,      // bytes recebidos != tamanho anunciado
  DIGEST_MISMATCH,    // SHA-256 divergente: imagem recusada
  INSTALL_FAILED,     // Update.begin/write/end recusou a gravacao
};

// Release publicada: o que o /ota mostra ao operador ANTES do reboot e o que a
// instalacao usa depois dele.
struct Release {
  char tag[24] = {0};  // tag publicada (preenchida tambem quando e draft,
                       // pre-release ou nao posterior: serve de explicacao)
  uint32_t assetSize = 0;
  bool hasDigest = false;
  uint8_t digest[32] = {0};
  String assetUrl;
};

// Consulta a ultima release e compara com a versao instalada. NAO baixa imagem
// e NAO grava flash: e o passo que o /ota executa DURANTE o atendimento do
// comando, para responder "ha versao nova?" com a verdade e evitar reboot
// inutil. Result::OK = existe versao posterior com o asset esperado (nesse caso
// `out` fica preenchida). Exige Wi-Fi e NTP ja ativos; sem eles devolve
// NO_NETWORK.
Result check(Release &out);

// Executa a tentativa. `bootMs` serve apenas para o log (marca do inicio do
// setup); `wifi` e bombeado enquanto espera rede/relogio.
Result attempt(uint32_t bootMs, WiFiManager &wifi);

// Texto curto do resultado, para o Serial (nunca contem token/credencial).
const char *resultText(Result result);

// Mesma informacao ACENTUADA, para as MENSAGENS DO TELEGRAM. As duas versoes
// existem de proposito: o log serial deste projeto e ASCII puro, enquanto as
// mensagens do Telegram usam acentuacao em UTF-8. Nao trocar uma pela outra.
const char *resultTextTelegram(Result result);

}  // namespace github_ota
