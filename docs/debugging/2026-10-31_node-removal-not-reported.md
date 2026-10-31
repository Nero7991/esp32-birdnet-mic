# A removed node or revoked key is not reported as rejected

## Question (2026-10-31)

Acceptance step 4: when the owner removes the mic from the tailnet, the mic should report
`state: error` with the "rejected" text, keep its LAN web UI, and not reboot-loop. Does it?

- Hardware: Seeed XIAO ESP32-S3 Sense, MAC `ac:27:6e:a8:4c:84`, LAN 192.168.97.174.
- Build: fork branch `tailscale` at `3a4ce30` (v1.36-ts.1), MicroLink `a8308b3`.
- Key: reusable, pre-approved, tagged auth key stored on the device.
- Symptom: after Remove in the admin console, `/api/tailscale` kept showing
  `"state":"connected"` with `last_error` empty for 5 minutes, while the laptop's
  `tailscale status` no longer listed the node. After the key was also revoked, the mic showed
  `reconnecting` and "Cannot reach Tailscale. Check that this network has internet access.",
  which is the wrong cause.

## Answer

MicroLink never sets `ML_STATE_ERROR` (the enum value is declared and never assigned), and it
ignores both ways the control server says no: a RegisterResponse whose `Error` field is set
(revoked or unknown key) and the plain-text body `node not found` (removed node). The removed
node also came back on its own at the next reconnect, because a reusable, pre-approved key is
still valid after the node is removed.

## Procedure

1. Remove the node in the admin console while the mic is connected; poll `/api/tailscale` and
   `/api/status` every 15 s for 2.5 minutes. Isolates what the live connection notices.
   Result: `connected`, uptime continuous, peer path flipped from direct to derp.
2. Serial capture for 110 s (by-id path, 115200). Isolates what the control connection
   receives. Result: H2 PINGs and endpoint updates (`Stream=false` MapRequests) keep going
   out; no error is logged; DERP reports `PeerGone` for the laptop.
3. Read `ml_coord.c` and `microlink.c`. `ML_STATE_ERROR` is only in the enum.
   `poll_map_update` discards the bodies of endpoint-update replies (stream 7 and up) at
   debug level.
4. Reboot the mic (`POST /api/action/reboot`) with serial captured. Isolates a fresh
   registration with the stored key. Result: registration succeeded and the node reappeared as
   `esp32mic-a84c84`, same 100.76.1.18.
5. Owner revokes the key and removes the node; reboot with serial captured. Isolates the
   server's answer to a rejected key. Result below.
6. After the fix, app-flash only (NVS and the revoked key kept), poll `/api/tailscale` every
   20 s for 3 minutes with serial captured.

## Evidence

Step 5, revoked key (MicroLink `a8308b3`):

```
I (3508) ml_coord: RegisterResponse: 242 bytes total data
I (3509) ml_coord: RegisterResponse first 32 bytes (hex): 7b 22 55 73 65 72 22 3a 7b 22 49 44 22 3a 30 2c ...
                                                          {"User":{"ID":0,"DisplayName":"" ...
I (3510) ml_coord: Fetching peers...
I (3772) ml_coord: MapResponse first 15 bytes (hex): 6e 6f 64 65 20 6e 6f 74 20 66 6f 75 6e 64 0a
                                                     node not found\n
E (3772) ml_coord: MapResponse JSON parse failed near: node not found
W (3773) ml_coord: MapRequest failed, will retry
I (3774) ml_coord: Reconnecting in 1000 ms (attempt 1)
```

`/api/tailscale` during step 5: `registering` / `reconnecting`, and after 30 s
`last_error` = "Cannot reach Tailscale. Check that this network has internet access."

Step 6, after the fix (MicroLink `1a31126`):

First fixed build (key ID replaced with `<id>` here):

