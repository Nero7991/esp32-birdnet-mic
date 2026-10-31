# Audio drops when BirdNET-Go pulls the stream across the internet

## Question (2026-10-31)

Why does a BirdNET-Go on a friend's network lose audio from the mic over the tailnet, when the
one-hour tailnet soak from the same LAN had no ring-buffer drops?

- Hardware: Seeed XIAO ESP32-S3 Sense, PDM mic, 48 kHz 16-bit mono, buffer 512 samples
  (10.7 ms per block). On the owner's LAN behind the ASUS media bridge.
- Build: fork `d2d51bd` (v1.36-ts.1), MicroLink `1a31126`, `CONFIG_LWIP_TCP_SND_BUF_DEFAULT=5760`.
- Client: BirdNET-Go on the friend's tailnet (100.92.18.45, ffmpeg Lavf 61.7.103), RTSP
  over TCP, tailnet path direct (public endpoint 72.198.80.192, Cox).
- Symptom: `rb_drops` +763/min (13.6% of 5625 blocks/min), `rtsp_write_stalls` +1594/min,
  `rb_flushes` climbing. The soak (`docs/tailscale.md`, Test results) had 0 drops in an hour.

## Answer

Two limits, both invisible on the LAN:

1. **TCP send buffer / RTT.** A sender can have at most one send buffer of unacknowledged data
   per round trip. 5760 bytes at ~62 ms caps throughput at ~93 KB/s; the stream needs ~99 KB/s
   with RTP and interleave headers. Fixed: `CONFIG_LWIP_TCP_SND_BUF_DEFAULT=14400`.
2. **Ring buffer shorter than a WAN stall.** On the S3 the audio ring is in internal RAM and
   the heap margin cut it to 45 KB (0.47 s). TCP loss recovery across the internet stalls for
   longer, so the ring overflowed and was then flushed. Fixed: fork builds put the ring in
   PSRAM (as upstream does on the C5), sized to 4 s (384 KB).

## Procedure

1. Counter deltas from `/api/audio_status` over 60 s with the friend's stream running. Measures
   loss as a fraction of blocks (`rb_drops` / (`rb_chunks` + `rb_drops`)).
2. Code read: one `rb_drops` increment is one 512-sample block that the producer could not put
   in the ring. With a single client, `streamAudio` stops consuming the ring while that
   client's writer is pending, so the ring is the jitter buffer; after an overflow
   `shouldFlushRecoveredBacklog` discards the stale backlog once (`rb_flushes`).
3. RTT estimate: ICMP to the friend's public IP is blocked; `tracepath -n 72.198.80.192` from
   the laptop (same uplink as the mic) reached the last Cox router at 58.5 ms. The mic's own
   DISCO RTT log is at INFO and the fork sets `ml_wg_mgr` to WARN, so it was not available.
4. Arithmetic: 5760 B / 0.062 s = 93 KB/s against ~99 KB/s needed. Prediction: steady drops
   at a few percent to over ten percent. Matched.
5. Change 1 only (send buffer), app-flash, wait for BirdNET-Go to reconnect, per-minute
   deltas for 6 minutes.
6. Change 2 (PSRAM ring, 4 s), same measurement for 5 minutes, then 30 minutes.

## Evidence

| Build | `rb_drops`/min, steady | Blocks lost | Stalls/min | `rb_capacity_bytes` | Internal free heap |
|---|---|---|---|---|---|
| SND_BUF 5760, internal ring | 763 | 13.6% | 1594 | 45056 | 59 KB |
| SND_BUF 14400, internal ring | 40 to 150 (mean ~85) | ~1.5% | ~265 | 45056 | 59 KB |
| SND_BUF 14400, PSRAM ring 4 s | 0 for 4 min after start | 0 | ~375 | 384000 (PSRAM) | 66 KB |

PSRAM ring, per-minute samples (`rb_drops rb_flushes stalls capacity in_psram chunks`):

```
18:00:30 51 0 73 384000 True 784
18:01:31 82 2 429 384000 True 6385
18:02:31 82 2 866 384000 True 12013
18:03:31 82 2 1221 384000 True 17654
18:04:31 82 2 1596 384000 True 23283
```

The 82 drops and 2 flushes are all in the first minute, while BirdNET-Go made several short
probe connections before it settled (log: connects at 22:00:03, :07, :17, streaming at :22).

## Measured and rejected (do not retry)

- **Send buffer alone is not enough.** 14400 bytes removed the throughput cap (~232 KB/s at
  62 ms) but drops continued at ~1.5% of blocks, because individual stalls still exceeded the
  0.47 s ring.
- **Not the WireGuard path or the device CPU.** The same build streamed one hour over the
  tailnet on the LAN with 0 drops; the difference is the RTT and loss of the WAN path.

## Measurement traps

- `/api/audio` does not exist; the counters are in `/api/audio_status`, which is gzip-encoded
  (`curl --compressed`).
- The device log clock is UTC; the laptop is EDT. "Nothing logged for 17 minutes" meant the
  stream was steady, not stuck.
- `rtsp_write_stalls` counts stall episodes, not lost audio. Loss is `rb_drops` plus whatever
  `rb_flushes` discarded.

## Open, not yet answered

- With two simultaneous clients, a client whose writer is pending is skipped and loses those
  blocks without any counter (`streamAudio` sends only to clients that are not pending).
  BirdNET-Go briefly opened a second session (slot 1) once; not measured.
- Whether a DERP-relayed path (higher RTT) stays clean; 14400 bytes covers up to ~150 ms.
- The startup drops during BirdNET-Go's probe connections.
