#!/usr/bin/env bash
source "$(dirname "$0")/scripts/idf-env.sh"
exec "${IDF_COMMAND[@]}" "${IDF_PROJECT_ARGS[@]}" build
