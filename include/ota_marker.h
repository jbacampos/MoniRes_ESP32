#pragma once

#include <stdint.h>

// ============================================================================
//  Marcadores OTA em NVS (Preferences)
//
//  Dois marcadores independentes, cada um consumido UMA vez:
//
//    request - o /ota pediu a verificacao. E consumido no boot ANTES da
//              tentativa, o que impede ciclo de reinicializacoes quando nao ha
//              internet, o GitHub esta fora, a API responde mal, o download
//              quebra ou a imagem e recusada.
//    applied - a imagem nova foi gravada e o dispositivo vai reiniciar. E
//              consumido na classificacao do boot, para o aviso do Telegram
//              dizer "Atualizacao OTA" apenas nesse boot.
//
//  Fica em NVS e nao no LittleFS de proposito: o marcador precisa sobreviver a
//  troca da tabela de particoes e nao pode depender de um filesystem que pode
//  estar ausente ou corrompido. A gravacao ocorre somente no /ota e apos uma
//  instalacao bem-sucedida (2 escritas por ciclo).
// ============================================================================
namespace ota_marker {

// Grava request=1. false quando a NVS nao esta disponivel.
bool setRequest();

// Le e apaga request. true somente se estava marcado.
bool consumeRequest();

// Apaga request sem efeito colateral (usado quando a confirmacao do /ota nao
// pode ser enviada e o dispositivo NAO reinicia).
void clearRequest();

// Grava applied=1 (chamado imediatamente antes do reboot pos-instalacao).
bool setApplied();

// Le e apaga applied. true somente no primeiro boot apos a instalacao.
bool consumeApplied();

}  // namespace ota_marker
