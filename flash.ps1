param(
    [string]$Port,
    [switch]$NoMonitor
)

$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "scripts\idf-env.ps1")

$argsList = @() + $IdfProjectArgs
if ($Port) {
    $argsList += @("-p", $Port)
}
$argsList += "flash"
if (-not $NoMonitor) {
    $argsList += "monitor"
}

idf.py @argsList
exit $LASTEXITCODE
