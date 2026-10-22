#!/usr/bin/env bash
# Apply LTO only to the two targets with tight default OTA slots.
set -euo pipefail
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
fqbn=""
args=("$@")
for ((i=0; i<${#args[@]}; i++)); do
  case "${args[i]}" in
    --fqbn|-b) fqbn="${args[i+1]:-}" ;;
    --fqbn=*) fqbn="${args[i]#--fqbn=}" ;;
  esac
done
if [[ -z "$fqbn" ]]; then
  echo "Usage: $0 --fqbn <BOARD_FQBN> [arduino-cli compile options]" >&2
  exit 2
fi
# XIAO ESP32-C5: enable its 8 MB PSRAM (audio ring and listening queue live there).
case "$fqbn" in
  esp32:esp32:XIAO_ESP32C5|esp32:esp32:XIAO_ESP32C5:*)
    if [[ "$fqbn" != *PSRAM=* ]]; then
      if [[ "$fqbn" == *:*:*:* ]]; then c5="$fqbn,PSRAM=enabled"; else c5="$fqbn:PSRAM=enabled"; fi
      for ((i=0; i<${#args[@]}; i++)); do
        case "${args[i]}" in
          --fqbn|-b) args[i+1]="$c5" ;;
          --fqbn=*) args[i]="--fqbn=$c5" ;;
        esac
      done
      set -- "${args[@]}"
      fqbn="$c5"
    fi
    ;;
esac
properties=()
case "$fqbn" in
  esp32:esp32:XIAO_ESP32C3|esp32:esp32:XIAO_ESP32C3:*|esp32:esp32:XIAO_ESP32C6|esp32:esp32:XIAO_ESP32C6:*)
    # SDK linker flags contain -fno-lto; enable LTO at both compilation and linking.
    # A dedicated build path prevents reuse of a precompiled non-LTO Arduino core.
    # Keep these objects separate from the normal S3/C5 core cache.
    build_key="${fqbn//[^a-zA-Z0-9._-]/_}"
    properties=(
      --build-path "$ROOT_DIR/esp32-birdnet-mic/build/lto-$build_key"
      --build-property compiler.c.extra_flags=-flto
      --build-property compiler.cpp.extra_flags=-flto
      --build-property 'compiler.c.elf.extra_flags=-flto -Werror=lto-type-mismatch'
    )
    ;;
esac
exec arduino-cli compile "${properties[@]}" "$@" "$ROOT_DIR/esp32-birdnet-mic"
