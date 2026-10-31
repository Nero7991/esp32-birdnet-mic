# RTSP over the tailnet panics the S3 within seconds

## Question (2026-10-30)

Why does an RTSP pull over Tailscale stop after a few seconds, when the same pull over the
LAN runs for minutes?

- Hardware: Seeed XIAO ESP32-S3 Sense (ESP32-S3 rev v0.2, 8 MB octal PSRAM), MAC
  `ac:27:6e:a8:4c:84`, LAN 192.168.97.174, tailnet 100.76.1.18.
- Build: fork branch `tailscale` at `c3bfb1e` (v1.36-ts.1), ESP-IDF v5.5.4-1035,
  arduino-esp32 3.3.12 component, MicroLink `216da33`.
- Client: orenslab-dell (100.112.112.63), ffmpeg 7.0.2 static,
  `ffmpeg -rtsp_transport tcp -i rtsp://100.76.1.18:8554/audio1 -t 60 -f null -`.
- Symptom: the web UI over the tailnet works (`/api/status` 200 in 47 ms, direct path). RTSP
  over the tailnet delivers 2.75 s of audio, then ffmpeg stalls forever. `/api/logs` then shows
  `Boot reason: panic`. Over the LAN, the same pull runs 60 s and exits 0.

## Answer

Two separate faults, found one after the other:

1. **Stack overflow in `tiT`** (the lwIP tcpip thread). WireGuard encrypts outgoing packets in
   that thread, and MicroLink's `ml_wg_mgr` logs every packet at INFO from it. The ESP-IDF
   default stack is 3072 bytes. Fixed by `CONFIG_LWIP_TCPIP_TASK_STACK_SIZE=6144` and
   `esp_log_level_set("ml_wg_mgr", ESP_LOG_WARN)`.
2. **lwIP thread-safety violation in MicroLink.** `ml_wg_mgr_task` calls
   `wireguardif_network_rx()`, which runs `ip_input()` and `tcp_input()`, from its own task. It
   does this while the tcpip thread is also processing writes on the same RTSP socket.
   lwIP's core is not thread-safe, and the race trips
   `assert lwip_netconn_do_writemore api_msg.c:1738 (offset < len)`. The same task also calls
   `wireguardif_periodic`, `wireguardif_add_peer/remove_peer/connect/update_endpoint` and
   netif functions without the core lock. Low-rate traffic (the web UI) rarely hits it; a
   48 kHz stream hits it within seconds.

## Procedure

1. Web UI over the tailnet: `curl http://100.76.1.18/api/status`. This isolates inbound TCP on
   the WireGuard netif from sustained traffic. Result: 200, 47 ms, `tailscale status` shows
   `direct 192.168.97.174:51820`. Inbound TCP works.
2. RTSP over the tailnet with `-loglevel debug` and a hard kill (`timeout -s KILL`).
   Result: DESCRIBE, SETUP and PLAY succeed; samples flow until `time=00:00:02.75`, then
   nothing. So the handshake path is fine and the failure needs sustained data.
3. `/api/logs` over the LAN: `Boot reason: panic`. The device crashes; ffmpeg's stall is a
   dead TCP connection, not a protocol problem.
4. Serial capture (`/dev/serial/by-id/...AC:27:6E:A8:4C:84-if00`, 115200) started before the
   pull, then the pull. The panic text was captured and decoded with
   `xtensa-esp32s3-elf-addr2line -pfiaC -e build/birdnet-mic-ts.elf <addresses>`.
5. After fix 1, the same procedure: the crash moved to the lwIP assert (fault 2).

## Evidence

Fault 1 (before the stack change):

```
I (82132) ml_wg_mgr: WG UDP TX: 1456 bytes -> 100.112.112.63:41641 type=4
***ERROR*** A stack overflow in task tiT has been detected.
0x4038124a: vApplicationStackOverflowHook at .../freertos/.../port.c:573
0x420eb1e6: __swrite at .../newlib/libc/stdio/stdio.c:94
CONFIG_LWIP_TCPIP_TASK_STACK_SIZE=3072
```

Fault 2 (after `CONFIG_LWIP_TCPIP_TASK_STACK_SIZE=6144`, pull reached 3.65 s):

