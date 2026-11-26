#pragma once

// ============================================================================
//  MoniRes_ESP32 - configuracao do firmware
//
//  Ponto central de configuracao. Nao existe outro lugar no projeto que
//  escolha entre teste e producao (ver TEST_MODE abaixo).
//
//  Projeto ESP32 DevKit V1 30 pinos (ESP32-WROOM-32, CH340):
//    QDY30A analógico  -> GPIO34  (entrada pura, sem pull)
//    TM1637 CLK        -> GPIO22
//    TM1637 DIO        -> GPIO23
//    Botão (ativo ALTO)-> GPIO16
//
//  No ESP32, `A0` NÃO é o GPIO34 (aponta para GPIO36). Os números explícitos
//  são usados justamente para evitar essa armadilha.
// ============================================================================

#include <Arduino.h>

#include "volume.h"

// ---------------------------------------------------------------------------
//  Modo de teste - a UNICA chave de selecao de ambiente
//
//  Alterar SOMENTE este valor seleciona, em um unico ponto, quais credenciais e
//  destinos toda a comunicacao usa:
//    true  -> ambiente de TESTE (simulador de sensor + credenciais de teste)
//    false -> PRODUCAO (analogRead(GPIO34) + credenciais de producao)
//
//  Nenhum outro arquivo (nem os ambientes do platformio.ini) define modo:
//  sensor, Wi-Fi, ThingsBoard e, futuramente, Telegram e OTA derivam tudo
//  daqui, por if constexpr ou por funcoes constexpr (ver credentials.h).
// ---------------------------------------------------------------------------
constexpr bool TEST_MODE = true;  // true = test, false = production

// ---------------------------------------------------------------------------
//  Versao do firmware - definicao UNICA
//
//  Usada pelo /status do Telegram (e disponivel para OTA/telemetria no futuro).
//  Nao existe outra copia da versao no projeto.
// ---------------------------------------------------------------------------
// constexpr char FIRMWARE_VERSION[] = "1.0.0"; // Migração de ESP8266 para ESP32
// O valor TEM de ser igual ao tag da release publicada: o /ota compara o tag com
// esta string, e um binario cuja versao interna fica atras do proprio tag faz o
// dispositivo reinstalar a mesma imagem em todo /ota.
constexpr char FIRMWARE_VERSION[] = "2.1.1"; // Publicação no ThingsBoard e comandos e alertas do Telegram

// ---------------------------------------------------------------------------
//  Hardware (ESP32 DevKit V1 30 pinos / WROOM-32)
// ---------------------------------------------------------------------------
constexpr uint8_t PIN_SENSOR_ADC  = 34;   // QDY30A (entrada pura, sem pull)
constexpr uint8_t PIN_DISPLAY_CLK = 22;   // TM1637 CLK
constexpr uint8_t PIN_DISPLAY_DIO = 23;   // TM1637 DIO
constexpr uint8_t PIN_BUTTON      = 16;   // botao de religamento do display

// Logica eletrica do botao, preservada do programa original:
//     if (digitalRead(PinButton) == HIGH) { ... }
// O original nao configurava o pino (sem INPUT_PULLUP) e considerava o nivel
// ALTO como botao acionado, ou seja, o circuito externo mantem GPIO16 em LOW em
// repouso e leva o pino a 3,3 V quando o botao e pressionado.
// ---------------------------------------------------------------------------
//  Temporizacao (todo o controle e feito com millis(), sem delay())
// ---------------------------------------------------------------------------
constexpr uint32_t SAMPLE_INTERVAL_MS     = 250;    // aquisicao do sensor
constexpr uint32_t DISPLAY_REFRESH_MS     = 15000;  // atualizacao do display
constexpr uint32_t DISPLAY_ON_TIME_MS     = 28000;  // desligamento automatico
constexpr uint32_t DIAGNOSTIC_INTERVAL_MS = 5000;   // periodicidade do Serial
constexpr uint32_t BUTTON_DEBOUNCE_MS     = 30;     // debounce do botao

// A atualizacao precisa caber na janela em que o display esta ligado: um refresh
// maior que o tempo ligado deixaria o valor congelado na tela.
static_assert(DISPLAY_REFRESH_MS < DISPLAY_ON_TIME_MS,
              "o intervalo de atualizacao do display deve caber no tempo ligado");

// ---------------------------------------------------------------------------
//  Filtro e display
// ---------------------------------------------------------------------------
constexpr size_t  MOVING_AVERAGE_WINDOW = 50;  // 50 leituras (12,5 s a 250 ms)
constexpr uint32_t SERIAL_BAUD          = 115200;

