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
$script:ActiveChild = $null

function Stop-ActiveChild {
    if ($null -ne $script:ActiveChild -and -not $script:ActiveChild.HasExited) {
        Stop-Process -Id $script:ActiveChild.Id -Force -ErrorAction SilentlyContinue
        $script:ActiveChild.WaitForExit()
    }
}

try {
    $RestartCode = 75
    while ($true) {
        $NodeArgs = @("--import", "tsx", "./src/index.ts") + $ManagerArgs
        $script:ActiveChild = Start-Process -FilePath "node" -ArgumentList $NodeArgs -NoNewWindow -PassThru
        $script:ActiveChild.WaitForExit()
        $ExitCode = $script:ActiveChild.ExitCode
        $script:ActiveChild = $null
        if ($ExitCode -ne $RestartCode) {
            exit $ExitCode
        }
    }
}
finally {
    Stop-ActiveChild
    Pop-Location
}
