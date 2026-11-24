#!/usr/bin/env bash
# Compatibility alias: web control now flashes the combined RF firmware.
exec "$(dirname "$0")/flash.sh" "$@"
