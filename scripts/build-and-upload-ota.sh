#!/usr/bin/env bash

set -euo pipefail

project_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
config_file="${BINARY_LOAD_LIFTER_OTA_ENV_FILE:-${project_dir}/.binary-load-lifter-ota.env}"
firmware_file="${project_dir}/.pio/build/esp32dev/firmware.bin"
dry_run=false

if [[ "${1:-}" == "--dry-run" ]]; then
  dry_run=true
elif [[ -n "${1:-}" ]]; then
  echo "Usage: $0 [--dry-run]" >&2
  exit 2
fi

if [[ ! -f "${config_file}" ]]; then
  echo "Missing ${config_file}. Copy .binary-load-lifter-ota.env.example to .binary-load-lifter-ota.env and add the API token." >&2
  exit 2
fi

api_token=""
device_host="binary-load-lifter.local"
while IFS='=' read -r key value || [[ -n "${key:-}" ]]; do
  key="${key%$'\r'}"
  value="${value%$'\r'}"
  [[ -z "${key}" || "${key}" == \#* ]] && continue
  case "${key}" in
    BINARY_LOAD_LIFTER_API_TOKEN) api_token="${value}" ;;
    BINARY_LOAD_LIFTER_HOST) device_host="${value}" ;;
    *)
      echo "Unknown setting '${key}' in ${config_file}." >&2
      exit 2
      ;;
  esac
done < "${config_file}"

if (( ${#api_token} < 32 )); then
  echo "BINARY_LOAD_LIFTER_API_TOKEN must contain the same token of at least 32 characters used by the firmware." >&2
  exit 2
fi
if [[ "${api_token}" == *$'\n'* || "${api_token}" == *$'\r'* ]]; then
  echo "BINARY_LOAD_LIFTER_API_TOKEN must be a single line." >&2
  exit 2
fi
if [[ ! "${device_host}" =~ ^[A-Za-z0-9.-]+(:[0-9]+)?$ ]]; then
  echo "BINARY_LOAD_LIFTER_HOST must be a hostname or IPv4 address, optionally followed by a port." >&2
  exit 2
fi

if ! command -v uv >/dev/null 2>&1; then
  echo "uv was not found. Install uv before building or uploading firmware." >&2
  exit 127
fi
platformio_command=(uvx --from platformio==6.1.19 pio)

echo "Building Binary Load Lifter firmware..."
(cd "${project_dir}" && "${platformio_command[@]}" run -e esp32dev)

if [[ ! -s "${firmware_file}" ]]; then
  echo "Build completed without producing ${firmware_file}." >&2
  exit 1
fi

update_url="http://${device_host}/v1/update"
if [[ "${dry_run}" == true ]]; then
  echo "Dry run passed: would upload ${firmware_file} to ${update_url}."
  exit 0
fi

echo "Uploading firmware to ${update_url}..."
# Supply the sensitive header through curl's standard-input config so the API
# token is not exposed in the process command line.
escaped_token="${api_token//\\/\\\\}"
escaped_token="${escaped_token//\"/\\\"}"
printf 'header = "Authorization: Bearer %s"\n' "${escaped_token}" | \
  curl --config - --fail-with-body --silent --show-error \
    --form "firmware=@${firmware_file}" "${update_url}"
echo
echo "Upload accepted. Binary Load Lifter is restarting; verify /v1/status after it reconnects."
