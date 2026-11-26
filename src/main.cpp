// ============================================================================
//  MoniRes_ESP32 - Etapa 7: integração Wi-Fi / NTP
//
//  Este arquivo substitui o esqueleto da Etapa 1. Nesta etapa, so sao
//  orquestrados:
//   - leitura do sensor QDY30A via ADC (GPIO34, contagem 10 bits, D2);
//   - filtro de media movel (50 amostras);
//   - display TM1637 (CLK=GPIO22, DIO=GPIO23);
//   - botao de religamento do display (GPIO16, ativo em HIGH);
//   - temporizacoes preservadas (250 ms, 15 s, 28 s, 30 ms);
//   - Wi-Fi e sincronizacao NTP.
//
//  Nao ha Wi-Fi, MQTT, Telegram, LittleFS nem historico: esses modulos
//  entram nas etapas seguintes.
// ============================================================================

#include <Arduino.h>
#include <WiFiClientSecure.h>
#include <esp_ota_ops.h>

#include "config.h"
#include "credentials.h"
#include "display.h"
#include "moving_average.h"
#include "sensor.h"
#include "simulator.h"
#include "tb_ca_cert.h"
#include "uptime.h"
#include "volume.h"
#include "wifi_manager.h"
#include "clock.h"
#include "thingsboard_manager.h"
#include "history.h"
#include "alert_monitor.h"
#include "rate.h"
#include "telegram.h"
#include "github_ota.h"
#include "ota_marker.h"

namespace {

constexpr char kProjectName[] = "MoniRes_ESP32";
constexpr char kStageName[] = "Etapa 7 - Wi-Fi / NTP";

MovingAverage g_volumeFilter(MOVING_AVERAGE_WINDOW);
DisplayManager g_display;
UptimeCounter g_uptime;
WiFiManager g_wifi;
ThingsBoardManager g_thingsBoard;
History g_history;
AlertMonitor g_alerts;
Telegram g_telegram;
rate::Ring g_rate;

volume::Measurement g_lastMeasurement;
float g_filteredLiters = 0.0f;
bool g_hasSample = false;
uint32_t g_lastSampleMs = 0;
uint32_t g_lastDiagnosticMs = 0;

void sampleAndProcess(uint32_t nowMs) {
  const uint16_t adc = sensor::read(nowMs);

  g_lastMeasurement = volume::compute(adc);
  g_volumeFilter.add(g_lastMeasurement.liters);
  g_filteredLiters = g_volumeFilter.average();

  // O display recebe o volume filtrado; a escrita fisica e temporizada.
  g_display.setLiters(g_filteredLiters);
  g_hasSample = true;

  g_lastSampleMs = nowMs;
}

void printDiagnostics(uint32_t nowMs) {
  const volume::Measurement& m = g_lastMeasurement;

  Serial.printf(
      "[%7.2f s] adc=%u | tensao=%.3f V | sensor=%.1f mm | nivel=%.1f mm | "
      "vol_inst=%.1f L | vol_filt=%.1f L",
      static_cast<double>(nowMs) / 1000.0, static_cast<unsigned>(m.adc),
      static_cast<double>(m.voltageV), static_cast<double>(m.sensorMm),
      static_cast<double>(m.levelMm), static_cast<double>(m.liters),
      static_cast<double>(g_filteredLiters));

  if constexpr (TEST_MODE) {
    Serial.printf(" | cenario=%s | filtro=%u/%u",
                  Simulator::scenarioName(SIM_SCENARIO),
                  static_cast<unsigned>(g_volumeFilter.sampleCount()),
                  static_cast<unsigned>(MOVING_AVERAGE_WINDOW));
  }

  // Informacoes de Wi-Fi e hora
  if (g_wifi.connected()) {
    Serial.print(" | wifi=ok");
    // No STA do ESP32 o campo 'canal' e' omitido para evitar dependencia.
    Serial.printf(" rssi=%d", static_cast<int>(g_wifi.rssi()));
  } else {
    Serial.print(" | wifi=off");
  }

  if (device_clock::synced()) {
    uint64_t ms = device_clock::nowMs();
    Serial.printf(" | hora=%lu.%03lu", static_cast<unsigned long>(ms / 1000),
      static_cast<unsigned long>(ms % 1000));
  } else {
    Serial.print(" | hora=off");
  }

  Serial.println();
}

void printStartup() {
  Serial.println();
  Serial.print("=== ");
  Serial.print(kProjectName);
  Serial.println(" ===");
  Serial.print("Estagio: ");
  Serial.println(kStageName);
  Serial.print("Modo: ");
  Serial.println(TEST_MODE ? "TESTE (simulador)" : "PRODUCAO (ADC GPIO34)");

  if constexpr (TEST_MODE) {
    Serial.print("Cenario: ");
    Serial.println(Simulator::scenarioName(SIM_SCENARIO));
  }

  Serial.print("Amostragem: ");
  Serial.print(SAMPLE_INTERVAL_MS);
  Serial.println(" ms");
  Serial.print("Display refresh: ");
  Serial.print(DISPLAY_REFRESH_MS);
  Serial.println(" ms");
  Serial.print("Display desliga: ");
  Serial.print(DISPLAY_ON_TIME_MS);
  Serial.println(" ms");
  Serial.print("Filtro janela: ");
  Serial.print(MOVING_AVERAGE_WINDOW);
  Serial.println(" amostras");
  Serial.print("Debounce botao: ");
  Serial.print(BUTTON_DEBOUNCE_MS);
  Serial.println(" ms");

  Serial.print("Referencia 100%: ");
  Serial.print(volume::maxMeasurableLiters(), 1);
  Serial.println(" L");

  Serial.println("Contrato de hardware: OK");
}

}  // namespace

