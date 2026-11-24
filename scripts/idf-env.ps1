# Dot-sourced by build.ps1 and flash.ps1 to activate local ESP-IDF v5.5.4.
$ErrorActionPreference = "Stop"

$ProjectRoot = Split-Path -Parent $PSScriptRoot
Set-Location $ProjectRoot

if (-not (Get-Command idf.py -ErrorAction SilentlyContinue)) {
    if (-not $env:IDF_TOOLS_PATH -and (Test-Path "C:\Espressif")) {
        $env:IDF_TOOLS_PATH = "C:\Espressif"
    }
    $idfExport = if ($env:LIGHT_IDF_EXPORT) {
        $env:LIGHT_IDF_EXPORT
    } elseif ($env:IDF_PATH -and (Test-Path (Join-Path $env:IDF_PATH "export.ps1"))) {
        Join-Path $env:IDF_PATH "export.ps1"
    } else {
        "C:\esp\v5.5.4\esp-idf\export.ps1"
    }

    if (-not (Test-Path $idfExport)) {
        throw "ESP-IDF export script not found: $idfExport. Set LIGHT_IDF_EXPORT or activate ESP-IDF manually."
    }
    . $idfExport
}

$consoleMode = if ($env:LIGHT_CONSOLE) { $env:LIGHT_CONSOLE } else { "usb" }
switch ($consoleMode) {
    "usb" {
        $script:IdfProjectArgs = @(
            "-B", "build",
            "-D", "SDKCONFIG=$ProjectRoot\build\sdkconfig",
            "-D", "SDKCONFIG_DEFAULTS=sdkconfig.defaults"
        )
    }
    "uart" {
        $script:IdfProjectArgs = @(
            "-B", "build-uart",
            "-D", "SDKCONFIG=$ProjectRoot\build-uart\sdkconfig",
            "-D", "SDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.uart.defaults"
        )
    }
    default {
        throw "LIGHT_CONSOLE must be usb or uart"
    }
}
