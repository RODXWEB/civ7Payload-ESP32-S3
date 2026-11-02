# civ7Payload-ESP32-S3

Port do firmware **PicoSwitch** (do projeto [CivSwitch](https://github.com/Gezine/CivSwitch)) para **ESP32-S3** com tela AMOLED.

A placa se apresenta ao Switch como **Pro Controller + teclado USB** (HID composto) e digita o payload de chat-XSS do Civilization 7 (Switch 1, versão 1.0.0 em cartucho). Na tela aparece o estado: **PARADO**, **RODANDO** ou **TERMINADO**.

> O payload não é criado por este repositório: ele vem do CivSwitch (Remote JS Loader). Aqui está só o firmware que o digita e as instruções.

## Hardware

- **Waveshare / Spotpear ESP32-S3-Touch-AMOLED-2.06** (2,06" redonda).
  - ESP32-S3, USB nativo (OTG), 32 MB flash, 8 MB PSRAM (OPI).
  - Display CO5300 QSPI 410×502, touch FT3168, PMU AXP2101.
- Cabo USB-C de **dados** (não serve cabo só de carga).

Outras placas ESP32-S3 com USB nativo funcionam se a tela for removida/ajustada (`main/display.c`), mas o pinout abaixo é o desta placa:

| Função | GPIO |
|---|---|
| LCD SCLK | 11 |
| LCD D0–D3 | 4, 5, 6, 7 |
| LCD CS | 12 |
| LCD RST | 8 |
| LCD offset coluna | 22 |

## Build

Requer **ESP-IDF v5.4.x** (testado com v5.4.4).

```bash
git clone -b v5.4.4 --depth 1 --recurse-submodules --shallow-submodules \
  https://github.com/espressif/esp-idf.git ~/esp/esp-idf
cd ~/esp/esp-idf && ./install.sh esp32s3 && . ./export.sh

cd civ7Payload-ESP32-S3
idf.py set-target esp32s3
idf.py build
```

O `main/idf_component.yml` baixa automaticamente `espressif/tinyusb` e `espressif/esp_lcd_co5300`.

## Gravação

A placa **só entra e sai do modo download manualmente** — o esptool/IDF não consegue resetá-la sozinho.

1. Entre em modo download: segure **BOOT**, segure **PWR 6 s** e solte, toque **PWR**, espere ~2 s e solte **BOOT**.
2. Confira a porta: `ls /dev/ttyACM0` (aparece como `303a:1001`).
3. Grave:

```bash
idf.py -p /dev/ttyACM0 flash
```

4. Para o firmware rodar, faça um **ciclo de energia**: segure **PWR 6 s** (desliga) e toque **PWR** (liga).

## Uso no Switch

1. No Switch, mude o idioma para **English (US)** — o firmware digita em layout US QWERTY.
2. Abra o Civ 7 e navegue: `MULTIPLAYER → WIRELESS → CREATE GAME → HOST LOBBY → CHAT`.
3. Deixe o cursor no chat.
4. Com a placa **desligada**, conecte na dock (modo docked) e ligue. ~1 s após o mount ela começa a digitar as ~87 linhas (~20 min).
5. Ao terminar, a tela mostra **TERMINADO**. Rode o instalador a partir do PC:

```bash
python installer-creator/install.py <ip-do-console>
```

(script do repositório CivSwitch).

### Estados da tela

- **PARADO** (vermelho): sem host USB conectado.
- **RODANDO** (laranja): digitando.
- **TERMINADO** (verde): payload concluído.

Tirar o cabo para o macro e mostra **PARADO**; reconectar recomeça **do início**. Como restart só acontece na reconexão, desligue a placa (PWR 6 s) antes de movê-la.

## Trocar o payload

`main/macro_script.h` é gerado a partir de `payload.txt`. Para usar outro payload:

1. Gere o `payload.txt` no repositório CivSwitch (pipeline `core.js → compress.js → payload_to_macro.py`).
2. Regenere o header:

```bash
python tools/payload_to_macro.py payload.txt main/macro_script.h
```

3. Rebuild e grave.

## Notas técnicas

- `CONFIG_USJ_ENABLE_USB_SERIAL_JTAG=n` em `sdkconfig.defaults`: sem isso a USB-Serial/JTAG ocupa a PHY e a USB-OTG não sobe.
- `usb_new_phy()` é chamado antes de `tusb_init()`.
- O descritor usa `bmAttributes = 0xA0` (remote wakeup) e o firmware chama `tud_remote_wakeup()` para o autosuspend do PC não derrubar a conexão.
- A detecção de cabo removido usa inatividade do endpoint HID (1 s), porque o ESP32-S3 não reporta a perda de VBUS.

## Créditos e licença

- [CivSwitch](https://github.com/Gezine/CivSwitch) (MIT) — firmware PicoSwitch, payload e pipeline.
- PicoSwitch é baseado em [Pico-Switch-Controller](https://github.com/Element2424/Pico-Switch-Controller).
- O núcleo do exploit JS referencia [Netflix-N-Hack](https://github.com/NetflixNHack/Netflix-N-Hack).

Licença MIT — veja [LICENSE](LICENSE). Este port mantém a licença e os créditos do projeto original.

---

## English

ESP32-S3 port of the **PicoSwitch** firmware from [CivSwitch](https://github.com/Gezine/CivSwitch). The board enumerates as a **Switch Pro Controller + USB keyboard** (composite HID) and types the Civ 7 chat-XSS payload. The AMOLED shows **PARADO / RODANDO / TERMINADO** (stopped / running / finished).

**Hardware:** Waveshare/Spotpear ESP32-S3-Touch-AMOLED-2.06 (ESP32-S3, native USB, 32 MB flash, 8 MB PSRAM, CO5300 QSPI display, FT3168 touch, AXP2101 PMU). A data-capable USB-C cable is required.

**Build (ESP-IDF v5.4.x):**

```bash
git clone -b v5.4.4 --depth 1 --recurse-submodules --shallow-submodules \
  https://github.com/espressif/esp-idf.git ~/esp/esp-idf
cd ~/esp/esp-idf && ./install.sh esp32s3 && . ./export.sh
cd civ7Payload-ESP32-S3
idf.py set-target esp32s3
idf.py build
```

**Flash:** the board only enters/leaves download mode manually. Hold **BOOT**, hold **PWR 6 s** and release, tap **PWR**, wait ~2 s, release **BOOT**. Then `idf.py -p /dev/ttyACM0 flash` and power-cycle (PWR 6 s off, tap PWR on) to run.

**Usage:** set the Switch language to **English (US)**; open `MULTIPLAYER → WIRELESS → CREATE GAME → HOST LOBBY → CHAT`, focus the chat, connect the board (powered off) to the dock and turn it on. Typing starts ~1 s after USB mount (~87 lines, ~20 min). When the screen shows **TERMINADO**, run `python installer-creator/install.py <console-ip>` from the CivSwitch repo. Unplugging stops the macro; reconnecting restarts it from the beginning.

**Payload:** `main/macro_script.h` is generated from `payload.txt` with `tools/payload_to_macro.py`. Regenerate it to use a different payload.

**Notes:** `CONFIG_USJ_ENABLE_USB_SERIAL_JTAG=n` is required (otherwise USB-Serial/JTAG owns the PHY and USB-OTG never starts); `usb_new_phy()` runs before `tusb_init()`; `bmAttributes = 0xA0` + `tud_remote_wakeup()` avoid host autosuspend; unplug is detected by HID inactivity (1 s) because the ESP32-S3 does not report VBUS loss.

**Credits:** [CivSwitch](https://github.com/Gezine/CivSwitch) (MIT), [Pico-Switch-Controller](https://github.com/Element2424/Pico-Switch-Controller), [Netflix-N-Hack](https://github.com/NetflixNHack/Netflix-N-Hack). MIT license — see [LICENSE](LICENSE).
