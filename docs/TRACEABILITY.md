# Rastreabilidade MoniRes_ESP32

Este documento liga cada arquivo do projeto original `Monitora_Reserv` (ESP8266) ao destino no `MoniRes_ESP32` (ESP32) e registra o estagio em que ele sera portado.

- Original: `d:\João\OneDrive\Documentos\Arduino\Monitora_Reserv`
- Novo: `d:\João\OneDrive\Documentos\Arduino\MoniRes_ESP32`
- Ultima atualizacao: 2026-11-23 (Etapa 1)

## Copiados sem nenhuma alteracao

Todos os arquivos abaixo foram copiados byte a byte e tiveram o SHA-256 conferido contra o original.

| Arquivo original | Destino | Etapa | Estado |
|---|---|---|---|
| `include/moving_average.h` | `include/moving_average.h` | 1 | copia identica |
| `src/moving_average.cpp` | `src/moving_average.cpp` | 1 | copia identica |
| `include/volume.h` | `include/volume.h` | 1 | copia identica (calibracao intocada) |
| `src/volume.cpp` | `src/volume.cpp` | 1 | copia identica (calibracao intocada) |
| `include/publish_policy.h` | `include/publish_policy.h` | 1 | copia identica |
| `include/rate.h` | `include/rate.h` | 1 | copia identica |
| `include/uptime.h` | `include/uptime.h` | 1 | copia identica |
| `include/queue_record.h` | `include/queue_record.h` | 1 | copia identica (formato persistido) |
| `include/history_record.h` | `include/history_record.h` | 1 | copia identica (formato persistido) |
| `include/alerts.h` | `include/alerts.h` | 1 | copia identica (logica pura) |
| `include/telegram_text.h` | `include/telegram_text.h` | 1 | copia identica (textos/comandos) |
| `include/boot_reason.h` | `include/boot_reason.h` | 1 | copia identica (tabela de textos) |
| `tools/host_test/hosttest.cpp` | `tools/host_test/hosttest.cpp` | 1 | copia identica |
| `tools/host_test/check_logic.py` | `tools/host_test/check_logic.py` | 1 | copia identica |

## Hashes SHA-256 (origem = destino)

```text
moving_average.h    CC17CC8106D5D85E36C0CFF8742C2528DC461F1666A661157E0FD290D6886C7A
volume.h            A627713E15F28AF39EC02DDED64161A222A4685484C5E3C81E6FB74EE7816096
publish_policy.h    77058B84296B26362A04A54E2BA3C5981998223551B8FDFFB8E98C0A00138339
rate.h              91C8CC56CB80D10B7437C39407E3F916C42894B1D37AE9D4745B6D921B5BF07E
uptime.h            1ADE6C9172D49738A7E237822915E6733C80675A7988969FE3630A758B110BF6
queue_record.h      21081737C3C5BFE1E36C4084980FD5C2D1D514BACC922F3C154537704C22586E
history_record.h    CCDD81F4DAD301417DAD8D3DA83752A4FF47D36AE2AF90481A700EB168902486
alerts.h            DE6CFC412D332C5E6309096EA839BE24EE07CE009B9D266F4BAD898CB28FB78D
telegram_text.h     9574FBE4A9463AD512055A903D6C5082F63FC3440753B22D5BB591BBBE4635F2
boot_reason.h       7AD25E5B2B66571979E74476E6904EB72E6190004D0C492BB63DF3F5D29DBA6B
moving_average.cpp  DB185934EBCDDADCB6CE9B3FA9BFDCE4DB9537A635B21EB0A1DE84E50855B196
volume.cpp          B3441BDD6458A48F40E1EF9E206A80C1FEA4F1A8FF61D5436DD4F36CA4C5E8AC
```

## Ainda nao copiados ou portados

