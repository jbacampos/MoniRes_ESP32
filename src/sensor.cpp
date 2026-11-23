#include "sensor.h"

#include <Arduino.h>

#include <math.h>

#include "config.h"
#include "simulator.h"

// ============================================================================
//  Aquisicao do sensor de pressao QDY30A (ESP32, GPIO34)
//
//  Decisao D2:
//    - analogReadMilliVolts(GPIO34) com attenuacao de 6 dB no pino.
//    - contagem10 = round(mV * 1023 / 3300), limitada a 0..1023.
//  TEST_MODE == true  -> leitura do simulador; analogRead* nao e emitido.
//  TEST_MODE == false -> leitura exclusiva de analogReadMilliVolts(GPIO34).
// ============================================================================

namespace {

// Construido apenas quando usado: no modo PRODUCAO esta funcao nunca e chamada,
// entao o simulador nao ocupa RAM nem e inicializado no boot.
[[maybe_unused]] Simulator &simulator() {
  static Simulator instance;
  return instance;
}

}  // namespace

namespace sensor {

void begin(uint32_t nowMs) {
  if constexpr (TEST_MODE) {
    simulator().begin(nowMs);
  } else {
    (void)nowMs;
    // Attenuacao de 6 dB somente no pino do sensor (D2).
    analogSetPinAttenuation(PIN_SENSOR_ADC, ADC_6db);
    // Descarta a primeira conversao, que costuma ser instavel apos o boot.
    (void)analogReadMilliVolts(PIN_SENSOR_ADC);
  }
}

uint16_t read(uint32_t nowMs) {
  if constexpr (TEST_MODE) {
    return simulator().sample(nowMs);
  } else {
    (void)nowMs;
    const uint32_t mV = analogReadMilliVolts(PIN_SENSOR_ADC);
    const long counts =
        static_cast<long>(lroundf(static_cast<float>(mV) * 1023.0f / 3300.0f));
    const long clamped =
        (counts < 0L) ? 0L : ((counts > 1023L) ? 1023L : counts);
    return static_cast<uint16_t>(clamped);
  }
}

}  // namespace sensor
