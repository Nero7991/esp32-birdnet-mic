<p align="center">
  <img src="assets/birdlogo.png" alt="ESP32 RTSP Mic" width="240" />
</p>

# ESP32 RTSP Mic for BirdNET

Turn a **Seeed Studio XIAO ESP32** and a digital microphone into a Wi-Fi microphone
for **BirdNET-Go**, **BirdNET-Pi**, VLC or ffmpeg. Listen in your browser, adjust settings
and check device health without installing an app.

**Latest firmware: v1.30** · [Latest published release](https://github.com/Sukecz/esp32-birdnet-mic/releases/latest)
· [Web flasher](https://esp32mic.msmeteo.cz) · [Changelog](esp32-birdnet-mic/CHANGELOG.md)

## What's new in 1.30

- **Redesigned Web UI:** collapsible sections, clear device status and save/discard feedback.
- **Browser listening:** Listen/Stop and playback volume, alongside your BirdNET streams.
- **Experimental PDM support:** IM73D122 on XIAO C3/S3/C5/C6, with mono 48 kHz output.
- **Better diagnostics:** microphone flatline detection, audio-processing load and capture errors.
- **Optional recovery after cooling:** streaming can resume automatically after a thermal shutdown.
- **Audio and OTA improvements:** integer audio processing, adaptive memory reserve and smaller C3/C6 builds.

Normal OTA updates keep the existing partition layout and saved settings. No USB migration is required.

## Support at a glance

🟢 **Supported** — implemented; hardware evidence is stated below.
🟡 **Experimental** — implemented, but integrated hardware validation is incomplete.
🔴 **Not supported** — unavailable in this firmware.

| Feature | Status | Details |
|---|---|---|
| I2S microphones | 🟢 Supported | ICS-43434, SPH0645LM4H and INMP441; see microphone table |
| PDM / IM73D122 | 🟡 Experimental | 48 kHz mono; physical validation of this integrated firmware is pending |
| PCM1808 external ADC | 🟡 Experimental | Audio remained noisy in testing; not recommended for a new build |
| BirdNET-Go / BirdNET-Pi | 🟢 Supported | RTSP over TCP / UDP respectively |
| Browser listening | 🟢 Supported | One browser listener, separate from RTSP client slots |
| Web settings, logs and JSON API | 🟢 Supported | Includes microphone, Wi-Fi, temperature and stream health |
| MQTT / Home Assistant discovery | 🟢 Supported | Optional broker connection |
| Streaming schedule and deep sleep | 🟢 Supported | Local-time windows, including overnight schedules |
| Thermal protection | 🟢 Supported | Optional automatic recovery is OFF by default |
| USB installation and OTA updates | 🟢 Supported | Separate firmware for each supported board |
| Stereo, USB audio or Bluetooth audio | 🔴 Not supported | Output is mono PCM over the network |
| SD-card recording | 🔴 Not supported | Recording/analysis belongs on the receiving system |

All four XIAO targets have build support. Maintainer runtime testing primarily uses **C6 with
ICS-43434**; a successful build does not establish long-term hardware stability on every board.

## Web UI

![Web UI with browser listening, device status and collapsible settings](assets/webui2.png)

*Screenshot from development firmware v1.27; the same interface is included in v1.30.*

## Quick start

1. Choose a board and microphone from the [parts list](#parts-list), then wire it with power disconnected.
2. Connect the XIAO by **USB-C data cable**. Open the [web flasher](https://esp32mic.msmeteo.cz)
   in desktop Chrome or Edge, click **Flash**, and select the device. Chip detection selects the image;
   the supplied firmware is intended for XIAO boards.
3. Join **ESP32-RTSP-Mic-AP**. Open `http://192.168.4.1` if the setup page does not appear,
   then save your Wi-Fi details.
4. Open `http://<device-ip>/` using the address shown in your router's device list.
5. In **Audio → Microphone format**, select your microphone and apply. Leave other audio
   settings at their defaults initially; follow the extra PDM steps below if applicable.
6. Press **Listen** to check the sound. In **Streams**, enable a stream, choose BirdNET-Go or
   BirdNET-Pi, and copy its URL into that system. Stream 1 is enabled by default; Stream 2 is optional.

```text
rtsp://<device-ip>:8554/audio1    Stream 1 (default target: BirdNET-Go)
rtsp://<device-ip>:8554/audio2    Stream 2 (default target: BirdNET-Pi)
```

Both streams share the microphone settings. Up to 1–3 RTSP clients are configurable (default: 2).
The device's `.local` hostname can replace its IP where your network supports mDNS.

## Microphones and wiring

### I2S microphones

| Microphone | Select in Audio → Microphone format | Hardware evidence |
|---|---|---|
| **Adafruit SPH0645LM4H** | MSB / left-justified | Contributor-tested; recommended I2S option for new builds |
| **ICS-43434** | Philips I2S (default) | Maintainer-tested reference |
| **INMP441** | Philips I2S | [User-reported compatible](https://github.com/Sukecz/esp32-birdnet-mic/discussions/25) |

<picture>
  <source media="(prefers-color-scheme: dark)" srcset="assets/connection-dark.png">
  <source media="(prefers-color-scheme: light)" srcset="assets/connection.png">
  <img alt="XIAO ESP32-C6 I2S wiring; ICS-43434 module shown" src="assets/connection.png">
</picture>

Use the **printed XIAO pin labels** below on C3/S3/C5/C6; GPIO numbers differ between boards.

| I2S microphone pin | XIAO pin |
|---|---|
| VDD / VCC | 3V3 |
| GND | GND |
| BCLK / SCK | D3 |
| LRCLK / WS | D1 |
| SD / DOUT | D2 |
| L/R / SEL | GND (left channel) |

Leave D7 unused for these microphones. See the [GPIO table and PCM1808 notes](esp32-birdnet-mic/README.md#i2s-wiring)
for advanced setups. Use **3.3 V**, not 5 V, for the microphone.

### PDM microphone — 🟡 Experimental

The [IM73D122 module](https://www.aliexpress.com/item/1005010674767599.html) uses **different wiring**:

| PDM microphone pin | XIAO pin |
|---|---|
| VDD | 3V3 (check the breakout's supply rating) |
| GND | GND |
| CLK | D3 |
| DATA | D2 |
| SEL / L-R | GND (left slot) |

Leave **D1 and D7 disconnected**. Check the module's pin labels before connecting power.
Select **PDM microphone (IM73D122)** in Audio; this sets **48 kHz** automatically.
Gain and high-pass filtering remain available; I2S Shift does not apply.

**On C3/C5, set Audio → Advanced audio settings → RTP packet size to 512 samples,
and use 160 MHz CPU in Thermal management.** The default packet size is already 512, but
selecting PDM preserves a previously saved larger size. The firmware reduces its internal audio
reserve when RAM is limited; this does not automatically change the packet-size setting.

S3 uses hardware PDM conversion; C3/C5/C6 use software conversion. Dan Logue reports hardware
tests with his contributing firmware. Physical microphone and long-running tests of this integrated
release are still pending, so PDM remains experimental on all four boards.
[Detailed PDM notes](esp32-birdnet-mic/README.md#pdm-microphones-experimental).

## Parts list

| Part | Qty | Notes | Link |
|---|---:|---|---|
| Seeed Studio XIAO ESP32-C3 | 1 | Supported XIAO target board | [Seeed Studio](https://www.seeedstudio.com/Seeed-XIAO-ESP32C3-p-5431.html) |
| Seeed Studio XIAO ESP32-S3 | 1 | **Recommended for new builds** | [Seeed Studio](https://www.seeedstudio.com/XIAO-ESP32S3-p-5627.html) |
| Seeed Studio XIAO ESP32-C5 | 1 | Supported XIAO target board | [Seeed Studio](https://www.seeedstudio.com/Seeed-Studio-XIAO-ESP32C5-p-6609.html) |
| Seeed Studio XIAO ESP32-C6 | 1 | Supported XIAO target board | [Seeed Studio](https://www.seeedstudio.com/Seeed-Studio-XIAO-ESP32C6-p-5884.html) |
| MEMS I2S microphone **Adafruit SPH0645LM4H** | 1 | **Recommended for new builds**; select MSB / left-justified in the Web UI | [Adafruit](https://www.adafruit.com/product/3421) |
| MEMS I2S microphone **ICS-43434** | 1 | Maintainer-tested I2S reference | [TDK product status](https://product.tdk.com/en/search/sw_piezo/mic/mems-mic/info?part_no=ICS-43434) |
| MEMS I2S microphone **INMP441** | 1 | User-reported compatible with Philips I2S mode | [TDK product status](https://product.tdk.com/en/search/sw_piezo/mic/mems-mic/info?part_no=INMP441) |
| PDM microphone **IM73D122 breakout** | 1 alternative | 🟡 Experimental; different wiring from I2S, see below | [AliExpress](https://www.aliexpress.com/item/1005010674767599.html) |
| Shielded cable, 5+ core | Optional | I2S mic needs 5 conductors: 3V3, GND, BCLK, LRCLK/WS, and SD/DOUT. A 6-core cable is a good practical choice for spare/shield handling. | [AliExpress](https://www.aliexpress.com/item/1005010375728700.html) |
| 5 V power supply | 1 | At least 1 A recommended. **The AliExpress page may default to 12 V; manually select the 5 V version. Never connect 12 V to the XIAO.** | [AliExpress](https://www.aliexpress.com/item/1005002624537795.html) |
| 2.4 GHz antenna, IPEX/U.FL | Recommended | Important for stable XIAO Wi-Fi streaming | [AliExpress](https://www.aliexpress.com/item/1005005439361093.html) |

Choose **one board and one microphone**, plus a USB-C data cable for initial setup.
Links are examples only. Verify the exact part number before buying.

Power the XIAO either through USB-C with a normal USB power supply or through its 5V/VBUS input
with a regulated 5 V supply. A higher-wattage USB-C supply is fine because the board draws only the
current it needs. Do not connect USB-C and an external 5 V source at the same time.

## Everyday use and troubleshooting

- **No sound:** check wiring and microphone format, enable the RTSP server, then press Listen.
  A flatline warning means the captured samples have stopped changing; it is not a microphone-quality measurement.
- **Dropouts:** check Wi-Fi signal, attach the board's antenna, and start with 512 samples per packet.
  Use the target setting matching BirdNET-Go (TCP) or BirdNET-Pi (UDP).
- **Noise:** keep microphone wires short and away from the antenna. Try lower Wi-Fi transmit power
  when the access point is nearby. High-pass filtering reduces low-frequency rumble (default: 500 Hz).
- **Temperature warning:** improve cooling or placement. Automatic recovery is optional and OFF
  by default; repeated or emergency shutdowns need acknowledgement after cooling.
- **Browser playback stops:** only one browser can listen at a time. Leaving/hiding the page,
  changing audio settings or updating firmware stops playback; press Listen again when ready.
- **Updates:** open **Firmware update** in the device header. Automatic update uses the published
  image; manual upload needs `firmware-app-<board>.bin`. Never use a merged `firmware-<board>.bin` for OTA.

Keep the device on a trusted LAN: HTTP/RTSP have no built-in user authentication or TLS.
For setup details, schedules, MQTT and API examples, see the [firmware guide](esp32-birdnet-mic/README.md).

## Build From Source

Use Bash (Linux, macOS, or Windows WSL), Python 3 and Arduino CLI **1.5.0**.
Run these commands from the repository root. First-time dependency setup:

```bash
arduino-cli config add board_manager.additional_urls https://espressif.github.io/arduino-esp32/package_esp32_index.json
python3 tools/check_build_dependencies.py --install
```

Build a XIAO ESP32-C6 image:

```bash
tools/compile_firmware.sh --fqbn esp32:esp32:XIAO_ESP32C6 \
  --output-dir esp32-birdnet-mic/build/c6
```

For another supported board, replace `XIAO_ESP32C6` with `XIAO_ESP32C3`, `XIAO_ESP32S3`,
or `XIAO_ESP32C5` and choose a matching output folder. The helper automatically enables
LTO for C3/C6; S3/C5 keep normal compilation.

For OTA, select `esp32-birdnet-mic/build/c6/esp32-birdnet-mic.ino.bin` under
**Firmware update -> Upload compiled file** in the device Web UI. Use the image built for
your board; OTA needs the app-only `.ino.bin`, not the `.merged.bin` USB image.
Existing devices use the same OTA procedure and partition layout.

Ordinary Arduino IDE or direct `arduino-cli compile` builds do not automatically enable LTO.
See the [full build instructions](esp32-birdnet-mic/README.md#build-and-flash-manually)
for more details.

### Arduino IDE Build Size

Keep `esp32-birdnet-mic/build_opt.h` next to the sketch. The ESP32 Arduino core loads it
automatically in Arduino IDE and `arduino-cli`; it disables WiFiManager serial debug output
and unused C++ exception/unwind metadata. The CLI helper above also applies the C3/C6 LTO
settings used for the packaged firmware.

## More documentation

- [Firmware guide and API](esp32-birdnet-mic/README.md)
- [Manual OTA files](manual-ota-firmware/README.md)
- [Web flasher and firmware file guide](web-flasher/README.md)
- [Release history](esp32-birdnet-mic/CHANGELOG.md)

## Credits and license

Created by **@Sukecz**, with thanks to **Dan Logue** for his PDM contribution and hardware testing,
and to everyone reporting compatibility and testing new builds. Released under the [MIT License](LICENSE).
