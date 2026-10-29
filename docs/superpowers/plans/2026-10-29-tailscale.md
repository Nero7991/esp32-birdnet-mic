# On-device Tailscale (S3 Sense) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A fork of esp32-birdnet-mic, built with ESP-IDF 5.5 + arduino-esp32 as a component, whose XIAO ESP32-S3 Sense image joins a Tailscale tailnet via MicroLink and serves RTSP and the web UI on its 100.x address.

**Architecture:** A new `idf/` ESP-IDF project compiles the unchanged sketch sources from `esp32-birdnet-mic/` with arduino-esp32, WiFiManager, PubSubClient and MicroLink as components, and owns `sdkconfig.defaults` (PSRAM, lwIP, mbedTLS). A new `TailscaleLink` module in the sketch stores the auth key in NVS, starts MicroLink after Wi-Fi is up, and reports status; the web UI gets a Tailscale card and endpoints. The arduino-cli build stays untouched for upstream merges.

**Tech Stack:** ESP-IDF v5.5 (`~/esp/v5.5/esp-idf`, installed v5.5.4+), arduino-esp32 3.3.x component, MicroLink `CamM2325/microlink` at `216da33`, Python unittest + host g++ tests (existing `tests/` style).

**Spec:** `docs/superpowers/specs/2026-10-29-tailscale-design.md`

## Global Constraints

- Fork: `Nero7991/esp32-birdnet-mic`, branch `tailscale`, based on upstream `2f13df8` (v1.36). Keep `origin`-style upstream remote as `upstream`.
- Firmware version string: `1.36-ts.1` (S3 IDF build only; the arduino-cli builds keep `1.36`).
- Target: XIAO ESP32-S3 Sense only, 8 MB flash, 8 MB octal PSRAM. Test unit `/dev/ttyACM2`, MAC `ac:27:6e:a8:4c:84`. Never touch `/dev/ttyACM0` or `/dev/ttyACM1` (QuickVolt boards).
- Partition table identical to the published `partitions-s3.bin`: nvs 0x9000 20K, otadata 0xe000 8K, app0 0x10000 3264K, app1 0x340000 3264K, spiffs 0x670000 1536K, coredump 0x7f0000 64K.
- Library pins: WiFiManager 2.0.17, PubSubClient 2.8, MicroLink `216da33`, arduino-esp32 the 3.3.x release closest to 3.3.12 that builds on ESP-IDF 5.5.
- Auth key: only from NVS, entered through the web UI by the owner. Never compiled in, logged (serial or `webui_pushLog`), returned by any endpoint, published over MQTT, or committed. `CONFIG_ML_TAILSCALE_AUTH_KEY=""` and `CONFIG_ML_ENABLE_CONFIG_HTTPD=n`.
- Tailscale off when no key is stored or no PSRAM is found; failure of any kind degrades to LAN-only and never reboots the device.
- Commits: explicit `git add <paths>` only; no Co-Authored-By lines; no em-dashes or emojis in code, UI or docs.
- The agent never reads, prints or handles the auth key. Hardware steps that need a key stop and ask the owner to enter it in the web UI.

## Review Focus

1. Key leakage through a path no one thought of (crash/coredump log, `/api/logs`, MQTT discovery payload, a `String` concatenated into a debug line): expect no `tskey-` substring anywhere observable. Pinned by `test_tailscale_key_never_exposed` (Task 2) and the leak sweep (Task 6).
2. Tailnet unreachable at boot (Wi-Fi up, internet down, or wrong key): expect the LAN web UI and RTSP to come up on time and the status to say why. Pinned by Task 3 Step 6.
3. RTSP DESCRIBE/SETUP answered with the LAN IP to a tailnet client, so the client follows a URL it cannot reach: expect URLs built from the socket's local address. Pinned by `test_rtsp_urls_use_socket_address` (Task 4).
4. Audio starvation from WireGuard work on the same core as capture: expect dropout counters over a 1-hour tailnet pull no worse than a LAN pull on stock v1.36. Pinned by Task 6 Step 2.
5. The upstream "install update" path replacing the fork, including a direct POST to `/ota/install`: expect refusal with a message. Pinned by `test_official_ota_pull_disabled` (Task 5).

---

### Task 1: Fork and ESP-IDF build of the unchanged firmware

**Files:**
- Create: `idf/CMakeLists.txt`, `idf/main/CMakeLists.txt`, `idf/main/idf_component.yml`, `idf/sdkconfig.defaults`, `idf/partitions.csv`, `idf/components/` (WiFiManager, PubSubClient as git submodules at the pinned tags), `idf/README.md`
- Modify: `esp32-birdnet-mic/esp32-birdnet-mic.ino:90` (version guarded by `#ifdef BIRDNET_TS_FORK`)
- Test: `tests/test_idf_project.py`

