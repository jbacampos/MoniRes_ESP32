#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

// ============================================================================
//  Comparacao de versoes MAJOR.MINOR.PATCH - camada pura
//
//  Nao conhece Arduino, rede nem flash: recebe duas strings e responde. Usada
//  pelo modulo OTA para decidir se a tag publicada no GitHub e posterior a
//  versao instalada (FIRMWARE_VERSION, fonte unica em include/config.h).
//
//  Regras aceitas (tag do GitHub e versao do firmware passam pelo MESMO parser):
//    - prefixo opcional 'v' ou 'V' ("v2.1.0" e "2.1.0");
//    - exatamente tres grupos numericos separados por ponto;
//    - 1 a 5 digitos por grupo, valor <= 65535;
//    - espacos/tabs/quebras nas pontas sao ignorados.
//
//  Qualquer outra forma ("2.1", "2.1.0.1", "v2.1.0-beta", "2.1.x", "v") e
//  invalida e NUNCA cai para comparacao textual.
// ============================================================================
namespace ota_version {

struct Semver {
  uint16_t major = 0;
  uint16_t minor = 0;
  uint16_t patch = 0;
};

inline bool semverIsSpace(char c) {
  return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

// Le um grupo numerico. `cursor` avanca ate o primeiro caractere que nao for
// digito. Falha em grupo vazio, com mais de 5 digitos ou acima de 65535.
inline bool readGroup(const char *&cursor, uint16_t &value) {
  if (cursor == nullptr || *cursor < '0' || *cursor > '9') {
    return false;
  }
  uint32_t accumulated = 0;
  uint8_t digits = 0;
  while (*cursor >= '0' && *cursor <= '9') {
    accumulated = accumulated * 10u + static_cast<uint32_t>(*cursor - '0');
    if (++digits > 5 || accumulated > 65535u) {
      return false;
    }
    ++cursor;
  }
  value = static_cast<uint16_t>(accumulated);
  return true;
}

inline bool parse(const char *text, Semver &out) {
  if (text == nullptr) {
    return false;
  }

  const char *begin = text;
  while (*begin != '\0' && semverIsSpace(*begin)) {
    ++begin;
  }
  const char *end = begin + strlen(begin);
  while (end > begin && semverIsSpace(end[-1])) {
    --end;
  }
  if (end == begin) {
    return false;
  }

  const char *cursor = begin;
  if (*cursor == 'v' || *cursor == 'V') {
    ++cursor;
  }

  Semver parsed;
  if (!readGroup(cursor, parsed.major) || *cursor != '.') {
    return false;
  }
  ++cursor;
  if (!readGroup(cursor, parsed.minor) || *cursor != '.') {
    return false;
  }
  ++cursor;
  if (!readGroup(cursor, parsed.patch)) {
    return false;
  }
  if (cursor != end) {
    return false;  // sufixo ("-rc1", "+build"), espaco interno, lixo
  }

  out = parsed;
  return true;
}

inline int compare(const Semver &a, const Semver &b) {
  if (a.major != b.major) {
    return (a.major < b.major) ? -1 : 1;
  }
  if (a.minor != b.minor) {
    return (a.minor < b.minor) ? -1 : 1;
  }
  if (a.patch != b.patch) {
    return (a.patch < b.patch) ? -1 : 1;
  }
  return 0;
}

// true somente quando `published` e ESTRITAMENTE posterior a `installed`.
// Qualquer versao invalida devolve false: nada e instalado.
inline bool isNewer(const char *published, const char *installed) {
  Semver candidate;
  Semver current;
  if (!parse(published, candidate) || !parse(installed, current)) {
    return false;
  }
  return compare(candidate, current) > 0;
}

}  // namespace ota_version
