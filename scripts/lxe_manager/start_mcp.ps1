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
$script:ActiveChild = $null

function Stop-ActiveChild {
    if ($null -ne $script:ActiveChild -and -not $script:ActiveChild.HasExited) {
        Write-ManagerLog "stopping active lxe_manager pid=$($script:ActiveChild.Id)"
        Stop-Process -Id $script:ActiveChild.Id -Force -ErrorAction SilentlyContinue
        $script:ActiveChild.WaitForExit()
    }
}

try {
    $RestartCode = 75
    while ($true) {
        $NodeArgs = @("--import", "tsx", "./src/index.ts") + $ManagerArgs
        Write-ManagerLog "starting lxe_manager: node $($NodeArgs -join ' ')"
        $StdoutLog = Join-Path (Split-Path -Parent $LogFile) "mcp.stdout.tmp.log"
        $StderrLog = Join-Path (Split-Path -Parent $LogFile) "mcp.stderr.tmp.log"
        Remove-Item $StdoutLog, $StderrLog -ErrorAction SilentlyContinue
        $script:ActiveChild = Start-Process -FilePath "node" -ArgumentList $NodeArgs -NoNewWindow -PassThru -RedirectStandardOutput $StdoutLog -RedirectStandardError $StderrLog
        Write-ManagerLog "lxe_manager child pid=$($script:ActiveChild.Id)"
        $script:ActiveChild.WaitForExit()
        $ExitCode = $script:ActiveChild.ExitCode
        if (Test-Path $StdoutLog) {
            Get-Content $StdoutLog | ForEach-Object { Add-Content -Path $LogFile -Value $_; Write-Host $_ }
        }
        if (Test-Path $StderrLog) {
            Get-Content $StderrLog | ForEach-Object { Add-Content -Path $LogFile -Value $_; Write-Host $_ }
        }
        Remove-Item $StdoutLog, $StderrLog -ErrorAction SilentlyContinue
        Write-ManagerLog "lxe_manager child exited code=$ExitCode"
        $script:ActiveChild = $null
        if ($ExitCode -ne $RestartCode) {
            Write-ManagerLog "exit code $ExitCode is not restart code $RestartCode; wrapper exiting"
            exit $ExitCode
        }
        Write-ManagerLog "restart code $RestartCode received; restarting lxe_manager"
    }
}
finally {
    Stop-ActiveChild
    Write-ManagerLog "wrapper stopped"
    Pop-Location
}