**Interfaces:**
- Produces: build command `idf.py -C idf build`; flash command `idf.py -C idf -p /dev/ttyACM2 flash`; compile define `BIRDNET_TS_FORK=1` set only by `idf/main/CMakeLists.txt`.

- [ ] **Step 1:** Fork on GitHub (`gh repo fork Sukecz/esp32-birdnet-mic --clone=false`), set the local clone's remotes to `origin` = fork, `upstream` = Sukecz, create branch `tailscale`. Move the already-written spec and this plan onto the branch.
- [ ] **Step 2: Write `tests/test_idf_project.py`** asserting:
  - `idf/sdkconfig.defaults` contains `CONFIG_IDF_TARGET="esp32s3"`, `CONFIG_ESPTOOLPY_FLASHSIZE_8MB=y`, `CONFIG_SPIRAM=y`, `CONFIG_SPIRAM_MODE_OCT=y`, `CONFIG_FREERTOS_HZ=1000`, `CONFIG_PARTITION_TABLE_CUSTOM=y`, `CONFIG_LWIP_IP4_FRAG=y`, `CONFIG_LWIP_IP4_REASSEMBLY=y`, `CONFIG_ML_TAILSCALE_AUTH_KEY=""`, `CONFIG_ML_ENABLE_CONFIG_HTTPD=n`.
  - `idf/partitions.csv` parses to exactly the Global Constraints table (compare with `gen_esp32part.py` output of `web-flasher/partitions-s3.bin`).
  - `idf/main/CMakeLists.txt` adds every `*.cpp` and the `.ino` from `../../esp32-birdnet-mic/` (no copies), and applies the `build_opt.h` flags.
  - the `.ino` defines `FW_VERSION "1.36-ts.1"` only under `BIRDNET_TS_FORK`.
- [ ] **Step 3:** Run `python3 -m unittest tests/test_idf_project.py`. Expected: FAIL (files missing).
- [ ] **Step 4:** Create the `idf/` project. Compile the `.ino` as C++ (e.g. a `main/sketch.cpp` that `#include`s it, or `set_source_files_properties(... LANGUAGE CXX)`). Match the upstream S3 image's USB serial behaviour (log on USB-JTAG at `/dev/ttyACM2`, as stock v1.36 prints the heap line there). MicroLink and its Kconfig values are added in Task 3, not here; the two `CONFIG_ML_*` lines are inert until then.
- [ ] **Step 5:** Run the unit test (PASS) and `idf.py -C idf build`. Expected: build succeeds; `idf.py -C idf size` app size under 3264 KB.
- [ ] **Step 6 (hardware):** Flash `/dev/ttyACM2` (app + bootloader + partitions; do not erase NVS so the saved Wi-Fi survives). Expected: serial shows `WiFi connected: <ip>`, the heap line reports PSRAM about 8 MB, `/api/status` reports `fw_version` `1.36-ts.1`, and `ffmpeg -rtsp_transport tcp -i rtsp://<ip>:8554/audio1 -t 60 -f null -` (install ffmpeg on the laptop if missing; ask the owner for sudo) exits 0. If Sense audio is silent here, also flash stock v1.36 and compare before going on (spec: baseline upstream Sense audio first).
- [ ] **Step 7:** Commit `idf/`, `.gitmodules`, `tests/test_idf_project.py`, the `.ino` change, spec and plan. Push branch to `origin`.

### Task 2: Tailscale settings store and endpoints (no networking yet)

**Files:**
- Create: `esp32-birdnet-mic/TailscaleConfig.h` (pure logic, host-testable), `esp32-birdnet-mic/TailscaleLink.h`, `esp32-birdnet-mic/TailscaleLink.cpp`
- Modify: `esp32-birdnet-mic/WebUI.cpp` (routes near line 1935), `esp32-birdnet-mic/esp32-birdnet-mic.ino` (`/api/status` JSON near line 1027)
- Test: `tests/test_tailscale_config.py`

**Interfaces:**
- Produces (TailscaleConfig.h):
  - `bool tailscaleKeyLooksValid(const String &key)`: true only for `tskey-auth-` prefix, length 20 to 200, no whitespace or control characters.
  - `const char *tailscaleStateName(int mlState)`: `"off"`, `"waiting_wifi"`, `"connecting"`, `"registering"`, `"connected"`, `"reconnecting"`, `"error"`, `"no_psram"`, `"no_key"`.
  - `String tailscaleStatusJson(const TailscaleStatus &s)` with `struct TailscaleStatus { bool available; bool hasKey; const char *state; String ip; String hostname; String peerPath; String lastError; }`. JSON keys: `available`, `has_key`, `state`, `ip`, `hostname`, `peer_path`, `last_error`. No field ever carries the key.
