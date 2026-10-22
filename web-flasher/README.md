# Web flasher and firmware files

**Firmware: 1.34** (2026-10-22).

Install using [esp32mic.msmeteo.cz](https://esp32mic.msmeteo.cz) in desktop **Chrome or Edge**
with a USB **data** connection (AtomS3U plugs directly into USB-A). The flasher detects the ESP chip family and chooses the image for
XIAO ESP32-C3/S3/C5/C6. It cannot distinguish other board designs using the same chip.

**XIAO ESP32-S3 Sense / M5Stack AtomS3U:** use the S3 image, then open
**Audio → S3 board** in the device Web UI, select your board and click **Apply board**.
AtomS3U is contributor-tested with BirdNET-Go on v1.33. Sense remains
**Experimental — needs more testing**; its profile also accepts external I2S microphones.

For wiring, microphone selection and feature status, see the [main guide](../README.md).
PDM (IM73D122 / Adafruit PDM / MP34DT01) is **🟡 Experimental**; C3/C5 should use **512 samples per packet** and **160 MHz**.
Keeping saved values preserves the packet size; **Recommended settings** applies 512.

## Choose the right file

| Purpose | File | Notes |
|---|---|---|
| Update through the device's OTA page | `firmware-app-<board>.bin` | App-only; choose c3, s3, c5 or c6 |
| Exact 1.34 OTA image | `firmware-app-<board>-1.34.bin` | Same app, versioned filename |
| USB web installation | `manifest.json` and its referenced parts | Let the web flasher handle offsets |
| Manual full USB flash | `firmware-<board>.bin` | Merged image; can overwrite settings; never upload through OTA |
| Legacy unsuffixed files | `firmware-app.bin`, `firmware.bin` | **C6 only** |

Normal OTA retains saved settings and the existing partition layout; no USB migration is required.
USB installation may erase settings: choose to keep existing data when updating through the flasher.

### Structure
- `index.html` - main page with the "Flash" button and instructions.
- `manifest.json` - ESP Web Tools manifest, writes only firmware parts without NVS and includes C3/S3/C5/C6 builds.
- `bootloader-<board>.bin`, `partitions-<board>.bin`, `boot_app0-<board>.bin` - system parts for USB web flashing.
- `firmware-<board>.bin` - merged image (bootloader + partition + app), only for manual full flash.
- `firmware-app-<board>.bin` - app-only image for OTA update through the device Web UI.
- `bootloader.bin`, `partitions.bin`, `boot_app0.bin`, `firmware.bin`, `firmware-app.bin` - compatible C6 aliases.
- `ota-version.txt` - simple text file with the OTA build version.

The manifest intentionally does not use the merged `firmware.bin` at offset `0x0`, because during an
update it would overwrite the NVS area from `0x9000` with `0xff` bytes and erase Wi-Fi and application
settings. ESP Web Tools also treats flashing an unrecognized device as a new installation and erases
the whole flash by default. Because of that, `new_install_prompt_erase` is enabled in the manifest;
when updating, choose to keep existing data.

### OTA update without USB
1. Open **https://esp32mic.msmeteo.cz**.
2. In the **OTA update without USB** section, enter the device IP address, for example `192.168.1.80`.
3. Click **Open OTA update**.
4. On the device page, choose one option:
   - **Automatic update**: the device downloads the latest board-specific app-only firmware from the web.
   - **Upload compiled file**: manually select an app-only `.bin` file.

Automatic download uses a stable plain HTTP URL based on the compiled board profile, for example
`http://esp32mic.msmeteo.cz/firmware-app-c3.bin`. Deployment replaces each stable board alias with
the latest image and verifies it against the versioned artifact. The server must serve these files
without redirects, with an octet-stream content type and valid content length. HTTPS/TLS does not
fit in the tight default XIAO ESP32-C3/C6 partitions. `firmware-app.bin` remains a C6-compatible
alias for older firmware.

Do not upload `firmware-<board>.bin` or `firmware.bin` on the OTA page. Those are USB merged images.
For OTA, use `firmware-app-<board>.bin`; `firmware-app.bin` is the compatible C6 alias.

## Build and host

Use the [source build instructions](../esp32-birdnet-mic/README.md#build-and-flash-manually).
Keep offsets from the target board's generated `flash_args`; do not reuse another chip's offsets.

Host `index.html`, `manifest.json`, the firmware parts and `ota-version.txt` together.
WebSerial needs HTTPS (or localhost). JSON and firmware must have the appropriate MIME types:
`application/json` and `application/octet-stream`. Automatic device OTA additionally needs direct
HTTP access to the board-specific app files without redirects.

GitHub release assets include all four boards' bootloader, partition, boot_app0, app-only and merged
images, plus the manifest and version file. See the [changelog](../esp32-birdnet-mic/CHANGELOG.md).