// Brilho 0..7 conforme a biblioteca. O programa original usava 0 (minimo).
constexpr uint8_t DISPLAY_BRIGHTNESS = 0;
constexpr uint8_t DISPLAY_DIGITS     = 6;  // TM1637TinyDisplay6

// Mensagem exibida no boot, como no programa original (display.showString).
constexpr char DISPLAY_BOOT_TEXT[] = "Inicio";

// Formato do volume, preservado do programa original:
//   volume <  1000 L -> sem separador (ex.: "830")
//   volume >= 1000 L -> ponto no 3o digito fisico, que atua como separador de
//                       milhar (ex.: "1.234", "12.345", "123.456")
constexpr long    DISPLAY_DOT_MIN_LITERS = 1000;        // limite do separador
constexpr uint8_t DISPLAY_THOUSANDS_DOT  = 0b00100000;  // ponto apos o 3o digito
constexpr long    DISPLAY_MAX_VALUE      = 999999;      // limite dos 6 digitos

// ---------------------------------------------------------------------------
//  Botao (GPIO16, ativo em nivel ALTO)
// ---------------------------------------------------------------------------
constexpr uint8_t BUTTON_ACTIVE_LEVEL = HIGH;

// O original nao habilitava pull-up interno. Mantenha false para nao inverter
// a polaridade do circuito; habilite somente se o pino ficar flutuando e a
// leitura de repouso nao for confiavel.
constexpr bool BUTTON_USE_INTERNAL_PULLUP = false;

// ---------------------------------------------------------------------------
//  Conflitos de pino - validacao estatica
//
//  GPIO34 e entrada pura no ESP32 (sem pull, sem saida). Os pinos do TM1637 e
//  do botao sao GPIO comuns. Nenhum pino pode se repetir.
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
//  Simulador (usado somente no modo TESTE)
// ---------------------------------------------------------------------------
enum class SimScenario : uint8_t {
  CONSTANT,    // nivel fixo
  FILLING,     // enchimento linear
  DRAINING,    // esvaziamento linear
  OSCILLATING, // oscilacao senoidal
  CRITICAL,    // nivel abaixo do offset do sensor (volume nulo)
  RECOVERY,    // recuperacao a partir de nivel critico
  CYCLE,       // esvazia, enche e recomeca (exercita os QUATRO alertas)
};

// Cenario ativo. Troque aqui para testar os demais comportamentos.
constexpr SimScenario SIM_SCENARIO = SimScenario::CYCLE;

constexpr bool  SIM_NOISE     = true;   // ruido artificial nas amostras ADC
constexpr float SIM_TIME_SCALE = 0.10f; // 0.1x: rampas de 1200 s

constexpr uint32_t SIM_SCENARIO_DURATION_MS = 120000; // rampas/recuperacao
constexpr float    SIM_NOISE_AMPLITUDE_COUNTS = 3.0f; // +/- 3 contagens ADC

constexpr float SIM_CONSTANT_LEVEL_MM      = 1500.0f;
constexpr float SIM_FILLING_START_MM       = 300.0f;
constexpr float SIM_FILLING_END_MM         = 2500.0f;
constexpr float SIM_DRAINING_START_MM      = 2500.0f;
constexpr float SIM_DRAINING_END_MM        = 200.0f;
constexpr float SIM_OSCILLATING_CENTER_MM  = 1500.0f;
constexpr float SIM_OSCILLATING_AMPLITUDE_MM = 400.0f;
constexpr float SIM_OSCILLATING_PERIOD_MS    = 60000.0f;
// Nivel igual a zero: a leitura fica abaixo do offset de 50 mm e a calibracao
// deve limitar nivel e volume a zero (exercita o clamp de seguranca).
constexpr float SIM_CRITICAL_LEVEL_MM        = 0.0f;
constexpr float SIM_RECOVERY_START_MM      = 200.0f;
constexpr float SIM_RECOVERY_END_MM        = 2200.0f;

// CYCLE: esvazia de SIM_DRAINING_START_MM ate SIM_DRAINING_END_MM em
// SIM_CYCLE_DRAIN_MS, enche de volta em SIM_CYCLE_FILL_MS e recomeca, para
// sempre. Com os limites de fabrica (atencao 60%, critico 30%, histerese 5%)
// um ciclo de 240 s percorre, nesta ordem: atingiu a atencao (~41 s), atingiu o
// critico (~84 s), recuperou-se do critico (~163 s) e recuperou-se da atencao
// (~206 s) - os quatro avisos, mais as histereses de um lado so. O
// OSCILLATING, em contraste, oscila em volta de 1500 mm e so cruza a atencao.
constexpr uint32_t SIM_CYCLE_DRAIN_MS = 120000UL;
constexpr uint32_t SIM_CYCLE_FILL_MS  = 120000UL;

