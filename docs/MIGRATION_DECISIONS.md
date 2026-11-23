# Decisoes da migracao MoniRes_ESP32

- **Data:** 2026-11-23
- **Base:** relatorio de auditoria do projeto `Monitora_Reserv` (ESP8266 / NodeMCU ESP-12E) apresentado e aprovado.
- **Projeto original:** `d:\João\OneDrive\Documentos\Arduino\Monitora_Reserv` (ESP8266). Permanece **absolutamente intocado**.
- **Novo projeto:** `d:\João\OneDrive\Documentos\Arduino\MoniRes_ESP32` (ESP32 DevKit V1 30 pinos, ESP32-WROOM-32, CH340), com repositorio Git independente.
- **Placa e pinagem aprovadas:** QDY30A em **GPIO34**; TM1637 **CLK=GPIO22**, **DIO=GPIO23**; botao **GPIO16** (ativo em nivel ALTO).

## Regras inviolaveis

1. Nao alterar formulas fisicas, calibracao, temporizacoes, comandos, mensagens, formatos persistidos ou comportamentos funcionais sem justificativa previa e aprovacao.
2. `TEST_MODE` existe **somente** em `include/config.h`; nenhum `-D` de modo no `platformio.ini` ou em qualquer outro arquivo.
3. O firmware **nunca** formata o LittleFS automaticamente. Em placa nova, a imagem e enviada uma vez com `pio run -t uploadfs`.
4. Nunca usar `setInsecure()` nem reduzir a validacao TLS (CA + hostname/SNI).
5. Nao criar camada de compatibilidade ESP8266/ESP32: o projeto novo e ESP32 puro.
6. O cliente HTTP/1.1 proprio do Telegram sera preservado; nao sera substituido por biblioteca de Telegram.
7. Toda etapa termina com compilacao, revisao de warnings, testes e registro de arquivos/alteracoes/pendencias.
8. O projeto original nunca e copiado por cima, editado ou reformatado.

## D1 - Local do projeto

**Decisao:** criar `MoniRes_ESP32` em diretorio **irmao** ao `Monitora_Reserv`, com repositorio Git independente.

**Consequencias:** nenhum arquivo do projeto original e tocado; o novo projeto tem `.gitignore`, `include/secrets.h` local e historico proprios. Nao ha aninhamento de projetos PlatformIO.

## D2 - ADC e calibracao

**Decisao:** usar `analogReadMilliVolts()` no GPIO34, com atenuacao inicial de **6 dB**, e converter a tensao medida para a **contagem equivalente de 10 bits** do contrato original:

```text
contagem10 = round(mV * 1023 / 3300), limitada a 0..1023
```

`include/volume.h` e `src/volume.cpp` permanecem **integralmente inalterados** (3,3 V, 1023 contagens, 1,1 V = 1000 mm, offset 50 mm, correcao 1,05, 41,52 L/mm).

**Consequencias:** a cadeia de volume recebe a mesma contagem que receberia no ESP8266 para a mesma tensao; a resolucao de 1 contagem continua ~2,93 mm (~116 L). A atenuacao definitiva (6 dB x 11 dB) e a calibracao serao validadas em bancada antes da aprovacao final.

**Validacao:** comparar mV/counts/volume com multimetro em 0,2/0,5/0,8/1,1 V e contra o firmware ESP8266 no mesmo sinal; criterio: sem offset sistematico e diferenca <= 1 contagem. No modo TESTE, os volumes e os tempos de alerta devem ser identicos aos vetores do original.

## D3 - Particionamento

**Decisao:** usar `no_ota.csv`: **2 MB** para a aplicacao e **1,875 MB** para o LittleFS.

**Consequencias:** ha espaco folgado para 128 segmentos (~406 KB), historico, alertas e o binario. OTA nao esta implementado; se virar requisito, uma tabela customizada sera avaliada (com aviso de que reparticionar pode apagar o LittleFS).

## D4 - LittleFS e truncamento

**Decisao:** usar `ftruncate()` POSIX via VFS `/littlefs` para remover cauda parcial de segmento, mantendo o plano B de **arquivo temporario + verificacao + rename** se a bancada reprovar.

**Consequencias:** a operacao de truncamento preserva a semantica de recuperacao do original (nao e "apagar e recriar sem verificar"). Antes de concluir a etapa de persistencia: testes de integridade, truncamento de cauda parcial e interrupcao de energia. O firmware nunca chama `format()`; `LittleFS.begin(false, ...)` e obrigatorio.

## D5 - TLS e heap

**Decisao:** migrar de BearSSL para **mbedTLS** (`WiFiClientSecure`), removendo mecanismos especificos do BearSSL (`setBufferSizes`, `setTrustAnchors`, `setX509Time`, `getLastSSLError`, `BR_ERR_TOO_LARGE`), mas preservando a logica de protecao, os gates e o diagnostico de falhas.

