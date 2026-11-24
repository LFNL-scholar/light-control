#!/usr/bin/env bash
source "$(dirname "$0")/scripts/idf-env.sh"
if [[ $# -gt 1 ]]; then
    echo "Usage: ./flash.sh [serial-port]" >&2
    exit 1
fi
if [[ $# -eq 1 ]]; then
    exec "${IDF_COMMAND[@]}" "${IDF_PROJECT_ARGS[@]}" -p "$1" flash monitor
else
    exec "${IDF_COMMAND[@]}" "${IDF_PROJECT_ARGS[@]}" flash monitor
fi