static_assert(SIM_CYCLE_DRAIN_MS > 0 && SIM_CYCLE_FILL_MS > 0,
              "as duas pernas do CYCLE precisam ter duracao");
static_assert(SIM_DRAINING_END_MM < SIM_DRAINING_START_MM,
              "o CYCLE espera uma rampa de descida e a mesma de volta");

static_assert(SIM_TIME_SCALE > 0.0f, "SIM_TIME_SCALE deve ser maior que zero");
static_assert(SAMPLE_INTERVAL_MS > 0, "SAMPLE_INTERVAL_MS deve ser maior que zero");

//
static_assert(PIN_SENSOR_ADC != PIN_DISPLAY_CLK,
              "o pino do ADC nao pode coincidir com o CLK do display");
static_assert(PIN_SENSOR_ADC != PIN_DISPLAY_DIO,
              "o pino do ADC nao pode coincidir com o DIO do display");
static_assert(PIN_SENSOR_ADC != PIN_BUTTON,
              "o pino do ADC nao pode coincidir com o botao");
static_assert(PIN_DISPLAY_CLK != PIN_DISPLAY_DIO,
              "CLK e DIO do TM1637 nao podem ser o mesmo pino");
static_assert(PIN_DISPLAY_CLK != PIN_BUTTON,
              "CLK do TM1637 nao pode coincidir com o botao");
static_assert(PIN_DISPLAY_DIO != PIN_BUTTON,
              "DIO do TM1637 nao pode coincidir com o botao");
// ---------------------------------------------------------------------------
//  Comunicacao (Wi-Fi + ThingsBoard)
//
//  Os parametros abaixo valem para os dois ambientes. O que muda entre TESTE e
//  PRODUCAO sao apenas as credenciais e o destino (credentials.h), sempre
//  derivados de TEST_MODE.
//
//  NOTA ESP32: esta secao manteve as temporizacoes do programa original. Os
//  valores de TLS/heap (que no ESP8266 dependiam do BearSSL e de medicao de
//  campo) serao recalibrados na Etapa 5/11 com mediçoes reais no ESP32.
// ---------------------------------------------------------------------------

// Identificacao usada no clientId do MQTT (facilita identificar a sessao no
// ThingsBoard e nos logs do broker).
constexpr char DEVICE_ID[] = "MonRes-01";

// Wi-Fi: intervalo entre tentativas de reconexao (nao bloqueante).
constexpr uint32_t WIFI_RETRY_MS = 10000;

// MQTT: intervalo entre tentativas de conexao e timeouts. Os timeouts limitam
// quanto tempo UMA tentativa pode segurar o loop:
//   - MQTT_SOCKET_TIMEOUT_MS: DNS + conexao TCP (WiFiClient);
//   - MQTT_CONNACK_TIMEOUT_S: espera do CONNACK (PubSubClient, em segundos).
// Com os valores abaixo, uma tentativa contra um broker fora do ar custa no
// maximo ~1,8 s, e nao a cada ciclo: falhas seguidas aplicam backoff crescente
// ate MQTT_MAX_RECONNECT_MS.
constexpr uint32_t MQTT_RECONNECT_MS      = 5000;
constexpr uint32_t MQTT_MAX_RECONNECT_MS  = 60000;
constexpr uint32_t MQTT_SOCKET_TIMEOUT_MS = 800;
constexpr uint16_t MQTT_CONNACK_TIMEOUT_S = 1;
// ---------------------------------------------------------------------------
//  Relogio (NTP)
//
//  A fila offline guarda o timestamp ORIGINAL da medicao, entao a hora precisa
//  estar sincronizada antes de publicar ou enfileirar. Sem sincronismo a
//  telemetria apenas aguarda: nada e gravado com timestamp invalido.
// ---------------------------------------------------------------------------
constexpr char NTP_SERVER_1[] = "time.cloudflare.com";
constexpr char NTP_SERVER_2[] = "time.google.com";
constexpr char NTP_SERVER_3[] = "pool.ntp.org";

// Deslocamento usado apenas nos logs (horario de Brasilia). O epoch gravado na
// fila e sempre UTC.
constexpr int32_t UTC_OFFSET_SECONDS = -3 * 3600;

// Qualquer data a partir de 2024-01-01 indica que o NTP ja ajustou o relogio.
constexpr uint32_t NTP_MIN_VALID_EPOCH_SECONDS = 1704067200UL;

// ---------------------------------------------------------------------------
//  ThingsBoard - topicos e chaves
// ---------------------------------------------------------------------------

// Topico de telemetria do ThingsBoard (autenticacao por device access token).
// A telemetria leva apenas o timestamp original (`ts`) e o volume.
constexpr char TB_TELEMETRY_TOPIC[] = "v1/devices/me/telemetry";