**Consequencias:** CA validada por `setCACert()` em cada nova sessao; hostname/SNI e `VERIFY_REQUIRED` mantidos; **nunca** `setInsecure()`. Todos os limites de heap (`TELEGRAM_*`, `STALL_GUARD_MAX_BLOCK_FLOOR`) serao recalibrados com medicoes reais no ESP32, nao copiados do ESP8266. Buffers TLS passam a ser os fixos do core (16 KB + 16 KB); a capacidade sera comprovada com as cadeias reais.

## D6 - Timeout do MQTT

**Decisao:** aceitar condicionalmente a mudanca de orcamento da tentativa MQTT (TCP 1 s + handshake 2 s + CONNACK 1 s, ~3-4 s no pior caso).

**Consequencias:** a etapa MQTT **nao sera considerada concluida** sem medir o bloqueio efetivo do `loop()` e demonstrar que a aquisicao de 250 ms, o filtro de 50 amostras, o display e as temporizacoes de historico/alertas nao foram comprometidos. Se houver regressao, o desenho sera revisto antes de seguir.

## D7 - DNS

**Decisao:** usar resolvedor **lwIP com deadline de 1.500 ms**, preservando hostname, SNI e validacao TLS.

**Consequencias:** nao depender de `WiFi.hostByName` sem timeout nem do DNS interno do `connect()`. A conexao usa o IP resolvido, mas o hostname continua sendo passado ao TLS para SNI/validacao. O resolvedor sera um helper pequeno, testado com host valido, invalido e timeout.

## D8 - Motivo do boot (brownout)

**Decisao:** acrescentar o codigo **10** com o texto **"Queda de tensao"**, preservando os codigos 1..9 e os textos existentes.

**Consequencias:** `ESP_RST_BROWNOUT` passa a ser distinguivel de `ESP_RST_UNKNOWN`. O marcador de OTA continua tendo prioridade. A tabela de textos permanece com fonte unica em `include/boot_reason.h`.

## D9 - Watchdog e stack

**Decisao:** manter inicialmente a configuracao **padrao do core** (sem inscrever o `loopTask` no Task WDT). Medir comportamento e stack antes de decidir sobre WDT explicito.

**Consequencias:** medir `uxTaskGetStackHighWaterMark`, o pior bloqueio de DNS/TCP/TLS e o comportamento em campo. Se necessario, avaliar WDT explicito com feed nos pontos longos e/ou `-DARDUINO_LOOP_STACK_SIZE=12288`, sem alterar temporizacoes funcionais.

## D10 - Credenciais

**Decisao:** corrigir os placeholders de `include/secrets.h.example` para o prefixo **`COLOQUE_...`**, sem modificar o projeto original e mantendo `include/secrets.h` fora do Git.

**Consequencias:** o detector de "credenciais pendentes" de `credentials.h` passa a reconhecer o exemplo copiado sem edicao. Nenhum valor real sera impresso em log, documento ou teste.

## D11 - Testes de host

**Decisao:** atualizar `check_telegram.py` para verificar as APIs e os mecanismos de seguranca do **ESP32**.

**Consequencias:** as assercoes deixam de exigir `setTrustAnchors`, `setBufferSizes`, `setX509Time`, `getLastSSLError`, `getMaxFreeBlockSize` e `getResetInfoPtr`, e passam a exigir `setCACert`, ausencia de `setInsecure`, timeouts em segundos, `esp_reset_reason`, `getMaxAllocHeap`, `ftruncate` e C++17. Os espelhos numericos dos demais modulos permanecem inalterados.

## Pendencias de medicao (nao decididas por suposicao)

1. Atenuacao definitiva do ADC e curva de calibracao em bancada (D2).
2. Pisos de heap do Telegram, pico de sessao e recuperacao, medidos no ESP32 (D5).
3. Bloqueio efetivo do `loop()` no MQTT e no Telegram e efeito na aquisicao de 250 ms (D6).
4. Stack high-water do `loopTask` e decisao sobre WDT explicito (D9).
5. Confirmacao de que `ftruncate()` funciona no VFS do LittleFS instalado (D4).
6. Tabela de particoes customizada somente se OTA for requisito futuro (D3).

## Criterio de conclusao da Etapa 1

- Estrutura independente criada, sem qualquer alteracao no projeto original.
- Modulos puros copiados byte a byte (SHA-256 conferido) e compilando no ESP32.
- `platformio.ini` com `esp32dev`, `no_ota.csv`, LittleFS, C++17 e dependencias fixadas.
- Este documento e a rastreabilidade registrados em `docs/`.