```
assert failed: lwip_netconn_do_writemore /IDF/components/lwip/lwip/src/api/api_msg.c:1738 (conn->current_msg->msg.w.offset < conn->current_msg->msg.w.len)
0x42083432: lwip_netconn_do_writemore at .../lwip/src/api/api_msg.c:1738
0x420838b1: sent_tcp at .../lwip/src/api/api_msg.c:420
0x420743ce: tcp_input at .../lwip/src/core/tcp_in.c:479
0x420796fe: ip4_input at .../lwip/src/core/ipv4/ip4.c:786
0x4205c069: wireguardif_process_data_message at .../wireguard_lwip/src/wireguardif.c:542
0x4205c3d5: wireguardif_network_rx at .../wireguard_lwip/src/wireguardif.c:851
0x420388a9: process_wg_packet at .../microlink/src/ml_wg_mgr.c:1187
0x42038ab2: ml_wg_mgr_task at .../microlink/src/ml_wg_mgr.c:1611
# CONFIG_LWIP_TCPIP_CORE_LOCKING is not set
```

## Measured and rejected (do not retry)

- **Not the RTSP URLs** (`Content-Base` built from `WiFi.localIP()`). ffmpeg completed SETUP
  and PLAY and received audio, so the LAN address in the reply was not what stopped it.
  (It is still wrong for remote clients and is fixed separately in Task 4.)
- **Not inbound TCP on the WireGuard netif.** The web UI works over the tailnet.
- **Not a DERP-only issue.** The path was direct (`direct 192.168.97.174:51820`).
- **MicroLink's zero-copy mode (`CONFIG_ML_ZERO_COPY_WG`) is not a complete fix.** It moves
  direct-path WireGuard input into the tcpip thread, but DERP-relayed packets still go through
  `process_wg_packet()` in `ml_wg_mgr_task`, and the periodic/peer calls are unchanged.

## Measurement traps

- When the device panics mid-stream, ffmpeg does not error out; it waits forever, and
  `timeout` with SIGTERM did not stop it. Use `timeout -s KILL`.
- A serial capture can miss the panic text: the USB-JTAG port drops during the reset. The
  first capture got it, the second only got the reboot. `/api/logs` `Boot reason` is the
  reliable crash indicator; repeat the capture to get the trace.
- The S3 moved from `/dev/ttyACM2` to `/dev/ttyACM1` after a replug. Use the by-id path.
- `pgrep -f`/`pkill -f` with a pattern that appears in the calling shell's own command line
  matches that shell (exit 144). Kill by PID.

## Fix (2026-10-30)

- `idf/sdkconfig.defaults`: `CONFIG_LWIP_TCPIP_TASK_STACK_SIZE=6144`,
  `CONFIG_LWIP_TCPIP_CORE_LOCKING=y`, `CONFIG_LWIP_CHECK_THREAD_SAFETY=y` (asserts on any raw
  lwIP call made without the lock, so a missed call site crashes loudly in testing instead of
  corrupting state rarely).
- MicroLink fork `Nero7991/microlink` branch `lwip-core-lock` (`a8308b3`, on upstream
  `216da33`): every `wireguardif_*` and netif call in `ml_wg_mgr.c` holds `LOCK_TCPIP_CORE()`;
  `ml_lwip_lock.h` refuses to build without core locking. The per-data-packet `[WG_RX]` printf
  in `wireguardif.c` is behind `WG_DEBUG_LOGGING`.
- `TailscaleLink.cpp`: `esp_log_level_set("ml_wg_mgr", ESP_LOG_WARN)`.
- Static test `test_microlink_calls_lwip_under_core_lock` fails if a `wireguardif_` call in
  `ml_wg_mgr.c` is outside a locked region.

Verified after the fix: a 60 s RTSP pull over the tailnet exits 0 (68 s wall time including
setup), followed by a 60 s LAN pull, exit 0, with no reboot (uptime continuous).
The locked regions contain no socket calls or blocking waits; the DERP output callback
queues with timeout 0, so it cannot deadlock against the DERP sender.

## Open, not yet answered

- Whether WireGuard on the S3 keeps up with the stream for an hour without more audio
  dropouts than the LAN (Task 6).
- Whether the core lock held during `wireguardif_periodic` (handshake crypto) delays the
  RTSP sender enough to matter. Not measured.
- A one-off 20 s delay before the first tailnet pull started streaming was not reproduced
  (`ffprobe` over the tailnet then took 0.07 s). Cause unknown.