// Atributos do dispositivo: `uptime_s` fica separado da telemetria e nunca
// entra na fila offline. E atualizado a cada publicacao de `volume_l` e na
// reconexao, sempre com o uptime atual - sem temporizador proprio.
constexpr char TB_ATTRIBUTES_TOPIC[] = "v1/devices/me/attributes";

// Chaves publicadas.
constexpr char TB_KEY_TS[]     = "ts";
constexpr char TB_KEY_VOLUME[] = "volume_l";
constexpr char TB_KEY_UPTIME[] = "uptime_s";

// Replay da fila offline: orcamento por iteracao do loop, igual ao Ecowitt2TB
// (mantem o loop responsivo e o display sem travamentos).
constexpr uint8_t  TB_QUEUE_REPLAY_MAX_RECORDS = 5;
constexpr uint32_t TB_QUEUE_REPLAY_BUDGET_MS   = 20;
// ---------------------------------------------------------------------------
//  Historico diario (recurso auxiliar)
//
//  O volume e ESTIMADO, nao medido: perder um dia ocasionalmente e aceitavel.
//  O historico nunca bloqueia sensor, display, MQTT, fila offline ou Telegram,
//  nunca formata o LittleFS e nao usa fila de recuperacao.
//
//  Retencao e layout binario ficam em history_record.h (30 slots, registro de
//  52 B, tabela de 1600 B); aqui ficam apenas temporizacao e limiares.
// ---------------------------------------------------------------------------
constexpr uint32_t HIST_SAMPLE_INTERVAL_MS   = 10000;   // amostragem nominal do historico
constexpr uint32_t HIST_SNAPSHOT_INTERVAL_MS = 300000;  // snapshot do dia corrente (5 min)
constexpr uint32_t HIST_RETRY_INTERVAL_MS    = 60000;   // nova tentativa de gravar o dia
constexpr uint32_t HIST_GAP_SECONDS          = 20;      // 2 acrescimos sem amostra

// Limiar de classificacao COMPLETO: amostragem e cobertura temporal do dia.
constexpr float HIST_COMPLETE_RATIO = 0.995f;

// Espaco livre minimo para iniciar uma gravacao, em blocos do LittleFS. E uma
// heuristica (nao uma garantia): cobre o bloco de dados + commit dos metadados
// e preserva a margem que a fila offline exige. No ESP32 o blockSize do
// esp_littlefs e 4096 B; manter este valor coerente com o que o VFS reporta.
constexpr uint8_t HIST_MIN_FREE_BLOCKS = 8;

static_assert(HIST_SAMPLE_INTERVAL_MS % 1000UL == 0,
              "HIST_SAMPLE_INTERVAL_MS deve ser multiplo de 1 s");
static_assert(HIST_GAP_SECONDS == 2 * (HIST_SAMPLE_INTERVAL_MS / 1000UL),
              "HIST_GAP_SECONDS deve ser o dobro do intervalo de amostragem");
static_assert(HIST_SNAPSHOT_INTERVAL_MS > HIST_SAMPLE_INTERVAL_MS,
              "o snapshot deve ser mais lento que a amostragem");
static_assert(HIST_COMPLETE_RATIO > 0.0f && HIST_COMPLETE_RATIO <= 1.0f,
              "HIST_COMPLETE_RATIO deve estar em (0, 1]");
static_assert(HIST_MIN_FREE_BLOCKS > 0, "HIST_MIN_FREE_BLOCKS deve ser positivo");


// Politica de publicacao:
//  - diferenca minima, em litros, para publicar por mudanca significativa;
//  - heartbeat periodico (1 hora), mesmo sem mudanca;
//  - intervalo minimo entre publicacoes por mudanca, para nao inundar o broker
//    enquanto o filtro esta subindo ou descendo rapido.
constexpr float    TB_PUBLISH_DELTA_LITERS    = 50.0f;
constexpr uint32_t TB_HEARTBEAT_INTERVAL_MS   = 60UL * 60UL * 1000UL;
constexpr uint32_t TB_MIN_PUBLISH_INTERVAL_MS = 10000;

static_assert(TB_PUBLISH_DELTA_LITERS > 0.0f,
              "TB_PUBLISH_DELTA_LITERS deve ser maior que zero");
static_assert(TB_HEARTBEAT_INTERVAL_MS > TB_MIN_PUBLISH_INTERVAL_MS,
              "o heartbeat deve ser maior que o intervalo minimo de publicacao");
static_assert(MQTT_SOCKET_TIMEOUT_MS < WIFI_RETRY_MS,
              "o timeout do socket deve ser menor que o intervalo de reconexao");

constexpr uint16_t MQTT_BUFFER_SIZE       = 256;

