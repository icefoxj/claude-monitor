#Requires -Version 7.0
<#
.SYNOPSIS
    Registers (or removes) the claude-monitor daemon as a scheduled task that
    starts hidden at logon for the current user, and starts it right away.

.EXAMPLE
    .\Install-MonitorDaemon.ps1 -PortName COM5
    .\Install-MonitorDaemon.ps1 -Uninstall
#>
param(
    [switch]$Uninstall,
    [string]$PortName = "COM5",
    [int]$HttpPort = 47831,
    [switch]$LogEvents,      # also append every raw hook payload to %LOCALAPPDATA%\claude-monitor\hooks.jsonl
    [switch]$AccountUsage,           # poll the plan usage like /usage does, with Claude Code's login (for the Tab5's screensaver)
    [switch]$DesktopNotifications    # show the Claude desktop app's notifications (Cowork, scheduled tasks) as a session
)

$taskName = "claude-monitor daemon"

# Stops the task and waits for the daemon to be gone. The daemon leaves on its
# own when its launcher conhost ends; one started by an older registration
# (pwsh straight from the task) or by hand is stopped by its command line
function Stop-Daemon {
    Stop-ScheduledTask -TaskName $taskName -ErrorAction SilentlyContinue
    $find = { Get-CimInstance Win32_Process -Filter "Name='pwsh.exe'" | Where-Object { $_.CommandLine -match 'Monitor-Daemon\.ps1' } }
    for ($i = 0; $i -lt 20 -and (& $find); $i++) { Start-Sleep -Milliseconds 250 }
    & $find | ForEach-Object { Stop-Process -Id $_.ProcessId -Force -ErrorAction SilentlyContinue }
}

if ($Uninstall) {
    Stop-Daemon
    Unregister-ScheduledTask -TaskName $taskName -Confirm:$false -ErrorAction SilentlyContinue
    Write-Host "Removed scheduled task '$taskName'."
    exit 0
}

$script  = Join-Path $PSScriptRoot "Monitor-Daemon.ps1"
$pwsh    = (Get-Command pwsh).Source
$conhost = Join-Path $env:SystemRoot "System32\conhost.exe"

$argument = "-NoProfile -WindowStyle Hidden -ExecutionPolicy Bypass -File `"$script`" -PortName $PortName -HttpPort $HttpPort"
if ($LogEvents) { $argument += " -LogEvents" }
if ($AccountUsage) { $argument += " -AccountUsage" }
if ($DesktopNotifications) { $argument += " -DesktopNotifications" }
# Through a headless conhost: with Windows Terminal as the default terminal
# the console is handed to a Terminal window that -WindowStyle Hidden does
# not hide, and closing that window kills the daemon (exit 0xC000013A)
if (Test-Path $conhost) {
    $action = New-ScheduledTaskAction -Execute $conhost -Argument "--headless `"$pwsh`" $argument"
} else {
    $action = New-ScheduledTaskAction -Execute $pwsh -Argument $argument
}
$trigger   = New-ScheduledTaskTrigger -AtLogOn -User $env:USERNAME
$settings  = New-ScheduledTaskSettingsSet -ExecutionTimeLimit ([TimeSpan]::Zero) -RestartCount 5 -RestartInterval (New-TimeSpan -Minutes 1) `
                 -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries -MultipleInstances IgnoreNew
$principal = New-ScheduledTaskPrincipal -UserId $env:USERNAME -LogonType Interactive -RunLevel Limited

# A running instance keeps the old arguments and blocks the new start
# (MultipleInstances = IgnoreNew): stop it first
Stop-Daemon
Register-ScheduledTask -TaskName $taskName -Action $action -Trigger $trigger -Settings $settings -Principal $principal -Force | Out-Null
Start-ScheduledTask -TaskName $taskName
Write-Host "Registered and started '$taskName' (port $PortName, http://localhost:$HttpPort/)."
