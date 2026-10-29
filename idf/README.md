# ESP-IDF build (Tailscale fork, XIAO ESP32-S3 Sense)

Builds the sketch in `../esp32-birdnet-mic` with ESP-IDF 5.5 and arduino-esp32 3.3.12 as a
component, so the project's own `sdkconfig.defaults` controls PSRAM, lwIP and power settings.
The arduino-cli build in `../tools/compile_firmware.sh` is unchanged and still builds upstream.

    git submodule update --init
    . ~/esp/v5.5/esp-idf/export.sh
    idf.py -C idf set-target esp32s3     # first time only
    idf.py -C idf build
    idf.py -C idf -p /dev/serial/by-id/<board> flash

Notes:

- `tools/ino_to_cpp.py` turns the `.ino` into `build/.../sketch_gen.cpp`, adding the function
  prototypes the Arduino builder would add.
- The registry package of arduino-esp32 ships only generic variants. The core is built for
  `esp32s3`; the sketch gets the XIAO pin map from `main/variant/pins_arduino.h` (copied from
  arduino-esp32 3.3.12).
- `flash` keeps NVS, so Wi-Fi and settings survive. Use `idf.py erase-flash` to start clean.
- Host tests need Node 22 and `pip install -r tools/requirements-webui.txt`.