```
E (3472) ml_coord: Control server rejected registration: invalid key: API key <id> not valid
E (3472) ml_coord: Registration failed
I (3473) ml_coord: Reconnecting in 60000 ms (attempt 1)
E (64198) ml_coord: Control server rejected registration: invalid key: API key <id> not valid
I (64199) ml_coord: Reconnecting in 60000 ms (attempt 2)
```

Second build, with the redaction:

```
E (3516) ml_coord: Control server rejected registration: invalid key
```

`/api/tailscale` for 3 minutes, uptime 19 s to 3 m 0 s, no reboot:

```
{"available":true,"has_key":true,"state":"error","ip":"","hostname":"","peer_path":"",
 "last_error":"The Tailscale control server rejected this device. Check the auth key and that the node was not removed."}
```

## Fix (2026-10-31)

MicroLink fork `Nero7991/microlink` branch `lwip-core-lock`, commit `1a31126`:

- `ml_ctrl_reject.c`: `ml_ctrl_register_error()` (non-empty `Error`) and
  `ml_ctrl_body_node_not_found()`. Pure, host-tested against ESP-IDF's cJSON.
- `do_register`, `do_fetch_peers` and the endpoint-update replies in `poll_map_update` set
  `ml->ctrl_rejected`. It clears when the node reaches CONNECTED.
- `microlink_get_state()` reports `ML_STATE_ERROR` while the flag is set, so the status does
  not flicker through connecting/registering between retries.
- Retries every 60 s while rejected (`ML_CTRL_REJECTED_RETRY_MS`), instead of 1 to 30 s.
- The server's text is logged after redaction: it echoes the key ID
  (`invalid key: API key <id> not valid`), so only the part before `:` is kept when the text
  mentions an API key or `tskey-`.

## Measured and rejected (do not retry)

- **Removing the node is not a way to cut a mic off while it holds a reusable key.** It
  re-registered by itself at the next reconnect (step 4). Revoke the key, or use Remove key
  on the mic.
- **The live control connection is not closed by the server on removal.** PINGs and endpoint
  updates kept succeeding for at least 5 minutes after the delete (step 2), so a
  connection-lost check alone would not notice.

## Measurement traps

- The server echoes the key ID in its error text. The first fixed build logged it to serial
  (the key was already revoked). Grep serial captures for the ID, not only for `tskey-`.
- `tailscale status` on another node drops the removed node at once; that is not evidence of
  what the device believes.

## Open, not yet answered

- Whether the endpoint-update reply after a live removal is actually `node not found` (the
  new check in `poll_map_update`). Not yet seen on hardware; needs a removal while connected
  with the fixed build.
- Whether a single-use (non-reusable) key survives reconnects. If MicroLink resends the used
  key on every registration, a single-use key would lock the mic out after its first
  reconnect.

## Update (2026-10-31): both open items answered

Build `9bbba97` (MicroLink `1a31126`), new key: single-use (Reusable off), pre-approved, tagged.

- **A used single-use key survives reconnects.** Two reboots each re-registered
  (`RegisterResponse: 193 bytes`, `Our VPN IP: 100.76.1.18`) and reached `connected`. The
  server recognises the existing node key, so the spent key is not a problem.
- **Live removal is detected.** Machine removed in the console while connected, no key
  revoked:

```
E (116495) ml_coord: Control server: node not found (removed from the tailnet)
W (116496) ml_coord: Long-poll connection lost
I (116496) ml_coord: Reconnecting in 60000 ms (attempt 1)
E (177238) ml_coord: Control server rejected registration: invalid key
I (177240) ml_coord: Reconnecting in 60000 ms (attempt 2)
```

  `/api/tailscale` showed `state` `error` with the rejected text from the first poll after
  the removal, for 2 minutes, no reboot. With a single-use key the removal sticks, because the
  spent key cannot register a new node. Use single-use keys for deployed mics.

Still open: `ip` keeps the last tailnet address while in `error` (cosmetic; `peer_path` is
already cleared).

Measurement trap, again: `pkill -f <pattern>` matched its own shell (exit 144). Kill by PID.
