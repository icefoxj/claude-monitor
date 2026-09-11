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
    [switch]$LogEvents   # also append every raw hook payload to %LOCALAPPDATA%\claude-monitor\hooks.jsonl
)

$taskName = "claude-monitor daemon"

if ($Uninstall) {
    Stop-ScheduledTask -TaskName $taskName -ErrorAction SilentlyContinue
    Unregister-ScheduledTask -TaskName $taskName -Confirm:$false -ErrorAction SilentlyContinue
    Write-Host "Removed scheduled task '$taskName'."
    exit 0
}

$script = Join-Path $PSScriptRoot "Monitor-Daemon.ps1"
$pwsh   = (Get-Command pwsh).Source

$argument = "-NoProfile -WindowStyle Hidden -ExecutionPolicy Bypass -File `"$script`" -PortName $PortName -HttpPort $HttpPort"
if ($LogEvents) { $argument += " -LogEvents" }
$action    = New-ScheduledTaskAction -Execute $pwsh -Argument $argument
$trigger   = New-ScheduledTaskTrigger -AtLogOn -User $env:USERNAME
$settings  = New-ScheduledTaskSettingsSet -ExecutionTimeLimit ([TimeSpan]::Zero) -RestartCount 5 -RestartInterval (New-TimeSpan -Minutes 1) `
                 -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries -MultipleInstances IgnoreNew
$principal = New-ScheduledTaskPrincipal -UserId $env:USERNAME -LogonType Interactive -RunLevel Limited

Register-ScheduledTask -TaskName $taskName -Action $action -Trigger $trigger -Settings $settings -Principal $principal -Force | Out-Null
Start-ScheduledTask -TaskName $taskName
Write-Host "Registered and started '$taskName' (port $PortName, http://localhost:$HttpPort/)."
