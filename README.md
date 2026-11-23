# MoniRes_ESP32

Migracao independente do monitor de reservatorio `Monitora_Reserv` (ESP8266 / NodeMCU ESP-12E) para **ESP32 DevKit V1 30 pinos** (ESP32-WROOM-32, CH340), preservando aquisicao, calibracao, filtro, display, Wi-Fi/NTP, fila offline, historico, alertas, ThingsBoard, Telegram e diagnosticos.

O projeto original permanece **intocado** em `..\Monitora_Reserv`. Este repositorio tem Git proprio e nao e uma camada de compatibilidade: e ESP32 puro.

## Estado atual

**Etapa 1 concluida:** estrutura independente, `platformio.ini` e modulos puros copiados byte a byte (SHA-256 conferido). O `src/main.cpp` atual e um **esqueleto de contrato de compilacao**; ele nao aciona hardware, rede nem persistencia. A orquestracao real entra nas etapas seguintes.

Ainda nao existem `include/config.h`, `include/credentials.h` nem `include/secrets.h`: eles serao portados na Etapa 3. Por isso o esqueleto nao define `TEST_MODE`; a unica chave TESTE/PRODUCAO continuara sendo `include/config.h`.

## Estrutura

```text
MoniRes_ESP32/
  platformio.ini        esp32dev, no_ota.csv, LittleFS, C++17
  data/.gitkeep         pasta exigida por `pio run -t uploadfs`
  include/              modulos puros copiados do original
  src/                  volume.cpp, moving_average.cpp e o esqueleto main.cpp
  tools/host_test/      hosttest.cpp e check_logic.py
  docs/
    MIGRATION_DECISIONS.md
    TRACEABILITY.md
```

## Comandos

Compilar (nao grava na placa):

```powershell
pio run -d "d:\João\OneDrive\Documentos\Arduino\MoniRes_ESP32"
```

Gravar o firmware:

```powershell
pio run -d "d:\João\OneDrive\Documentos\Arduino\MoniRes_ESP32" -t upload
```

Teste numerico dos modulos puros (nao precisa de placa):

```powershell
cd "d:\João\OneDrive\Documentos\Arduino\MoniRes_ESP32"
python tools\host_test\check_logic.py
```

Se o upload falhar em 921600, use `-t upload --upload-port COMx` e, se necessario, reduza `upload_speed` para `460800` no `platformio.ini`.

**LittleFS:** o firmware nunca formata o sistema de arquivos. Em uma placa nova, envie a imagem **uma vez**:

```powershell
pio run -d "d:\João\OneDrive\Documentos\Arduino\MoniRes_ESP32" -t uploadfs
```

Nao execute `uploadfs` em uma placa com dados que precisam ser preservados.

## Pinagem aprovada

| Funcao | ESP8266 original | ESP32 |
|---|---|---|
| QDY30A (analogico) | A0 | **GPIO34** |
| TM1637 CLK | D1 | **GPIO22** |
| TM1637 DIO | D2 | **GPIO23** |
| Botao do display | D7 | **GPIO16** (ativo em nivel ALTO) |

No ESP32, `A0` **nao** e o GPIO34 (aponta para GPIO36). O codigo novo usa os numeros explicitos; nao reutilize os aliases `A0`, `D1`, `D2` ou `D7`.

## Restricoes de migracao

- Nao alterar formulas fisicas, calibracao, temporizacoes, comandos, mensagens ou formatos persistidos sem aprovacao previa.
- `TEST_MODE` apenas em `include/config.h`; nenhum `-D` de modo.
- Nunca `setInsecure()`; CA + hostname/SNI sempre validados.
- LittleFS sem formatacao automatica; falha de montagem deixa o modulo indisponivel, sem apagar dados.
- O cliente HTTP/1.1 proprio do Telegram e preservado; nao sera trocado por biblioteca.
- Segredos ficam em `include/secrets.h`, ignorado pelo Git; o modelo usa placeholders `COLOQUE_...`.

## Documentacao

- `docs/MIGRATION_DECISIONS.md`: decisoes aprovadas (D1..D11) e pendencias de medicao.
- `docs/TRACEABILITY.md`: mapa arquivo original -> destino, etapa e hashes.
