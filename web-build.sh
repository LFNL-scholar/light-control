#!/usr/bin/env bash
# Compatibility alias: the final webpage and radio controller are one project.
exec "$(dirname "$0")/build.sh" "$@"