void setup() {
  Serial.begin(SERIAL_BAUD);
  delay(100);
  printStartup();

  const uint32_t now = millis();

  // O uptime passa a ser acumulado a partir daqui.
  g_uptime.begin(now);

  sensor::begin(now);
  g_display.begin(now);

  // Wi-Fi e NTP
  g_wifi.begin(now);
  device_clock::begin(now);  // Use device_clock:: prefix

  // Diagnostico de banco: app0 e o slot gravado por USB, app1 o destino do OTA.
  // Sem esta linha nao ha como provar, pelo log, qual imagem esta em execucao
  // quando os dois binarios tem a mesma versao.
  const esp_partition_t *runningSlot = esp_ota_get_running_partition();
  const esp_partition_t *nextSlot = esp_ota_get_next_update_partition(nullptr);
  Serial.printf("OTA: slot em execucao=%s | proximo=%s\n",
                runningSlot != nullptr ? runningSlot->label : "?",
                nextSlot != nullptr ? nextSlot->label : "?");

  // B5.2 - verificacao OTA pedida pelo /ota, ANTES dos servicos de aplicacao
  // (ThingsBoard, History, AlertMonitor e Telegram). O marcador ja foi
  // consumido aqui: uma falha nao se repete no proximo boot, e o firmware
  // atual continua funcionando.
  if (ota_marker::consumeRequest()) {
    Serial.println("OTA: pedido pendente; iniciando verificacao");
    const github_ota::Result otaResult = github_ota::attempt(now, g_wifi);
    // attempt() so retorna quando NAO instalou: em sucesso ele reinicia a placa.
    Serial.printf("OTA: FALHOU (%s); seguindo boot normal\n",
                  github_ota::resultText(otaResult));
    // O motivo vai no aviso de boot: sem isso a tentativa frustrada chegaria ao
    // operador como um reinicio comum, sem nenhuma explicacao.
    // O motivo vai no aviso de boot (texto acentuado, para o Telegram); o log
    // serial continua com a versao ASCII.
    g_telegram.setOtaFailure(github_ota::resultTextTelegram(otaResult));
  } else {
    Serial.println("OTA: sem pedido pendente");
  }

  // A tentativa OTA pode durar dezenas de segundos: os servicos recebem o
  // instante ATUAL, nao o do inicio do setup.
  const uint32_t servicesNow = millis();
  g_thingsBoard.begin(servicesNow);
  g_history.begin(servicesNow);
  g_alerts.begin(servicesNow);
  rate::begin(g_rate, servicesNow);
  g_telegram.begin(servicesNow);

  g_lastSampleMs = servicesNow;

  // Primeira amostra e primeiro diagnostico imediatos.
  sampleAndProcess(servicesNow);
  printDiagnostics(servicesNow);
  g_lastDiagnosticMs = servicesNow;
}

void loop() {
  const uint32_t now = millis();

  // Acumula o uptime em toda iteracao (wrap-safe por millis()).
  g_uptime.update(now);

  g_wifi.update(now);
  device_clock::update(now, g_wifi.connected());

  // Aquisicao do sensor a cada SAMPLE_INTERVAL_MS.
  if ((now - g_lastSampleMs) >= SAMPLE_INTERVAL_MS) {
    sampleAndProcess(now);
  }

  // Refresh, desligamento e botao do display (temporizacao interna propria).
  g_display.update(now);

  // Taxa recente baseada no mesmo volume filtrado usado pelo display, MQTT,
  // historico, alertas e Telegram.
  rate::update(g_rate, now, g_filteredLiters, g_hasSample);

  TelemetrySample sample;
  sample.volumeL = g_filteredLiters;
  sample.timestampMs = device_clock::synced() ? device_clock::nowMs() : 0;
  sample.uptimeS = g_uptime.seconds();
  sample.valid = g_hasSample;

  // O historico recebe as amostras antes de qualquer relatorio Telegram.
  g_history.update(now, device_clock::synced(), sample.timestampMs,
                   g_filteredLiters, g_hasSample);

  // O monitor processa o volume antes de Telegram consultar eventos pendentes.
  g_alerts.update(now, g_filteredLiters, g_hasSample);

  g_thingsBoard.update(now, sample, g_wifi.connected());

  TelegramContext telegramContext;
  telegramContext.volumeL = g_filteredLiters;
  telegramContext.uptimeS = sample.uptimeS;
  telegramContext.sampleValid = g_hasSample;
  telegramContext.rateLpm = rate::litersPerMinute(g_rate);
  telegramContext.hasRate = rate::hasRate(g_rate);
  telegramContext.history = &g_history;
  telegramContext.alerts = &g_alerts;
  g_telegram.update(now, telegramContext);

  // Diagnostico em frequencia propria.
  if ((now - g_lastDiagnosticMs) >= DIAGNOSTIC_INTERVAL_MS) {
    g_lastDiagnosticMs = now;
    printDiagnostics(now);
  }
}