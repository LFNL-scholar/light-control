$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "scripts\idf-env.ps1")
idf.py @IdfProjectArgs build
exit $LASTEXITCODE
