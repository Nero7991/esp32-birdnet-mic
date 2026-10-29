# On-device Tailscale for esp32-birdnet-mic (XIAO ESP32-S3 Sense)

Date: 2026-10-29. Status: draft for owner approval.

## Intent

A Seeed XIAO ESP32-S3 Sense running esp32-birdnet-mic v1.36 sits on one network (the owner's
side). A friend's BirdNET-Go server lives on a different network, reachable only through the
friend's Tailscale tailnet. The mic must join that tailnet itself, with no router changes, no
port forwarding and no extra box at the mic's site, so BirdNET-Go can pull the mic's RTSP audio
over Tailscale.

The owner chose an on-device client over a subnet router on another host, knowing it means
maintaining a firmware fork.

## Goals

1. The S3 build joins a Tailscale tailnet with an auth key and gets a 100.x address.
2. Over the tailnet, the RTSP streams (`rtsp://<100.x>:8554/audio1`, `/audio2`) and the web UI
   (port 80) work as they do on the LAN.
3. LAN behaviour is unchanged: everything that works on the LAN today still works, with or
   without Tailscale configured.
4. Tailscale is off until an auth key is entered; a build with no key behaves exactly like
   upstream v1.36.
5. The auth key is entered in the mic's web UI and stored on the chip. It is never compiled
   into the image, logged, printed on serial, or returned by any page or API.
6. The web UI shows Tailscale status: off / connecting / connected, the 100.x address, the
   tailnet node name, and direct vs DERP path for the active peer if available.

## Non-goals

