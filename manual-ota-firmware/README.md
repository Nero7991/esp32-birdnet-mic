# Manual OTA firmware

This folder contains the **v1.30** app-only firmware files for manual OTA update through the device
Web UI.

Open the device update page:

```text
http://<device-ip>/ota
```

Choose **Upload compiled file** and select the file matching your board:

```text
firmware-app-c3.bin    XIAO ESP32-C3
firmware-app-s3.bin    XIAO ESP32-S3
firmware-app-c5.bin    XIAO ESP32-C5
firmware-app-c6.bin    XIAO ESP32-C6
```

`firmware-app.bin` is kept as a compatibility alias for the XIAO ESP32-C6 build.

Upload only the file for your board. Do not use merged `firmware-<board>.bin` images for OTA.
Saved settings and the partition layout are retained during a normal update.

PDM / IM73D122 support is **🟡 Experimental**. On C3/C5 use 512 samples per packet and 160 MHz;
selecting PDM does not replace a previously saved packet size.
See the [wiring and setup guide](../esp32-birdnet-mic/README.md#pdm-microphones-experimental).