| Arquivo original | Destino previsto | Etapa | Observacao |
|---|---|---|---|
| `include/config.h` | `include/config.h` | 3 | portar apenas o que e plataforma; preservar constantes fisicas/temporizacoes |
| `include/credentials.h` | `include/credentials.h` | 3 | `TEST_MODE` continua exclusivo daqui |
| `include/secrets.h.example` | `include/secrets.h.example` | 3 | placeholders corrigidos para `COLOQUE_...` (D10) |
| `include/sensor.h`, `src/sensor.cpp` | idem | 4 | ADC: `analogReadMilliVolts` + contagem 10 bits (D2) |
| `include/simulator.h`, `src/simulator.cpp` | idem | 4 | simulador preservado; valida a cadeia no TESTE |
| `include/display.h`, `src/display.cpp` | idem | 6 | pinos GPIO22/GPIO23/GPIO16 |
| `include/wifi_manager.h`, `src/wifi_manager.cpp` | idem | 7 | `WiFi.onEvent` + `ARDUINO_EVENT_WIFI_STA_DISCONNECTED` |
| `include/clock.h`, `src/clock.cpp` | idem | 7 | NTP; conferir sinal do offset |
| `include/offline_queue.h`, `src/offline_queue.cpp` | idem | 8 | FS sem `Dir`/`File::truncate`; `ftruncate` POSIX (D4) |
| `include/history.h`, `src/history.cpp` | idem | 9 | `FSInfo` substituido por helper; formato intacto |
| `include/alert_monitor.h`, `src/alert_monitor.cpp` | idem | 10 | apenas o acesso ao FS muda |
| `include/thingsboard.h`, `src/thingsboard.cpp` | idem | 11 | mbedTLS, timeouts em segundos, `getEfuseMac` (D5/D6) |
| `include/datalog_mqtt_ca.h` | idem | 11 | CA preservada; `setCACert` |
| `include/telegram.h`, `src/telegram.cpp` | idem | 12 | HTTP proprio preservado; so o transporte TLS muda |
| `include/telegram_ca.h` | idem | 12 | CA preservada; sem `setInsecure` |
| `include/stall_guard.h`, `src/stall_guard.cpp` | idem | 13/14 | recalibrar piso de bloco com medicao |
| `src/main.cpp` | `src/main.cpp` | 13/15 | o arquivo atual e um esqueleto da Etapa 1; a orquestracao real substitui o conteudo |
| `tools/host_test/check_queue.py` | idem | 8 | espelho do formato; sem mudanca de layout |
| `tools/host_test/check_history.py` | idem | 9 | constantes preservadas; ajustar comentario de blockSize |
| `tools/host_test/check_alerts.py` | idem | 10 | sem mudanca de formato |
| `tools/host_test/check_rate.py` | idem | 5 | sem mudanca |
| `tools/host_test/check_telegram.py` | idem | 12 | reescrever assercoes para APIs ESP32 (D11) |
| `README.md` do projeto | `README.md` | 1/15 | novo README; o original nao e copiado |

## Arquivos novos (sem correspondente no original)

| Arquivo | Etapa | Papel |
|---|---|---|
| `platformio.ini` | 1 | `esp32dev`, `no_ota.csv`, LittleFS, C++17, dependencias fixadas |
| `.gitignore` | 1 | `.pio`, `secrets.h`, artefatos |
| `docs/MIGRATION_DECISIONS.md` | 1 | decisoes aprovadas |
| `docs/TRACEABILITY.md` | 1 | este documento |
| `data/.gitkeep` | 1 | mantem a pasta `data/` para `uploadfs` |
| `src/main.cpp` (esqueleto) | 1 | contrato de compilacao dos modulos puros |

## Regras de rastreabilidade

1. Nenhum arquivo e editado no projeto original; a copia e feita sempre do original para o novo.
2. Antes de portar um modulo, o hash da copia e comparado com o original; qualquer divergencia e registrada.
3. Alteracoes de plataforma sao marcadas no codigo com um comentario `MoniRes_ESP32:` e justificadas em `docs/MIGRATION_DECISIONS.md`.
4. Formatos persistidos, textos do Telegram, comandos, pinos logicos e temporizacoes so mudam com aprovacao previa.
5. O projeto novo nunca le ou converte o LittleFS do ESP8266: a imagem e criada do zero no ESP32.