// ---------------------------------------------------------------------------
//  Telegram (Bot API, HTTPS direto no ESP32, sem servico intermediario)
//
//  O modulo consulta getUpdates a cada TELEGRAM_POLL_INTERVAL_MS e responde
//  apenas aos chats autorizados (include/secrets.h). Nada aqui e bloqueante por
//  mais que TELEGRAM_RESPONSE_TIMEOUT_MS.
//
//  MoniRes_ESP32 (D5): buffers TLS BearSSL, pisos de heap medidos no ESP8266
//  (TELEGRAM_MIN_FREE_HEAP/BLOCK, TELEGRAM_SESSION_BYTES/HOLE, pipos BearSSL)
//  e heuristica de sessao persistente do BearSSL NAO foram portados. Os valores
//  abaixo sao apenas intervalos/timeouts/porta e limites logicos pertinentes a
//  ESP32; o transporte TLS sera via mbedTLS (WiFiClientSecure, setCACert) e os
//  pisos de heap serao recalibrados com medicoes reais no ESP32.
//
//  Nunca usar setInsecure(): CA + hostname/SNI sempre validados.
// ---------------------------------------------------------------------------
constexpr char TELEGRAM_HOST[]    = "api.telegram.org";
constexpr uint16_t TELEGRAM_PORT  = 443;

// Intervalo entre consultas (contado do INICIO da consulta anterior, o que
// garante que nunca existam duas consultas simultaneas).
constexpr uint32_t TELEGRAM_POLL_INTERVAL_MS = 20000UL;

// Atraso da PRIMEIRA consulta depois do boot (as seguintes respeitam
// TELEGRAM_POLL_INTERVAL_MS). A consulta so sai quando Wi-Fi E relogio
// (NTP) estiverem prontos - ver o guarda de relogio em Telegram::update().
constexpr uint32_t TELEGRAM_FIRST_POLL_DELAY_MS = 1000UL;
static_assert(TELEGRAM_FIRST_POLL_DELAY_MS < TELEGRAM_POLL_INTERVAL_MS,
              "a primeira consulta precisa vir antes do intervalo normal");

// Limites de tempo de UMA consulta, incluindo DNS, handshake TLS e leitura da
// resposta HTTP. O nome e resolvido ANTES com deadline de 1.500 ms (D7,
// resolvedor lwIP com deadline) e o resultado fica no cache do lwIP.
constexpr uint16_t TELEGRAM_DNS_TIMEOUT_MS       = 1500;
constexpr uint16_t TELEGRAM_SOCKET_TIMEOUT_MS    = 2500;
constexpr uint32_t TELEGRAM_RESPONSE_TIMEOUT_MS = 3000UL;

// Corpo HTTP maximo aceito. getUpdates com limit=1 devolve ~700 B (mensagem de
// texto) e sendMessage ~400 B; 2048 B e o teto que o leitor nunca ultrapassa
// (resposta maior e descartada como falha, sem truncar JSON).
constexpr size_t TELEGRAM_BODY_MAX = 2048;

// DIAGNOSTICO - periodo minimo entre dois avisos de heap insuficiente.
// O adiamento NAO consome o intervalo de polling: o gate continua sendo
// reavaliado a cada loop(). Apenas a MENSAGEM e limitada, para o log nao
// repetir a mesma linha a cada iteracao do loop.
//
// NOTA (D5): os pisos de heap do gate (TELEGRAM_MIN_FREE_HEAP etc.) que
// acompanhavam este aviso no ESP8266 serao redefinidos na Etapa 12 apos
// medicao real no ESP32. Aqui fica apenas o periodo do aviso.
constexpr uint32_t TELEGRAM_HEAP_WARN_PERIOD_MS = 300000UL;  // 5 min

// DIAGNOSTICO/RECUPERACAO - tempo minimo com o gate fechado antes de UMA
// tentativa de recriar a sessao com o piso de emergencia. Uma tentativa por
// minuto deixa o bot voltar a responder em ate ~1 min depois de perder a
// sessao, sem martelar a Bot API enquanto ela estiver inacessivel.
//
// NOTA (D5): o piso de emergencia (TELEGRAM_MIN_FREE_BLOCK_RECOVERY) sera
// definido na Etapa 12 apos medicao real no ESP32.
constexpr uint32_t TELEGRAM_GATE_RECOVERY_PERIOD_MS = 60000UL;  // 1 min

// Idade maxima da sessao TLS antes de RECRIA-LA DE PROPOSITO. Medido em campo
// no ESP8266 que o api.telegram.org encerra a conexao com ~1 h de idade.
// Renovar antes troca uma queda descontrolada por um fechamento previsivel.
//
// NOTA (D5/D9): a necessidade de renovar depende do servidor, nao do BearSSL.
// Mantido o valor de comportamento (50 min) para a jornada futura; o desenho
// de sessao persistente do BearSSL NAO foi portado. Medicao real no ESP32
// confirmara na Etapa 12.
constexpr uint32_t TELEGRAM_SESSION_MAX_AGE_MS = 50UL * 60UL * 1000UL;  // 50 min

