#Requires -Version 7.0
<#
.SYNOPSIS
    Regenerates the README images (docs/images) from a Tab5 on the serial port.

.DESCRIPTION
    Drives the board through the protocol (aggregate words for the icon
    gallery on the single placeholder tile, fake session and event lines for
    the six-tile dashboard and the detail page) and captures each picture
    with Get-Screenshot.ps1. The Tab5 must be in landscape (1280x720) and
    the daemon must not hold the port: Stop-ScheduledTask "claude-monitor
    daemon" first, Start-ScheduledTask after.

    -Quick skips the two captures that need real time to pass: the work ring
    (2.5 minutes of processing for a quarter lap) and the lost-host mark
    (a minute of silence after a ping).

.EXAMPLE
    Stop-ScheduledTask "claude-monitor daemon"
    .\tools\Make-ReadmeImages.ps1 -PortName COM7
    Start-ScheduledTask "claude-monitor daemon"
#>
param(
    [string]$PortName = 'COM7',
    [string]$OutDir = (Join-Path $PSScriptRoot '..\docs\images'),
    [switch]$Quick
)

$ErrorActionPreference = 'Stop'
$shot = Join-Path $PSScriptRoot 'Get-Screenshot.ps1'
$OutDir = [System.IO.Path]::GetFullPath($OutDir)
New-Item -ItemType Directory -Force $OutDir | Out-Null

# The single tile's icon on a 1280x720 screen: band 56, room 608, icon 486
# at (397, 117) (see applyLayout in firmware/tab5/main/main.cpp)
$icon = @(397, 117, 486, 486)
$iconWidth = 240

function Shot([string]$name, [string[]]$setup, [double]$wait, [int[]]$crop, [int]$width) {
    $out = Join-Path $OutDir "$name.png"
    Write-Host "-- $name"
    $args = @{ PortName = $PortName; OutFile = $out; Setup = $setup; Wait = $wait }
    if ($crop) { $args.Crop = $crop }
    if ($width) { $args.Width = $width }
    & $shot @args
}

# ---- the icons, one at a time on the placeholder tile ----
$clean = @('session_clear', 'view grid', 'sessions', 'subagent_stop', 'tool_stop')
Shot 'icon-processing' ($clean + 'idle' + 'processing') 1.5 $icon $iconWidth
Shot 'icon-subagents'  @('idle', 'processing', 'subagent_start') 1.5 $icon $iconWidth
Shot 'icon-tool'       @('subagent_stop', 'idle', 'processing', 'tool_start') 1.5 $icon $iconWidth
Shot 'icon-compacting' @('tool_stop', 'idle', 'compacting') 1.5 $icon $iconWidth
Shot 'icon-waiting'    @('waiting_user') 3 $icon $iconWidth
Shot 'icon-question'   @('question') 3 $icon $iconWidth
Shot 'icon-error'      @('error') 3 $icon $iconWidth
Shot 'icon-paused'     @('paused') 5 $icon $iconWidth
Shot 'icon-idle'       @('idle') 3 $icon $iconWidth
Shot 'icon-sessions'   @('sessions iwqp') 1 $icon $iconWidth
if (-not $Quick) {
    Shot 'icon-ring'      @('sessions', 'idle', 'processing') 150 $icon $iconWidth
    Shot 'icon-link-lost' @('idle', 'ping') 66 $icon $iconWidth
}

# ---- six sessions on the dashboard ----
$t = "`t"
$sessions = @(
    "session a1${t}label=api${t}state=processing${t}subagents=0${t}tool=1${t}project=api${t}root=E:\work\api",
    "session b2${t}label=web${t}state=waiting_user${t}subagents=0${t}tool=0${t}project=web${t}root=E:\work\web",
    "session c3${t}label=infra #1${t}state=processing${t}subagents=2${t}tool=0${t}project=infra${t}root=E:\work\infra",
    "session d4${t}label=infra #2${t}state=question${t}subagents=0${t}tool=0${t}project=infra${t}root=E:\work\infra",
    "session e5${t}label=docs${t}state=idle${t}subagents=0${t}tool=0${t}project=docs${t}root=E:\work\docs",
    "session f6${t}label=mobile${t}state=paused${t}subagents=0${t}tool=0${t}project=mobile${t}root=E:\work\mobile"
)
$common = "project=api${t}project_root=E:\work\api${t}session_id=a1${t}transcript_path=C:\Users\me\.claude\projects\e--work-api\a1.jsonl${t}cwd=E:\work\api${t}permission_mode=default"
$events = @(
    "event SessionStart${t}summary=SessionStart${t}${common}${t}hook_event_name=SessionStart${t}source=startup",
    "event UserPromptSubmit${t}summary=UserPromptSubmit${t}${common}${t}hook_event_name=UserPromptSubmit${t}prompt=Run the test suite and fix whatever fails",
    "event PreToolUse${t}summary=PreToolUse/Read${t}${common}${t}hook_event_name=PreToolUse${t}tool_name=Read${t}tool_input.file_path=E:\work\api\src\server.ts${t}tool_use_id=toolu_01",
    "event PostToolUse${t}summary=PostToolUse/Read${t}${common}${t}hook_event_name=PostToolUse${t}tool_name=Read${t}tool_input.file_path=E:\work\api\src\server.ts${t}tool_response.type=text${t}tool_use_id=toolu_01",
    "event PreToolUse${t}summary=PreToolUse/Bash${t}${common}${t}hook_event_name=PreToolUse${t}tool_name=Bash${t}tool_input.command=npm test${t}tool_input.description=Run the test suite${t}tool_use_id=toolu_02"
)
Shot 'tab5-dashboard' (@('session_clear', 'ping') + $sessions) 4 $null 0
Shot 'tab5-detail'    ($events + 'view detail a1') 2 $null 0

# ---- leave the board as we found it ----
$done = Join-Path $OutDir 'cleanup.png'
& $shot -PortName $PortName -OutFile $done -Setup @('view grid', 'session_clear', 'idle') | Out-Null
Remove-Item -LiteralPath $done -Force -ErrorAction SilentlyContinue
Get-ChildItem $OutDir -Filter *.png | ForEach-Object { "{0,-24} {1,7:N0} KB" -f $_.Name, ($_.Length / 1KB) }
