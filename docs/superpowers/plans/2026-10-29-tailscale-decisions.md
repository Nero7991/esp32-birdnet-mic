# Tailscale fork: decisions, review results and open items

Companion to `2026-10-29-tailscale.md` (plan) and `../specs/2026-10-29-tailscale-design.md`
(spec). Records the calls made during execution where the plan was silent or wrong, the final
review's findings, and what was left open. Written 2026-10-31, branch `tailscale` at `7aa721f`.

## Rulings made during execution

Each: what was decided, why, and what it costs if it was the wrong call.

| # | Task | Ruling | Why | Cost if wrong |
|---|---|---|---|---|
| 1 | 1 | Libraries live in `idf/libs/` and are compiled by `main`, not as `idf/components/` | PubSubClient has no ESP-IDF CMakeLists; WiFiManager's would register its own component | A path move |
| 2 | 1 | Core built with the generic `esp32s3` variant; the XIAO `pins_arduino.h` (3.3.12) is first on `main`'s include path | The registry arduino-esp32 has no XIAO variants; the core differs only in Wire defaults and USB VID, which the sketch does not use | Pin defaults differ if the sketch starts using Wire |
| 3 | 1 | `idf/tools/ino_to_cpp.py` generates prototypes and moves default arguments into them (tested by `tests/test_ino_to_cpp.py`) | A plain C++ build of the `.ino` lacks the Arduino builder's prototypes | A converter edge case breaks a future upstream merge |
| 4 | 1 | `-Wno-error=format` for `main` | WiFiManager 2.0.17 fails ESP-IDF's `-Werror=format`; Arduino builds do not use `-Werror` | None at runtime |
| 5 | 2 | Tailscale status goes in `httpStatus` (`/api/status`), not the `.ino` state JSON the plan named | That JSON is `mqttBuildStateJson`, published over MQTT; a test asserts MQTT carries no tailscale data | None |
| 6 | 3 | MicroLink task priorities and cores left at upstream values (`net_io`/`wg_mgr` priority 7) | Changing them needs a MicroLink patch; the Task 6 soak decides | Audio dropouts found late. The one-hour soak showed none |
| 7 | 3 | MicroLink submodule at `idf/libs/microlink`, `EXTRA_COMPONENT_DIRS libs/microlink/components` | Consistent with `libs/`; the repo nests its components | A path move |
| 8 | 3 | Keys are entered with `idf/tools/set-tailscale-key.sh` (hidden prompt, key on stdin to curl) or the web card | Keeps the key out of argv, shell history and the agent's context | None |
| 9 | 3 | MicroLink forked to `Nero7991/microlink` branch `lwip-core-lock` | Upstream MicroLink calls lwIP outside the tcpip thread and asserts under a sustained stream (`docs/debugging/2026-10-30_tailnet-rtsp-panics.md`) | A fork to maintain against upstream MicroLink |
| 10 | 3 | `CONFIG_LWIP_CHECK_THREAD_SAFETY=y` kept on | A missed lock crashes loudly instead of corrupting state rarely | An Arduino-core raw lwIP call without the lock would crash; none seen |
| 11 | 3 | Added `tailscaleProblem` (last error after 30 s unreachable) and `tailscalePeerPath` (path only while connected) | The internet-blocked test showed an empty `last_error` and a stale "derp" path | None |
| 12 | 3 | The invalid-key check was folded into the Task 6 node-removal test | Avoids making the owner enter a key twice | Wrong-key message unverified until Task 6. It was verified there |
| 13 | 4 | `rtspLocalIp(session)` is not fork-only | It returns the same LAN address as before for LAN clients and is correct for any second interface | A 7-line conflict on upstream merges |
| 14 | 4 | Tailnet stream URLs added to `/api/status`, not the MQTT JSON | Same as ruling 5 | None |
| 15 | 5 | Upstream update pulls disabled by `FW_OTA_ARTIFACT_STR=""` plus a 403 in `httpOtaInstall` | Upstream's own "no artifact" path; one switch covers URL, version check and `ota_auto_supported` | None |
| 16 | 5 | `tests/ui_structure_browser_test.js` counts `details.card:not([hidden])` | The fork card is hidden on upstream builds, so the visible count stays 9 | A one-line conflict on upstream merges |
| 17 | 6 | A reusable key re-registers the node after it is removed; documented rather than changed in MicroLink | Matches how the key is stored; the guide now recommends single-use keys, with which removal sticks | An owner removes a mic with a reusable key and it comes back |
| 18 | Final | `tests/test_c5_psram.py` updated to the fork's PSRAM gate | `9ff5a80` widened the C5-only gate for fork builds and broke this test; only the tailscale tests had been run. Upstream builds stay C5-only | None; intent preserved |