static_assert(TELEGRAM_SESSION_MAX_AGE_MS >= 5UL * TELEGRAM_POLL_INTERVAL_MS,
              "renovar a sessao a cada poucos polls nao faz sentido");
static_assert(TELEGRAM_SESSION_MAX_AGE_MS < 3600000UL,
              "o servidor fecha a sessao com ~1 h: renovar antes disso");

// DIAGNOSTICO - instante da amostra de heap depois que a consulta termina
// (sessao TLS ja destruida). Da tempo para o lwIP devolver os pbufs e para o
// alocador fundir os blocos livres.
constexpr uint32_t TELEGRAM_POST_POLL_SAMPLE_MS = 5000UL;  // 5 s

// Comando recebido: texto util (o resto e ignorado).
constexpr size_t TELEGRAM_TEXT_MAX = 64;

// Aviso de boot (mensagem enviada uma vez a cada boot). Cada consulta tenta
// entrega-lo; depois deste numero de tentativas o aviso e descartado ate o
// proximo boot. Nada e persistido em flash.
constexpr uint8_t TELEGRAM_BOOT_NOTICE_MAX_ATTEMPTS = 3;

// ---------------------------------------------------------------------------
//  /reboot - offset persistido
//
//  Um /reboot confirmado precisa deixar o PROXIMO offset em flash antes de
//  reiniciar; sem isso o Telegram reenviaria o mesmo update depois do boot e o
//  comando entraria em ciclo. Dois slots A/B de 24 B, com CRC32 e geracao -
//  mesmo padrao de ALERT_FILE_A/B; o formato do registro fica em
//  src/telegram.cpp. A gravacao acontece SOMENTE no /reboot: durante o polling
//  normal nada e escrito em flash.
// ---------------------------------------------------------------------------
constexpr char TELEGRAM_OFFSET_FILE_A[] = "/t-offset-a.dat";
constexpr char TELEGRAM_OFFSET_FILE_B[] = "/t-offset-b.dat";

// Espera curta entre a confirmacao enviada e o ESP.restart(): tempo para o
// socket TLS fechar e o UART terminar de transmitir o log. NAO e espera do
// envio - sendMessage() so retorna depois de ler a resposta do Telegram.
constexpr uint32_t TELEGRAM_REBOOT_SETTLE_MS = 100UL;

static_assert(TELEGRAM_REBOOT_SETTLE_MS < TELEGRAM_RESPONSE_TIMEOUT_MS,
              "a espera antes do reboot deve ser curta");

static_assert(TELEGRAM_PORT == 443, "TELEGRAM_PORT deve ser 443 (HTTPS)");
static_assert(TELEGRAM_RESPONSE_TIMEOUT_MS < TELEGRAM_POLL_INTERVAL_MS,
              "o timeout da resposta deve caber no intervalo de polling");
static_assert(TELEGRAM_BODY_MAX <= 4096, "corpo HTTP maximo acima do razoavel");
static_assert(TELEGRAM_GATE_RECOVERY_PERIOD_MS >= TELEGRAM_POLL_INTERVAL_MS,
              "a recuperacao nao pode ser mais frequente que o polling");

// ---------------------------------------------------------------------------
//  Atualizacao remota (OTA) pelo GitHub - B5.2
//
//  Fluxo: /ota -> marcador em NVS -> reboot -> consulta da ultima release ->
//  comparacao semantica de versao -> download do asset MoniRes.bin -> tamanho +
//  SHA-256 -> Update (particao inativa) -> reboot. Qualquer falha encerra a
//  tentativa e o boot normal segue com o firmware atual, sem nova tentativa
//  automatica.
//
//  A tentativa roda ANTES de ThingsBoard, History, AlertMonitor e Telegram, e
//  por isso os tetos de tempo abaixo precisam ser finitos: o dispositivo nunca
//  fica preso no boot.
// ---------------------------------------------------------------------------
constexpr char GITHUB_OTA_RELEASE_API[] =
    "https://api.github.com/repos/jbacampos/MoniRes_ESP32/releases/latest";
constexpr char GITHUB_OTA_ASSET_NAME[] = "MoniRes.bin";

// A API do GitHub recusa requisicoes sem User-Agent identificavel.
constexpr char GITHUB_OTA_USER_AGENT[] = "MoniRes-ESP32-OTA/1.0";