- No Tailscale on the C3, C5 or C6 builds. S3 only.
- No exit node, subnet routing, MagicDNS server, Tailscale SSH, or Funnel/Serve.
- No change to the audio pipeline, microphone profiles or BirdNET-specific behaviour.
- No auth on the web UI (upstream has none; access control is the friend's tailnet ACLs, which
  is the owner's accepted trade-off).
- Not upstreamed now. Keep the change opt-in and isolated so it could be offered upstream later.

## Anti-goals (failure even if it "works")

- The auth key appears anywhere outside the chip's NVS: source, build output, serial log, web
  log buffer, `/api/*` JSON, MQTT, or a git commit.
- Audio dropouts on the LAN stream that upstream v1.36 does not have, caused by Tailscale work
  starving the audio or RTSP tasks.
- An "install update" from the upstream OTA feed silently replaces the fork and removes
  Tailscale.
- The device boot-loops or loses its LAN web UI when the tailnet is unreachable, the key is
  wrong or expired, or the control server is down. Tailscale failure must degrade to "LAN only".

## Constraints

- Hardware: XIAO ESP32-S3 Sense, ESP32-S3 (QFN56) rev v0.2, 8 MB embedded PSRAM (octal,
  AP_3v3), 8 MB flash. Test unit MAC `ac:27:6e:a8:4c:84`, on the owner's laptop as `/dev/ttyACM2`.
- Upstream: `Sukecz/esp32-birdnet-mic` at `2f13df8` (v1.36), MIT. Arduino sketch built with
  arduino-cli 1.5.0, core `esp32:esp32` 3.3.12, libraries WiFiManager 2.0.17 and PubSubClient
  2.8 (`build-dependencies.json`), via `tools/compile_firmware.sh --fqbn ...`.
- S3 partition table (from the published `partitions-s3.bin`): two 3264 KB OTA app slots, so
  flash size is not a constraint (current app is 1.18 MB).
- The upstream S3 build has PSRAM off (the firmware reports "PSRAM 0 KB"). Upstream enables
  PSRAM only for C5 in `compile_firmware.sh`.
- Client library: MicroLink (`CamM2325/microlink`, MIT), an ESP-IDF Tailscale client
  (ts2021 control, WireGuard via `wireguard_lwip`, DERP, DISCO, STUN). Its runtime needs about
  1 MB PSRAM at peak and 85 to 116 KB internal SRAM static, plus about 42 KB of task stacks.
- Stream bandwidth is about 0.8 Mbps per client (48 kHz, 16-bit mono PCM over RTP).
- The web UI has OTA (file upload and "pull from official host"), Wi-Fi and MQTT settings, and
  no authentication.

## Decisions made (by the owner)

- On-device Tailscale, not a subnet router or ZeroTier route.
- The mic joins the friend's tailnet; the friend generates the auth key.
- Over the tailnet: RTSP and the web UI are reachable (not RTSP only).
- Fork lives at `Nero7991/esp32-birdnet-mic` (author's proposal, not objected to).
- Build system: **ESP-IDF 5.5 with arduino-esp32 as a component** (the sketch code stays Arduino),
  replacing the arduino-cli build for the fork. Reason, in the owner's words: "I want light sleep
  working and other ESP32 hardware related features as well". The project's own `sdkconfig`
  must therefore own PSRAM, power management, lwIP and mbedTLS settings.
- Development and testing use the **owner's own tailnet** with a tagged auth key the owner
  generates. The friend's tailnet is needed only for the final acceptance run with their
  BirdNET-Go.
- The friend's tailnet is **hosted Tailscale, no Tailnet Lock**. No control-server URL setting.

## Decisions made by the author (owner may override)

- MicroLink is the client. Alternatives found (`esphome-tailscale`, `lolipop-ztl`) are either
  ESPHome-specific or less documented.
- The S3 build enables PSRAM (OPI). MicroLink's large buffers go to PSRAM; audio buffers stay
  where upstream puts them unless measurement shows a need to move them.
- The "pull update from official host" OTA path is disabled in the fork (the button is hidden
  and the endpoint refuses), with a short note in the UI saying the device runs a fork. File
  upload OTA stays.
- The firmware version string identifies the fork, for example `1.36-ts.1`, so the UI and
  `/api` show which build is running.
- Expected key type: a reusable or one-off auth key, preferably tagged and pre-approved by the
  friend, so the node does not need manual approval and does not expire with a user's login.
  If MicroLink supports it, node key expiry should be disabled by the friend in their admin
  console for this node.

- Target board: the fork's image is built and tested for the **XIAO ESP32-S3 Sense only**. If
  the image boots without PSRAM (another S3 board), Tailscale stays disabled and says why in the
  UI; no other boards are tested.
- Delivery: local builds only, flashed over USB or the web UI's file-upload OTA. No fork
  releases, web flasher, or OTA feed. The upstream release tooling is left in place but not
  used for the fork.
- MicroLink's own config HTTP server and its compiled-in auth key option
  (`CONFIG_ML_TAILSCALE_AUTH_KEY`) are disabled; the key comes only from NVS.

## Later, not in this build

- Power management (CONFIG_PM_ENABLE, automatic light sleep with tickless idle), enabled and
  measured once Tailscale works. While audio is captured and streamed, the I2S/PDM driver holds
  a power-management lock and the radio stays busy, so savings are expected mainly when no
  client is pulling the stream. Measure, do not assume.

## Left to the builder

- Project layout for the ESP-IDF build (where `main/`, the sketch sources, MicroLink and the
  arduino-esp32 component live, pinned versions of each). The arduino-esp32 version must be one
  that targets ESP-IDF 5.5 and stays as close to upstream's 3.3.12 as possible, so upstream
  sketch changes merge cleanly. WiFiManager and PubSubClient are pinned to the versions in
  `build-dependencies.json`.
- How the RTSP server and web server accept connections on the WireGuard netif. Upstream binds
  `WiFiServer rtspServer(8554)` and builds RTSP `Content-Base` and RTP-Info URLs from
  `WiFi.localIP()`; over the tailnet those must use the address the client actually connected
  to. Whether RTP-over-UDP works through the tunnel or only RTSP-interleaved TCP, and what
  BirdNET-Go uses, must be checked; at least the transport BirdNET-Go uses must work.
- Task priorities and core pinning for MicroLink relative to audio capture and RTSP send.
- Web UI placement and wording for the Tailscale settings and status (follow the existing UI's
  style), and the NVS key names.
- Hostname the node registers with (default: the existing mDNS hostname).
- Tests: whatever host-side tests the repo's `tests/` style supports, plus a hardware test
  procedure. The hardware acceptance test must include: a 1-hour RTSP pull over the tailnet
  with no more dropouts than the same pull over the LAN on upstream v1.36; behaviour with a
  wrong key, with Wi-Fi up but internet down, and after the tailnet removes the node; and a
  grep of serial log, web log, `/api` output and the repo for the key.

## Open, not yet answered

- Whether MicroLink's `wireguard_lwip` netif accepts inbound TCP to a server bound to
  `INADDR_ANY` without changes. This is the first thing to prove (a spike) before the rest.
- Whether the S3 sustains WireGuard encryption plus audio capture plus RTSP at 48 kHz without
  dropouts. Unmeasured.
- Whether Arduino core 3.3.12's prebuilt ESP-IDF libraries include everything MicroLink needs
  (for example mbedTLS options, lwIP IPv4 fragmentation and reassembly). If not, the build may
  need the core's lib-builder or an ESP-IDF build with Arduino as a component.
- Whether arduino-esp32 as a component builds cleanly against ESP-IDF 5.5 with the sketch's
  `build_opt.h` flags, and which upstream release tests still apply to the new build.
- Upstream marks XIAO ESP32-S3 Sense support as experimental and untested on hardware. Audio
  problems on Sense may be upstream issues, not Tailscale ones. Baseline the stock v1.36 Sense
  audio on the LAN first.
