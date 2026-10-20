# Release validation

Firmware **1.31** includes all four XIAO board images. Maintainer hardware validation
primarily covers C6 with an I2S microphone; physical PDM validation and long-running
C5 PDM testing of the integrated image remain pending.

## Release notes

See the [current changelog](esp32-birdnet-mic/CHANGELOG.md#131---2026-10-20).
Previous release records are retained in `release-notes/`.

## Build and local checks

`build-dependencies.json` pins Arduino CLI, the ESP32 core and libraries. CI installs
those versions; the local `scripts/build_all_firmware.sh` checks them before building.
Dependency upgrades are explicit changes to that file and require all four board builds.

```sh
python3 tools/check_build_dependencies.py
python3 -m unittest discover -s tests -p 'test_*.py'
node tests/webui_health_test.js
node tests/thermal_recovery_ui_test.js
# Browser tests additionally require playwright-core and Chromium.
g++ -std=c++11 -Wall -Wextra -Werror tests/discovery_progress_test.cpp -o /tmp/discovery-test
/tmp/discovery-test
g++ -std=c++11 -Wall -Wextra -Werror -Itests/stubs tests/discovery_client_test.cpp -o /tmp/discovery-client-test
/tmp/discovery-client-test
g++ -std=c++11 -Wall -Wextra -Werror tests/time_zone_schedule_test.cpp -o /tmp/time-zone-schedule-test
/tmp/time-zone-schedule-test
g++ -std=c++17 -Wall -Wextra -Werror -Itests/wifi_ap_stubs tests/wifi_ap_test.cpp esp32-birdnet-mic/WifiAp.cpp -o /tmp/wifi-ap-test
/tmp/wifi-ap-test
scripts/build_all_firmware.sh
python3 tests/validate_ota_contract.py
```

After changing `esp32-birdnet-mic/webui/index.html`, regenerate the embedded header
using `esp32-birdnet-mic/tools/gen_webui_gzip_header.sh` before building.

## Publication acceptance check

After committing the reviewed release files, run `scripts/publish_release.sh`.
The maintainer authorizes this complete workflow for requested firmware changes
after validation, unless a task explicitly stops at review, testing, or preparation.
The script pushes the GitHub commit and tag, publishes all four board asset sets,
deploys the public web/OTA feed, and verifies that both destinations agree.
The deployment helper checks the public web and performs the final acceptance
check. If a step fails, inspect which destinations were already updated before
retrying; uploads and deployments are not rolled back automatically.

The read-only acceptance check can also be rerun independently:

```sh
python3 tools/verify_published_release.py 1.31
```

The check downloads public artifacts over the actual HTTP OTA route and compares all
31 assets against GitHub SHA-256 digests (or downloads GitHub assets when the digest
is absent). It checks the advertised version, four-board manifest, versioned/stable
app images and C6 aliases. It never compares against a fresh local rebuild: build
timestamps can change binaries without changing the firmware version.

A missing release, stale feed, missing board or mismatched file fails the check.
This validates distribution, not successful installation on a physical device.

## Hardware acceptance

- Confirm the installed version and fresh uptime after an authorized OTA.
- Keep streaming while requesting discovery; all 32 entities should publish in
  about 66 seconds, including incremental removal of legacy retained topics.
- With an authorized broker interruption, confirm reconnect and discovery completion;
  after recovery, discovery errors must clear without hiding a state-publish failure.
- Confirm the last stream stop remains visible after reconnect. Check stream identity,
  client TEARDOWN versus write failures, and totals across concurrent clients.
- Check actual received audio and concurrent-client responsiveness independently
  of uptime and device-side packet counters.
