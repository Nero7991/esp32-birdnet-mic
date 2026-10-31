# Tailscale on the XIAO ESP32-S3 Sense (fork)

This fork adds an on-device Tailscale client to esp32-birdnet-mic, so the mic can join a
tailnet and serve its RTSP streams and web UI on a 100.x address. No router changes, port
forwarding or extra hardware are needed at the mic's site.

Only the XIAO ESP32-S3 Sense build is supported and tested. Upstream marks the Sense
microphone profile as experimental.

## What is different from upstream

| | Upstream v1.36 | This fork (v1.36-ts.1) |
|---|---|---|
| Build | arduino-cli | ESP-IDF 5.5 + arduino-esp32 3.3.12 as a component (`idf/`) |
| PSRAM | off on S3 | on (8 MB) |
| Tailscale | no | MicroLink client, off until an auth key is stored |
| Update from the official host | yes | refused (it would replace the fork); file upload still works |

MicroLink comes from the fork `Nero7991/microlink`, branch `lwip-core-lock`, which fixes
crashes under sustained traffic and reports a revoked key or removed node as rejected. See `docs/debugging/2026-10-30_tailnet-rtsp-panics.md`.

## Build and flash

    git clone --recurse-submodules -b tailscale git@github.com:Nero7991/esp32-birdnet-mic.git
    cd esp32-birdnet-mic
    . ~/esp/v5.5/esp-idf/export.sh
    idf.py -C idf set-target esp32s3        # first time only
    idf.py -C idf build
    idf.py -C idf -p /dev/serial/by-id/<board> flash

`flash` keeps the saved Wi-Fi, settings and the Tailscale key. To update a running device
without USB, upload `idf/build/birdnet-mic-ts.bin` on the device's Firmware update page.

The XIAO ESP32-S3 needs its external antenna attached (U.FL connector). Without it the
setup hotspot only reaches a few centimetres.

## Join a tailnet

On the tailnet the mic should join (yours, or a friend's):

1. Access controls: give a tag an owner, for example
   `"tagOwners": {"tag:birdnet-mic": ["autogroup:admin"]}`.
2. Settings > Keys > Generate auth key: Reusable, Pre-approved, Tags = `tag:birdnet-mic`,
   Ephemeral off. Tagged devices do not expire, so the node stays joined after the key's own
   expiry date.
3. Enter the key on the mic, either in the web UI (Tailscale card, Save key) or from a
   terminal on the same network:

       idf/tools/set-tailscale-key.sh <mic-lan-ip>

   The helper reads the key at a hidden prompt and never shows or logs it. The device
   restarts and joins within a few seconds.

Allow the server that pulls the stream to reach the mic. A grant in the tailnet policy:

    {"src": ["<birdnet-go host or group>"], "dst": ["tag:birdnet-mic"], "ip": ["tcp:8554", "tcp:80"]}

`tcp:80` is the web UI. It has no password, so anyone the policy allows can change the mic's
settings and upload firmware. Leave it out if only the stream is needed.

## Use it

- Stream: `rtsp://<100.x>:8554/audio1` (and `/audio2` if stream 2 is on). The Tailscale card
  and `/api/status` (`stream1_url_tailnet`) show the exact URL.
- BirdNET-Go uses RTSP over TCP; the stream target "BirdNET-Go" (the default) forces TCP.
  RTP over UDP across the tailnet also works (stream target "BirdNET-Pi").
- Status: Tailscale card, or `GET /api/tailscale`:
  `state`, `ip`, `hostname`, `peer_path` (`direct` or `derp`), `last_error`.

## When it is not connected

| `last_error` | Meaning |
|---|---|
| Cannot reach Tailscale. Check that this network has internet access. | Not connected for over 30 s: no internet, or the control server is down. The device keeps retrying. |
| The Tailscale control server rejected this device. ... | Wrong key, or the node was removed from the tailnet. Enter a new key. |

While rejected, the device retries once a minute. Tailscale problems never restart the
device; the LAN web UI and LAN streams keep working.

To take the mic off a tailnet, use Remove key in the card, or revoke its auth key in the
admin console (Settings > Keys) and then remove the machine. Removing the machine alone does
not last: with a reusable key the mic registers again the next time it reconnects.

## Test results

Acceptance run on 2026-10-30 and 2026-10-31. Device: XIAO ESP32-S3 Sense (rev v0.2, 8 MB
PSRAM), PDM mic, stream target BirdNET-Go (RTSP over TCP). Client: a Linux laptop on the same
LAN, ffmpeg 7.0.2, `-t 3600 -f null`. The tailnet path was direct (no DERP relay).

One-hour pulls, counter deltas from `/api/audio`:

| | Stock v1.36, LAN | Fork v1.36-ts.1, tailnet |
|---|---|---|
| ffmpeg exit | 0 | 0 |
| Audio received | 3600.0 s (172,801,024 samples) | 3600.0 s (172,801,024 samples) |
| Reboots | 0 | 0 (uptime 15 s to 1 h 0 m 34 s) |
| `rb_drops` | 312 | 0 |
| `rtsp_write_stalls` | 309 | 1197 |
| `rtsp_write_failures` | 0 | 1 (at teardown, when the client closed at 3600 s) |
| `dma_overruns_total`, `i2s_error_count` | 0, 0 | 0, 0 |
| `rtsp_client_disconnects` | 0 | 0 |
| Free heap after | | 81 KB internal (minimum 72 KB) |

Write stalls are higher over the tailnet (WireGuard adds per-packet work), but no audio was
lost: the ring buffer never overflowed. Neither run had a panic, assert or reset in the serial log.

Other checks:

- Web UI over the tailnet: HTTP 200, about 50 ms.
- RTP over UDP across the tailnet (stream target BirdNET-Pi): 30 s pull, complete.
- Internet blocked at the router for 5 minutes: LAN web UI kept working, no reboot,
  `last_error` showed the "Cannot reach Tailscale" text after 30 s; reconnected about 9 s after
  the block was lifted.
- The official-host update is refused with HTTP 403; file upload still works.
- Revoked key: `state` `error` with the "rejected" text, steady across retries, no reboot
  (see `docs/debugging/2026-10-31_node-removal-not-reported.md`).
- No auth-key text in the one-hour serial log, `/api/logs`, `/api/status`, `/api/tailscale` or
  the git history (only the obviously fake test fixture).
