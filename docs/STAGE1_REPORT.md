# Relatorio da Etapa 1 - estrutura independente e PlatformIO

- **Data:** 2026-11-23
- **Escopo executado:** criar o projeto irmao `MoniRes_ESP32`, copiar os modulos puros sem alteracao, configurar o PlatformIO para ESP32 e validar a compilacao.
- **Projeto original:** `Monitora_Reserv` (ESP8266), **nao modificado**. `git status --short` vazio antes e depois da etapa.
- **Etapa do plano:** esta etapa absorve as Etapas 1 e 2 do plano de auditoria (estrutura + PlatformIO/C++17). A proxima etapa de execucao e a de configuracao/pinos/credenciais (Etapa 3 do plano).

## Arquivos criados

| Arquivo | Papel |
|---|---|
| `platformio.ini` | `esp32dev`, `no_ota.csv`, LittleFS, C++17, dependencias fixadas |
| `.gitignore` | ignora `.pio`, `include/secrets.h` e artefatos |
| `.gitattributes` | `* -text`: preserva LF e os bytes das copias |
| `README.md` | estado, comandos, pinagem e restricoes |
| `docs/MIGRATION_DECISIONS.md` | decisoes D1..D11 e pendencias de medicao |
| `docs/TRACEABILITY.md` | mapa original -> ESP32, etapas e hashes |
| `docs/STAGE1_REPORT.md` | este relatorio |
| `data/.gitkeep` | mantem a pasta exigida por `uploadfs` |
| `src/main.cpp` | esqueleto de contrato de compilacao (nao e o firmware final) |

## Arquivos copiados sem alteracao

Copias byte a byte, com SHA-256 conferido contra o original:

- `include/moving_average.h`, `src/moving_average.cpp`
- `include/volume.h`, `src/volume.cpp` (calibracao intocada)
- `include/publish_policy.h`
- `include/rate.h`
- `include/uptime.h`
- `include/queue_record.h`
- `include/history_record.h`
- `include/alerts.h`
- `include/telegram_text.h`
- `include/boot_reason.h`
- `tools/host_test/hosttest.cpp`
- `tools/host_test/check_logic.py`

Os hashes completos estao em `docs/TRACEABILITY.md`. A conferencia final retornou `ALL_COPIES_MATCH`.

## Alteracoes realizadas

1. Criacao da estrutura independente em `d:\João\OneDrive\Documentos\Arduino\MoniRes_ESP32`.
2. Copia dos modulos puros, sem edicao de conteudo.
3. `platformio.ini` com:
   - `platform = espressif32`, `board = esp32dev`, `framework = arduino`;
   - `board_build.filesystem = littlefs`;
   - `board_build.partitions = no_ota.csv`;
   - `build_unflags = -std=gnu++11` e `build_flags = -std=gnu++17 ...`;
   - TM1637 v1.12.2, PubSubClient 2.8 e ArduinoJson 6.21.5 fixados.
4. `src/main.cpp` apenas como contrato de compilacao: inclui todos os headers puros, executa verificacoes de sanidade e **nao** aciona hardware, rede, FS ou temporizacoes.
5. Documentacao das decisoes aprovadas (D1..D11) e da rastreabilidade.
6. Repositorio Git independente com dois commits iniciais (`16350e7` e `3c86142`).

## Resultado da compilacao

- PlatformIO Core 6.2.0; plataforma `espressif32` 7.0.1; Arduino core 2.0.17; IDF 4.4.7.
- Comando: `pio run -d "d:\João\OneDrive\Documentos\Arduino\MoniRes_ESP32"`.
- Resultado: **SUCCESS**, `PIO_EXIT=0`.
- RAM: **21.464 B de 327.680 B (6,6%)**.
- Flash: **268.749 B de 2.097.152 B (12,8%)**.
- Nenhum `warning:` ou `error:` no log de build (varredura automatica).
- Flag confirmada no objeto do `main.cpp`: `-std=gnu++17`. A unica ocorrencia de `-std=gnu++11` no log verboso e a linha de eco da configuracao (`build_unflags`), nao uma linha de compilacao.
- O build usa o diretorio global `D:\PlatformIO\workspace\build\esp32dev` (configuracao do ambiente PlatformIO desta maquina, fora do repositorio).

## Testes executados

| Teste | Comando | Resultado |
|---|---|---|
| Logica pura (filtro, volume, politica de publicacao) | `python tools\host_test\check_logic.py` | **TODOS OS TESTES PASSARAM (0 falhas)** |
| Conferencia de copias | SHA-256 de cada arquivo | **ALL_COPIES_MATCH** |
| Projeto original intocado | `git -C Monitora_Reserv status --short` | vazio |
| Estado do novo repositorio | `git status --short` | limpo |

O `check_logic.py` confirmou, entre outros pontos: janela parcial/cheia do filtro, descarte da amostra mais antiga, `ADC 0 -> 0 L`, `ADC 1023 -> 116.651,4 L`, clamp do offset em zero e as quatro decisoes de publicacao (FIRST, RECONNECT, DELTA, HEARTBEAT). A diferenca de ate 64,7 L em relacao ao programa original e o **truncamento inteiro** documentado pelo proprio teste, nao uma regressao da migracao.

`hosttest.cpp` **nao foi compilado** porque este PC nao possui `g++` nativo; ele sera usado quando um compilador de host estiver disponivel. O teste numerico em Python cobre a mesma logica.

## Pendencias e riscos identificados

1. **Sem testes de hardware nesta etapa:** ADC, display, Wi-Fi, NTP, LittleFS, MQTT, Telegram e watchdog continuam pendentes das etapas seguintes.
2. **`uploadfs` nao foi executado:** nenhuma placa foi gravada; o LittleFS sera criado apenas na etapa de persistencia.
3. **ADC:** a atenuacao de 6 dB e a conversao para contagem de 10 bits ainda precisam de validacao em bancada (D2).
4. **TLS/heap:** os pisos do Telegram e o piso do `stall_guard` continuam indefinidos ate a medicao real (D5).
5. **Timeout MQTT:** a condicao de aceite da D6 exige medir o bloqueio do `loop()` e o efeito na aquisicao de 250 ms.
6. **C++17:** confirmado por flag; os modulos que usam `if constexpr` ainda serao compilados nas etapas seguintes.
7. **Diretorio de build global:** o PlatformIO desta maquina usa `D:\PlatformIO\workspace\build\esp32dev`. Nao ha colisao agora porque o projeto original usa `nodemcuv2`, mas dois projetos ESP32 com o mesmo nome de ambiente compartilhariam o diretorio. Acompanhar ou isolar com `workspace_dir` quando houver um segundo projeto ESP32.
8. **Upload em 921600:** se o CH340 falhar, usar `460800`; a compilacao nao e afetada.
9. **Binario maior que o original:** o esqueleto ja ocupa 268.749 B; o firmware completo precisa caber nos 2 MB de `no_ota.csv` e sera monitorado a cada etapa.

## Proxima etapa

Etapa 2 de execucao (Etapa 3 do plano): portar `config.h`, `credentials.h` e `secrets.h.example`, com pinos explicitos (34, 22, 23, 16), `TEST_MODE` exclusivamente no `config.h`, placeholders `COLOQUE_...` e todos os `static_assert` de coerencia. Nenhum modulo funcional sera alterado antes da compilacao e verificacao dessa etapa.