## Final review (fresh reviewer, 2026-10-31)

No Critical findings. Three Important, six Minor.

| Finding | Grade | Outcome |
|---|---|---|
| Factory reset kept the auth key and node identity, so a mic given away rejoins the owner's tailnet | Important | Fixed, `b8d794f`: `resetToDefaultSettings` calls `tailscaleClearKey()`. Test `test_factory_reset_leaves_the_tailnet` |
| Upload page suggested upstream's `firmware-app-s3.bin` and accepted any image, which would strand a remote mic | Important | Fixed, `b8d794f`: first chunk must be ESP-IDF project `birdnet-mic-ts`. Tests `test_fork_image_check`, `test_upload_rejects_other_images` |
| Internal-RAM headroom not measured on HEAD after `CONFIG_LWIP_TCP_SND_BUF_DEFAULT=14400` (all TCP pcbs, internal pbufs) and the 4 s PSRAM ring. Last measured 66 KB free, below upstream's 80 KB margin | Important | **Open.** Needs a one-hour soak on HEAD with both streams and the web UI open, recording minimum free internal heap. If it falls below ~40 KB: `CONFIG_SPIRAM_TRY_ALLOCATE_WIFI_LWIP=y`, or a send buffer of ~11 KB |

As of `7aa721f` the two fixes are built but **not flashed**; the mic runs `9ff5a80`.

## Deferred minors

Not fixed. Each line is the finding and the suggested fix.

1. `TailscaleLink.cpp`: `tailscaleSaveKey`/`tailscaleClearKey` call `microlink_factory_reset()` while
   MicroLink's tasks run. `ml_peer_nvs_deinit()` can free its table while `ml_wg_mgr` is inside
   `ml_peer_nvs_save`, in the ~700 ms before the reboot. Worst case a panic instead of a clean
   restart. Fix: store an "erase pending" flag in `tsnet` and erase in `tailscaleLoad()` before
   `microlink_init`.
2. `/api/tailscale` `ip` and `stream1_url_tailnet` keep the last 100.x address while `state` is
   `error`. Fix: clear both unless the state is connected.
3. MicroLink `ml_wg_mgr_peer_is_up` reads `dev->peers[...]` after unlocking, racing
   `wireguardif_remove_peer`. Fix: read under the lock.
4. `wireguardif.c` still `printf`s handshake packets inside `wireguardif_network_rx`, under the
   core lock. With USB-JTAG console and no host attached the write can block on the TX timeout
   while every network task waits. Fix: `ESP_LOGD`.
5. Test quality: the `TailscaleLink`, MicroLink wiring, PSRAM-ring and send-buffer tests are
   source-text regexes, not behaviour. `test_microlink_calls_lwip_under_core_lock` covers only
   `wireguardif_*` calls (not `netif_*`, `udp_*`, or reads of `dev->` after unlock), and its
   "unlock followed by return" rule would pass an early return from the middle of a region.
6. Documentation: `docs/tailscale.md` cites `/api/audio` (the endpoint is `/api/audio_status`,
   gzip-encoded); its results table describes the build before `d8b71a5`; the access-rule advice
   should mention that the default allow-all policy also exposes ArduinoOTA on 3232.

## Declined to judge by the reviewer

- Wi-Fi drop and reconnect, and the recovery-AP switch, while MicroLink runs. Not tested.
- CPU frequency below 160 MHz (the UI allows it) with WireGuard. Not measured.
- Rarely used arduino-esp32 3.3.12 paths calling lwIP without the core lock and tripping
  `LWIP_CHECK_THREAD_SAFETY`. Needs a core audit.
- No web UI authentication over the tailnet, and the auth key staying in NVS: both accepted by
  the spec.
- `microlink_start` sets `ML_EVT_WIFI_CONNECTED` unconditionally; correct only if retries recover
  when the pinned access point is down at boot. Not tested.

## Other open items

- Two simultaneous RTSP clients: a client whose writer is pending is skipped and loses blocks
  without any counter (`docs/debugging/2026-10-31_remote-rtsp-drops.md`).
- DERP-relayed path not soaked; the 14400-byte send buffer covers RTT up to ~150 ms.
- Drops during BirdNET-Go's probe connections at stream start (82 blocks, 2 flushes).
- Battery runtime not measured. Light sleep, the reason for the ESP-IDF build, not attempted.