- Produces (TailscaleLink.h, only under `BIRDNET_TS_FORK`): `void tailscaleLoad()`, `bool tailscaleSaveKey(const String &key)`, `void tailscaleClearKey()`, `bool tailscaleHasKey()`, `TailscaleStatus tailscaleStatus()`. NVS namespace `tsnet`, keys `key`, `host`.
- Produces (HTTP): `GET /api/tailscale` -> `tailscaleStatusJson`; `POST /api/tailscale` form fields `auth_key` (optional), `hostname` (optional, mDNS-safe), `clear` (`on`/`off`); responses never echo `auth_key`. `/api/status` gains `"tailscale": <same JSON>`.

- [ ] **Step 1: Write tests** in `tests/test_tailscale_config.py` (compile `TailscaleConfig.h` on host with `tests/stubs`, like `test_audio_diagnostics.py`):
  - `test_key_validation`: valid `tskey-auth-kAbC123CNTRL-0123456789abcdef`; invalid `""`, `tskey-api-...`, key with a space, key with `\n`, 201 chars.
  - `test_state_names`: each ML state 0..6 plus the two extra states maps to the names above.
  - `test_status_json_shape`: keys exactly as listed; `has_key` true when a key is set.
  - `test_tailscale_key_never_exposed`: source scan of `TailscaleLink.cpp`, `WebUI.cpp` and the `.ino`: the variable holding the key is never passed to `simplePrintln`, `Serial.print*`, `webui_pushLog`, `mqttClient.publish`, or concatenated into a `String` that reaches those; and `httpTailscaleSet` never writes `auth_key` into its response.
- [ ] **Step 2:** Run. Expected: FAIL (header missing).
- [ ] **Step 3:** Implement the three files and the routes. The key lives in NVS and in one heap buffer that stays alive for MicroLink (its `auth_key` is a borrowed pointer). Saving a key or clearing it schedules a reboot via the existing `scheduleReboot` so MicroLink restarts cleanly.
- [ ] **Step 4:** Run tests (PASS), full `python3 -m unittest discover tests` (no regressions), `idf.py -C idf build`.
- [ ] **Step 5:** Commit.

### Task 3: MicroLink bring-up and the inbound-TCP spike (gate)

**Files:**
- Create: `idf/components/microlink` (git submodule at `216da33`)
- Modify: `idf/sdkconfig.defaults` (MicroLink README values: PSRAM malloc settings, TLS settings, `CONFIG_ML_H2_BUFFER_SIZE_KB` and `CONFIG_ML_JSON_BUFFER_SIZE_KB` in PSRAM), `esp32-birdnet-mic/TailscaleLink.cpp`, `esp32-birdnet-mic/esp32-birdnet-mic.ino` (call sites)
- Test: `tests/test_tailscale_config.py` (extend), hardware steps

**Interfaces:**
- Consumes: Task 2 store and status.
- Produces: `void tailscaleBegin()` (call in `setup()` right after the `applyMdnsSetting()` line, only if `WiFi.status() == WL_CONNECTED` or deferred until it is), `void tailscaleLoop()` (from `loop()`, cheap), `IPAddress tailscaleIp()` (0.0.0.0 when not connected).

- [ ] **Step 1: Extend tests:** `test_begin_guards`: source scan shows `tailscaleBegin` returns early with state `no_psram` when `ESP.getPsramSize() == 0` and `no_key` when no key; `test_no_restart_on_failure`: `TailscaleLink.cpp` contains no `ESP.restart` or `esp_restart`.
- [ ] **Step 2:** Run. Expected: FAIL.
- [ ] **Step 3:** Implement `tailscaleBegin/Loop/Ip` with `microlink_init` (device name = stored hostname or the mDNS hostname, `enable_derp/stun/disco = true`, `max_peers = 16`), `microlink_start`, a state callback that records state and last error. Pin MicroLink tasks to the core not used by audio capture (check `xTaskCreate` at `.ino:3083` and the I2S task affinity), at a priority below the capture task.
- [ ] **Step 4:** Tests PASS, build passes, flash.
- [ ] **Step 5 (owner action, then gate):** Ask the owner (AskUserQuestion, instructions in the question) to: create a tag such as `tag:birdnet-mic` in their tailnet ACL, generate a reusable, pre-approved, tagged auth key, install Tailscale on one client device on their tailnet, and enter the key at `http://<mic-lan-ip>/` in the Tailscale card. Then verify from that client: `GET /api/tailscale` shows `connected` with a 100.x IP; `curl http://<100.x>/api/status` returns 200; `ffmpeg -rtsp_transport tcp -i rtsp://<100.x>:8554/audio1 -t 60 -f null -` exits 0. **If inbound TCP does not reach the servers on the WireGuard netif, stop and report to the owner with the evidence before changing approach** (spec: first thing to prove).
- [ ] **Step 6:** Degrade tests on hardware: with a deliberately invalid but well-formed key the owner enters (or after clearing the key), and with the router's WAN unplugged by the owner, the LAN web UI answers within 20 s of boot and `state` shows `error`/`connecting` with `last_error` set; no reboot loop over 5 minutes (watch serial).
- [ ] **Step 7:** Commit; write `docs/debugging/2026-10-29_tailscale-spike.md` if anything had to be root-caused.

