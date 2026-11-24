#!/usr/bin/env bash
# Sourced by the root wrappers. Uses the user's existing EIM installation.
set -e
PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$PROJECT_ROOT"
if ! command -v idf.py >/dev/null 2>&1; then
    light_idf_activation="${LIGHT_IDF_ACTIVATE:-$HOME/.espressif/tools/activate_idf_v6.1.sh}"
    if [[ ! -f "$light_idf_activation" ]]; then
        echo "ESP-IDF activation script not found: $light_idf_activation" >&2
        echo "Activate ESP-IDF yourself, or set LIGHT_IDF_ACTIVATE." >&2
        exit 1
    fi
    # EIM's generated script detects sourcing using $0, which fails inside
    # wrappers named build.sh. Its documented -e mode exports the same toolchain
    # without changing the user's selected IDE installation or evaluating text.
    light_idf_exports="$(bash "$light_idf_activation" -e)"
    while IFS='=' read -r key value; do
        [[ "$key" =~ ^[A-Za-z_][A-Za-z0-9_]*$ ]] || continue
        if [[ "$key" == PATH ]]; then
            export PATH="$value:$PATH"
        else
            export "$key=$value"
        fi
    done <<< "$light_idf_exports"
fi
if [[ -n "${IDF_PYTHON_ENV_PATH:-}" && -f "${IDF_PATH:-}/tools/idf.py" ]]; then
    IDF_COMMAND=("$IDF_PYTHON_ENV_PATH/bin/python" "$IDF_PATH/tools/idf.py")
else
    IDF_COMMAND=(idf.py)
fi
case "${LIGHT_CONSOLE:-usb}" in
    usb) IDF_PROJECT_ARGS=(-B build -D "SDKCONFIG=$PROJECT_ROOT/build/sdkconfig" -D "SDKCONFIG_DEFAULTS=sdkconfig.defaults") ;;
    uart) IDF_PROJECT_ARGS=(-B build-uart -D "SDKCONFIG=$PROJECT_ROOT/build-uart/sdkconfig" -D "SDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.uart.defaults") ;;
    *) echo "LIGHT_CONSOLE must be usb or uart" >&2; exit 1 ;;
esac
