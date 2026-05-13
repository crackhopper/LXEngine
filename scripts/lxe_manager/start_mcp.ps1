param(
    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]]$ManagerArgs
)

$RepoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$ManagerDir = Join-Path $RepoRoot "tools/lxe_manager"
$PackageJson = Join-Path $ManagerDir "package.json"

if (-not (Test-Path $PackageJson)) {
    throw "lxe_manager MCP start failed: package.json not found under $ManagerDir"
}

Push-Location $ManagerDir
try {
    & node --import tsx ./src/index.ts @ManagerArgs
    $ExitCode = $LASTEXITCODE
}
finally {
    Pop-Location
}
exit $ExitCode