// Tempos da tentativa.
constexpr uint32_t GITHUB_OTA_NETWORK_WAIT_MS = 45000UL;   // Wi-Fi + NTP
constexpr uint32_t GITHUB_OTA_HTTP_TIMEOUT_MS = 15000UL;   // uma requisicao
constexpr uint32_t GITHUB_OTA_IDLE_TIMEOUT_MS = 20000UL;   // download parado
constexpr uint32_t GITHUB_OTA_TOTAL_BUDGET_MS = 240000UL;  // tentativa inteira

// Corpo maximo aceito da API. A release do MoniRes tem ~9 KB de JSON bruto; o
// parser guarda apenas os campos filtrados (tag_name, draft, prerelease e os
// quatro campos do asset).
constexpr size_t GITHUB_OTA_API_MAX_BYTES = 32768;

// Saltos seguidos: latest -> tag -> CDN assinado (2 reais, com folga).
constexpr uint8_t GITHUB_OTA_MAX_REDIRECTS = 4;

// Sanidade do asset antes de tocar a flash (o binario atual mede ~987 KB).
constexpr uint32_t GITHUB_OTA_MIN_IMAGE_BYTES = 262144UL;  // 256 KB

static_assert(GITHUB_OTA_MAX_REDIRECTS >= 2 && GITHUB_OTA_MAX_REDIRECTS <= 8,
              "o download passa por dois redirecionamentos; nao seguir mais que 8");
static_assert(GITHUB_OTA_IDLE_TIMEOUT_MS < GITHUB_OTA_TOTAL_BUDGET_MS,
              "o timeout de inatividade deve caber no orcamento total");
static_assert(GITHUB_OTA_HTTP_TIMEOUT_MS < GITHUB_OTA_TOTAL_BUDGET_MS,
              "uma requisicao nao pode consumir o orcamento inteiro");
static_assert(GITHUB_OTA_TOTAL_BUDGET_MS <= 300000UL,
              "a tentativa OTA nao pode atrasar o boot por mais de 5 min");
static_assert(GITHUB_OTA_MIN_IMAGE_BYTES >= 131072UL,
              "imagem menor que 128 KB e recusada como corrompida");


// ---------------------------------------------------------------------------
//  Alertas de nivel (Telegram)
//
//  O firmware compara o volume FILTRADO (o mesmo do display, do MQTT e do
//  /status) com dois limiares e envia UMA mensagem por transicao:
//    volume <= lr                  -> "atingiu o nivel de atencao"
//    volume <= lc                  -> "atingiu o nivel critico"
//    subiu para >= lc + hist       -> "recuperou-se do nivel critico"
//    subiu para >= lr + hist       -> "recuperou-se do nivel de atencao"
//
//  Os limiares sao PERCENTUAIS DO MAIOR VOLUME MEDIVEL (fundo de escala do
//  sensor): 100% = volume::maxMeasurableLiters() = 116.651 L (nivel corrigido de
//  2.809,5 mm). Usa-se o teto do sensor, e nao os 3 m de agua do reservatorio,
//  porque o ADC satura em 3,0 V - ou seja, 3 m de agua NAO seriam medidos e um
//  limiar acima de 93,65% nunca disparara.
//
//  Quando o nivel maximo real for identificado em producao, basta ajustar a
//  calibracao em include/volume.h (as constantes sao a fonte unica deste numero).
// ---------------------------------------------------------------------------
constexpr uint8_t ALERT_DEFAULT_RISK_PCT     = 60;
constexpr uint8_t ALERT_DEFAULT_CRITICAL_PCT = 30;
constexpr uint8_t ALERT_DEFAULT_HIST_PCT     = 5;

// Tempo entre o boot e a PRIMEIRA avaliacao: da tempo de sair do ruido da
// primeira amostra e de o filtro encher (MOVING_AVERAGE_WINDOW amostras).
constexpr uint32_t ALERT_SETTLE_MS = 15000UL;

// Arquivo de configuracao/estado dos alertas: dois slots A/B (mesmo padrao do
// historico). Nada aqui formata o LittleFS: se a montagem falhar, os alertas
// funcionam em RAM com os defaults e o /w avisa que nao houve persistencia.
constexpr char ALERT_FILE_A[] = "/w-alerts-a.dat";
constexpr char ALERT_FILE_B[] = "/w-alerts-b.dat";

static_assert(volume::maxMeasurableLiters() > 116000.0f &&
                  volume::maxMeasurableLiters() < 117000.0f,
              "a referencia dos alertas deve ser o teto do sensor (116.651 L)");
static_assert(ALERT_DEFAULT_CRITICAL_PCT < ALERT_DEFAULT_RISK_PCT,
              "o limiar critico deve ser menor que o de atencao");
