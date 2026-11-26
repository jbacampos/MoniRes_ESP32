#include "ota_marker.h"

#include <Arduino.h>
#include <Preferences.h>

// ============================================================================
//  Marcadores OTA - implementacao (NVS/Preferences)
//
//  Cada funcao abre e fecha o namespace: a NVS nao fica aberta entre chamadas
//  e nenhuma delas e chamada no caminho quente do loop.
// ============================================================================
namespace {

constexpr char kNamespace[] = "monota";
constexpr char kRequestKey[] = "req";
constexpr char kAppliedKey[] = "applied";

bool writeFlag(const char *key) {
  Preferences prefs;
  if (!prefs.begin(kNamespace, false)) {
    Serial.println("OTA: NVS indisponivel; marcador nao gravado.");
    return false;
  }
  const size_t written = prefs.putUChar(key, 1);
  prefs.end();
  return written == sizeof(uint8_t);
}

// Le a chave e, quando marcada, apaga antes de retornar: o marcador nunca e
// consumido duas vezes, mesmo que o boot seguinte seja interrompido.
bool takeFlag(const char *key) {
  Preferences prefs;
  if (!prefs.begin(kNamespace, false)) {
    return false;
  }
  const uint8_t value = prefs.getUChar(key, 0);
  if (value != 0) {
    prefs.remove(key);
  }
  prefs.end();
  return value != 0;
}

void removeFlag(const char *key) {
  Preferences prefs;
  if (!prefs.begin(kNamespace, false)) {
    return;
  }
  prefs.remove(key);
  prefs.end();
}

}  // namespace

bool ota_marker::setRequest() { return writeFlag(kRequestKey); }

bool ota_marker::consumeRequest() { return takeFlag(kRequestKey); }

void ota_marker::clearRequest() { removeFlag(kRequestKey); }

bool ota_marker::setApplied() { return writeFlag(kAppliedKey); }

bool ota_marker::consumeApplied() { return takeFlag(kAppliedKey); }