### Task 4: RTSP over the tailnet (URLs and UDP transport)

**Files:**
- Modify: `esp32-birdnet-mic/esp32-birdnet-mic.ino:3559` and `:3588` (DESCRIBE/SETUP `ip`), `/api/status` stream URLs near `:1047`
- Test: `tests/test_rtsp_parser.py` (extend)

- [ ] **Step 1: Write tests:** `test_rtsp_urls_use_socket_address`: the DESCRIBE and SETUP handlers take the address from the session's client socket (`client.localIP()`), not `WiFi.localIP()`; `test_status_has_tailnet_urls`: `/api/status` JSON includes `stream1_url_tailnet` and `stream2_url_tailnet` only when `tailscaleIp()` is non-zero (guarded by `BIRDNET_TS_FORK`).
- [ ] **Step 2:** Run. Expected: FAIL.
- [ ] **Step 3:** Implement. For RTP over UDP, confirm `WiFiUDP` sends to a 100.x client leave through the WireGuard netif (lwIP routes by destination); if they do not, answer UDP SETUP from tailnet clients with `461 Unsupported Transport` so clients fall back to TCP interleaved.
- [ ] **Step 4:** Tests PASS; build; flash. From the tailnet client: `ffmpeg -rtsp_transport udp ...` and `-rtsp_transport tcp ...` against `rtsp://<100.x>:8554/audio1` for 60 s each; LAN pulls still work.
- [ ] **Step 5:** Commit.

### Task 5: Web UI card and fork guard on upstream OTA

**Files:**
- Modify: `esp32-birdnet-mic/webui/index.html` (Tailscale card), regenerate `WebUI_gz.h` with `esp32-birdnet-mic/tools/gen_webui_gzip_header.sh`, `esp32-birdnet-mic/WebUI.cpp` (`httpOtaInstall`, OTA page text)
- Test: `tests/test_tailscale_ui.py` or a `tests/*_browser_test.js` in the existing style

- [ ] **Step 1: Write tests:**
  - `test_tailscale_card`: the card exists only when `/api/status` has `tailscale`; it has a password-type `auth_key` input that is never pre-filled, a hostname field, Save and Remove key buttons, and shows `state`, `ip`, `hostname`, `peer_path`, `last_error`.
  - `test_official_ota_pull_disabled`: under `BIRDNET_TS_FORK`, the "install from official host" button is not rendered and `POST /ota/install` returns 403 with text "This firmware is a fork with Tailscale. Install updates by file upload." File upload (`/ota/upload`) is unchanged.
- [ ] **Step 2:** Run. Expected: FAIL.
- [ ] **Step 3:** Implement, in the existing UI's style. No em-dashes or emojis in UI text.
- [ ] **Step 4:** Tests PASS; full suite; build; flash; check the card in a browser on the LAN and over the tailnet.
- [ ] **Step 5:** Commit.

### Task 6: Acceptance on hardware, then the friend's tailnet

**Files:**
- Create: `docs/tailscale.md` (owner-facing: build, flash, key setup, ACL example for the friend, known limits)
- Test: hardware procedure below, results recorded in `docs/tailscale.md`

- [ ] **Step 1:** Baseline: stock v1.36 on the Sense, 1-hour LAN pull (`ffmpeg -rtsp_transport tcp ... -t 3600 -f null -`), record `/api/audio_status` and `/api/perf_status` dropout and RTSP counters before and after.
- [ ] **Step 2:** Fork image: same 1-hour pull over the tailnet from the owner's client; record the same counters and `peer_path`. Expected: dropouts and `rtspWriteFailCount` no worse than baseline.
- [ ] **Step 3:** Leak sweep: capture serial for the whole run, `/api/logs`, `/api/status`, `/api/tailscale`, and `git log -p`; `grep -c tskey-` must be 0 everywhere (the agent runs the grep on captured files without printing matches).
- [ ] **Step 4:** Node removal: owner removes the node in the admin console; device shows `error`, LAN keeps working, no reboot loop.
- [ ] **Step 5:** Hand-off: the owner gives the friend the ACL snippet from `docs/tailscale.md`; the friend issues a tagged key; the owner enters it; BirdNET-Go on the friend's server pulls `rtsp://<100.x>:8554/audio1`. Record the result.
- [ ] **Step 6:** Commit docs; push; record the deployment in `~/GitHub/DevOps/docs/notes/` (new device, port, where it lives).