static_assert(ALERT_DEFAULT_RISK_PCT + ALERT_DEFAULT_HIST_PCT <= 100,
              "atencao + histerese nao pode passar de 100% (nunca recuperaria)");
static_assert(ALERT_SETTLE_MS >= MOVING_AVERAGE_WINDOW * SAMPLE_INTERVAL_MS,
              "o settle deve cobrir o filtro cheio (janela x intervalo)");

// ---------------------------------------------------------------------------
//  Guarda de bloqueio - reboot controlado
//
//  Os dois servicos externos podem ficar bloqueados SEM prazo: o Telegram quando
//  o gate de heap nao abre (fragmentacao) e o MQTT quando o broker/TLS nao aceita
//  a sessao. O reboot devolve o heap inteiro e refaz as sessoes TLS do zero.
//
//  ULTIMO RECURSO: o reboot NAO e um evento periodico. Ele exige as DUAS
//  condicoes ao mesmo tempo: (1) um servico bloqueado ha TELEGRAM_STALL_REBOOT_MS
//  / MQTT_STALL_REBOOT_MS e (2) o heap fragmentado a ponto de nao dar mais para
//  recriar a sessao TLS. Um servico parado com o heap intacto NAO reinicia.
//
//  Regras: so conta com Wi-Fi conectado e fora da carencia do boot; nao conta
//  quando as credenciais estao pendentes (nao ha o que reconectar); avisa uma vez
//  ao passar de STALL_GUARD_FIRST_WARN_MS (servico) e ao persistir a fragmentacao.
//
//  Custo de um reboot: uptime volta a zero, o historico do dia perde o acumulado
//  desde o ultimo snapshot (ate HIST_SNAPSHOT_INTERVAL_MS) e a fila offline e
//  preservada (LittleFS, com CRC e reparo no boot).
//
//  MoniRes_ESP32 (D5/D9): o piso de bloco (STALL_GUARD_MAX_BLOCK_FLOOR) era
//  definido em termos de TELEGRAM_MIN_FREE_BLOCK_RECOVERY do BearSSL. Como
//  esses pisos NAO foram portados, STALL_GUARD_MAX_BLOCK_FLOOR fica
//  intencionalmente de fora desta Etapa; o modulo stall_guard so sera
//  definido apos medicao real do heap no ESP32 (Etapa 13/14). Os tempos
//  de guarda abaixo (comportamento puro) sao preservados.
// ---------------------------------------------------------------------------
constexpr uint32_t STALL_GUARD_GRACE_MS = 60000UL;  // 1 min apos o boot
constexpr uint32_t STALL_GUARD_FIRST_WARN_MS = 600000UL;  // 10 min: primeiro aviso
constexpr uint32_t TELEGRAM_STALL_REBOOT_MS = 1800000UL;  // 30 min sem consulta ok
constexpr uint32_t MQTT_STALL_REBOOT_MS = 1800000UL;      // 30 min sem sessao

// Quanto tempo o bloco tem de ficar abaixo do piso (a definir na Etapa 13/14
// apos medicao real) para o reboot ser permitido. O bloco oscila durante
// publicacao/handshake; so um estado SUSTENTADO conta.
// Valor de comportamento preservado; a comparacao com o piso real fica pendente.
constexpr uint32_t STALL_GUARD_FRAGMENTED_MS = 300000UL;  // 5 min

static_assert(STALL_GUARD_GRACE_MS >= TELEGRAM_POLL_INTERVAL_MS,
              "a carencia deve cobrir pelo menos um intervalo de polling");
static_assert(STALL_GUARD_FIRST_WARN_MS >= STALL_GUARD_GRACE_MS,
              "o primeiro aviso nao pode ser anterior a carencia");
static_assert(TELEGRAM_STALL_REBOOT_MS > STALL_GUARD_FIRST_WARN_MS,
              "o reboot deve vir depois do aviso");
static_assert(TELEGRAM_STALL_REBOOT_MS > TELEGRAM_GATE_RECOVERY_PERIOD_MS,
              "a recuperacao do gate deve ter chance antes do reboot");
static_assert(MQTT_STALL_REBOOT_MS > MQTT_MAX_RECONNECT_MS,
              "o backoff do MQTT deve ter chance antes do reboot");
static_assert(TELEGRAM_STALL_REBOOT_MS <= 7200000UL && MQTT_STALL_REBOOT_MS <= 7200000UL,
              "bloqueio de mais de 2 h nao deve esperar o reboot");
// Exige persistencia: o gate de emergencia tenta uma vez por minuto e a
// oscilacao do bloco nao pode disparar o reboot.
static_assert(STALL_GUARD_FRAGMENTED_MS >= TELEGRAM_GATE_RECOVERY_PERIOD_MS,
              "a fragmentacao precisa persistir alem da recuperacao do gate");


