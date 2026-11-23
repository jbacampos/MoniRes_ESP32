#pragma once

#include <stdint.h>

#include "config.h"

// ============================================================================
//  Aquisicao do sensor de pressao QDY30A
//
//  PRODUCAO: leitura de analogReadMilliVolts(GPIO34), convertida para a
//  contagem equivalente de 10 bits (0..1023), conforme decisao D2.
//  TESTE:    leitura do simulador; analogRead* nao e referenciado.
// ============================================================================
namespace sensor {

void begin(uint32_t nowMs);
uint16_t read(uint32_t nowMs);

}  // namespace sensor
