param(
    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]]$ManagerArgs
)

$RepoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$ManagerDir = Join-Path $RepoRoot "tools/lxe_manager"
$PackageJson = Join-Path $ManagerDir "package.json"
$LogFile = if ($env:LXE_MANAGER_MCP_LOG_FILE) {
    $env:LXE_MANAGER_MCP_LOG_FILE
} else {
    Join-Path $RepoRoot "data/lxe_manager/mcp.log"
}

if (-not (Test-Path $PackageJson)) {
    throw "lxe_manager MCP start failed: package.json not found under $ManagerDir"
}

New-Item -ItemType Directory -Force -Path (Split-Path -Parent $LogFile) | Out-Null

function Write-ManagerLog {
    param([string]$Message)
    $Line = "[{0}] {1}" -f (Get-Date -Format "o"), $Message
    Add-Content -Path $LogFile -Value $Line
    Write-Host $Line
}

Push-Location $ManagerDir

try {
    $NodeArgs = @("--import", "tsx", "./src/supervisor.ts") + $ManagerArgs
    Write-ManagerLog "starting lxe_manager supervisor: node $($NodeArgs -join ' ')"
    & node @NodeArgs
    $ExitCode = $LASTEXITCODE
    if ($null -eq $ExitCode) {
        $ExitCode = 1
    }
    Write-ManagerLog "lxe_manager supervisor exited code=$ExitCode"
    exit $ExitCode
}
finally {
    Write-ManagerLog "wrapper stopped"
    Pop-Location
}
