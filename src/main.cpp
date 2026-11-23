// ============================================================================
//  MoniRes_ESP32 - Etapa 1: esqueleto compilavel
//
//  Este arquivo NAO e o firmware final: e o contrato de compilacao da Etapa 1.
//  Ele inclui todos os modulos puros copiados do projeto original e executa
//  apenas verificacoes de sanidade no boot. A orquestracao real (sensor,
//  display, Wi-Fi, historico, MQTT, Telegram, alertas e supervisao) entra nas
//  etapas seguintes, sempre preservando formulas, temporizacoes e formatos.
//
//  Ainda NAO existe include/config.h neste projeto: ele sera portado na
//  Etapa 3, junto com pinos, credenciais e a unica chave TESTE/PRODUCAO.
// ============================================================================
#include <Arduino.h>

#include "alerts.h"
#include "boot_reason.h"
#include "history_record.h"
#include "moving_average.h"
#include "publish_policy.h"
#include "queue_record.h"
#include "rate.h"
#include "telegram_text.h"
#include "uptime.h"
#include "volume.h"

namespace {

constexpr char kProjectName[] = "MoniRes_ESP32";
constexpr char kStageName[] = "Etapa 1 - esqueleto";

void printContract() {
  Serial.println();
  Serial.print("=== ");
  Serial.print(kProjectName);
  Serial.println(" ===");
  Serial.print("Estagio: ");
  Serial.println(kStageName);
  Serial.println("Modo: esqueleto (nenhum hardware acionado)");

  // Filtro: janela de 50 amostras (mesma do firmware original).
  MovingAverage filter;
  for (int i = 0; i < 50; ++i) {
    filter.add(512.0f);
  }
  Serial.printf("Filtro: janela=%u amostras, media=%.2f\n",
                static_cast<unsigned>(filter.sampleCount()), filter.average());

  // Cadeia de calibracao preservada de include/volume.h e src/volume.cpp.
  const volume::Measurement measurement = volume::compute(341);
  Serial.printf("Volume(ADC 341): tensao=%.4f V, sensor=%.2f mm, "
                "nivel=%.2f mm, volume=%.2f L\n",
                measurement.voltageV, measurement.sensorMm,
                measurement.levelMm, measurement.liters);
  Serial.printf("Referencia de 100%%: %.2f L\n",
                alerts::maxReferenceLiters());

  // Formato persistido da fila e do historico (sem structs com sizeof).
  Serial.printf("Segmento da fila: %u B (limite de 4096 B)\n",
                static_cast<unsigned>(queue_record::SEGMENT_MAX_SIZE));
  Serial.printf("Tabela do historico: %u B (%u slots)\n",
                static_cast<unsigned>(history_record::TABLE_SIZE),
                static_cast<unsigned>(history_record::SLOTS));

  // Politica de publicacao: primeira leitura valida sempre publica.
  publish_policy::Input policy{};
  policy.sampleValid = true;
  policy.volumeL = measurement.liters;
  policy.deltaLiters = 50.0f;
  policy.heartbeatMs = 3600000UL;
  policy.minIntervalMs = 10000UL;
  Serial.printf("Politica de publicacao (primeira leitura): %s\n",
                publish_policy::reasonName(publish_policy::decide(policy)));

  // Uptime imune ao rollover de millis().
  UptimeCounter uptime;
  uptime.begin(1000);
  uptime.update(4000);
  char uptimeText[32];
  formatUptime(uptime.seconds(), uptimeText, sizeof(uptimeText));
  Serial.printf("Uptime acumulado: %lu s (%s)\n",
                static_cast<unsigned long>(uptime.seconds()), uptimeText);

  // Taxa recente: sem base ate a janela encher.
  rate::Ring ring;
  rate::begin(ring, 0);
  rate::update(ring, rate::RATE_SAMPLE_INTERVAL_MS, measurement.liters, true);
  Serial.printf("Taxa recente: base=%s\n", rate::hasRate(ring) ? "sim" : "nao");

  // Limites de alerta: configuracao inicial valida (60/30/5).
  alerts::Config alertConfig;
  alertConfig.riskPct = 60;
  alertConfig.criticalPct = 30;
  alertConfig.histPct = 5;
  Serial.printf("Limites de alerta 60/30/5: %s\n",
                alerts::validate(alertConfig) == alerts::ConfigError::OK
                    ? "validos"
                    : "INVALIDOS");

  // Tabela unica de motivo do boot (classificador puro; a ligacao com
  // esp_reset_reason() entra na etapa de diagnosticos).
  Serial.printf("Motivo de boot (Power On): %s\n",
                bootReasonDescription(BOOT_REASON_POWER_ON));

  Serial.println("Contrato dos modulos puros: OK");
  Serial.println("Aguardando Etapa 2 (PlatformIO validado).");
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(100);
  printContract();
}

void loop() {
  // Esqueleto: nao executa aquisicao, rede nem persistencia.
  delay(1000);
}
