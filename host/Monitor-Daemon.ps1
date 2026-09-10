#Requires -Version 7.0
<#
.SYNOPSIS
    claude-monitor host daemon.

.DESCRIPTION
    Keeps the device's serial port permanently open and turns Claude Code hook
    events, received as HTTP POSTs on localhost, into device states. Compared
    with one process per hook this has no start-up latency, a single writer on
    the port, guaranteed ordering, and a log with timestamps.

    Every Claude Code session is tracked separately; the device shows the most
    urgent state among the live sessions (error > waiting_user = question >
    paused > compacting > processing > idle), so a second session cannot hide a
    permission prompt from the first one.

    Endpoints (all on http://localhost:<HttpPort>/):
      POST /hook            Claude Code hook input JSON (any event)
      POST /state/<state>   write one state straight to the device (tests)
      GET  /serial/<cmd>    write any protocol word; for "status" returns the reply
      GET  /status          daemon state, sessions, device state
      POST /release[?seconds=120]   close the port so idf.py can flash; reopens after the delay
      POST /reconnect       reopen the port now
      GET  /health

.EXAMPLE
    pwsh -File Monitor-Daemon.ps1 -PortName COM5
#>
[CmdletBinding()]
param(
    [string]$PortName = "COM5",
    [int]$HttpPort = 47831,
    [string]$LogPath = (Join-Path ([Environment]::GetFolderPath('LocalApplicationData')) "claude-monitor\daemon.log"),
    [int]$SessionTimeoutMinutes = 240
)

$ErrorActionPreference = 'Continue'

$States   = @('processing', 'waiting_user', 'question', 'error', 'paused', 'compacting', 'idle', 'off')
$Priority = @{ off = 0; idle = 1; processing = 2; compacting = 3; paused = 4; question = 5; waiting_user = 5; error = 6 }

# ---------------- logging ----------------

New-Item -ItemType Directory -Force (Split-Path $LogPath) | Out-Null

function Log([string]$msg) {
    $line = "{0:yyyy-MM-dd HH:mm:ss.fff} {1}" -f (Get-Date), $msg
    try {
        if ((Test-Path $LogPath) -and (Get-Item $LogPath).Length -gt 5MB) {
            Move-Item -Force $LogPath "$LogPath.1"
        }
        Add-Content -Path $LogPath -Value $line -Encoding utf8
    } catch {}
    Write-Host $line
}

# ---------------- serial port ----------------

$script:serial          = $null
$script:releaseUntil    = [datetime]::MinValue
$script:nextOpenTry     = [datetime]::MinValue
$script:deviceState     = $null   # last state written to the device
$script:deviceSubagents = 0       # subagent count the device currently holds

function Open-Serial {
    try {
        $p = [System.IO.Ports.SerialPort]::new($PortName, 115200)
        $p.NewLine      = "`n"
        $p.DtrEnable    = $false   # never reset the board on open
        $p.RtsEnable    = $false
        $p.WriteTimeout = 500
        $p.ReadTimeout  = 200
        $p.Open()
        $script:serial = $p
        Log "serial: opened $PortName"
        return $true
    } catch {
        $script:serial = $null
        return $false
    }
}

function Close-Serial([string]$why) {
    if ($script:serial) {
        try { $script:serial.Close(); $script:serial.Dispose() } catch {}
        $script:serial = $null
        Log "serial: closed ($why)"
    }
}

function Send-Serial([string]$line) {
    if (-not $script:serial) { return $false }
    try {
        $script:serial.WriteLine($line)
        return $true
    } catch {
        Log "serial: write failed: $($_.Exception.Message)"
        Close-Serial "write error"
        return $false
    }
}

function Read-SerialLines {
    if (-not $script:serial) { return @() }
    try {
        if ($script:serial.BytesToRead -gt 0) {
            return ($script:serial.ReadExisting() -split "`n")
        }
    } catch {
        Close-Serial "read error"
    }
    return @()
}

# Writes a protocol word; for "status" waits briefly for the STATUS reply
function Send-SerialCommand([string]$cmd) {
    if (-not (Send-Serial $cmd)) { return $null }
    if ($cmd -ne 'status') { return $null }
    $deadline = (Get-Date).AddSeconds(1.5)
    $buf = ''
    while ((Get-Date) -lt $deadline) {
        Start-Sleep -Milliseconds 50
        try {
            if ($script:serial -and $script:serial.BytesToRead -gt 0) { $buf += $script:serial.ReadExisting() }
        } catch {}
        if ($buf -match 'STATUS[^\r\n]*') { return $Matches[0] }
    }
    return $null
}

# ---------------- sessions ----------------

$script:sessions = @{}   # session_id -> @{ state; subagents; last }

function Get-Session([string]$id) {
    if (-not $script:sessions.ContainsKey($id)) {
        $script:sessions[$id] = @{ state = 'idle'; subagents = 0; last = (Get-Date) }
    }
    return $script:sessions[$id]
}

function Expire-Sessions {
    $cut = (Get-Date).AddMinutes(-$SessionTimeoutMinutes)
    foreach ($id in @($script:sessions.Keys)) {
        if ($script:sessions[$id].last -lt $cut) {
            $script:sessions.Remove($id)
            Log "session ${id}: expired after $SessionTimeoutMinutes min without events"
        }
    }
}

function Get-EffectiveState {
    if ($script:sessions.Count -eq 0) { return 'off' }
    $best = $null; $bestP = -1; $bestT = [datetime]::MinValue
    foreach ($s in $script:sessions.Values) {
        $p = $Priority[$s.state]
        if ($p -gt $bestP -or ($p -eq $bestP -and $s.last -gt $bestT)) {
            $best = $s.state; $bestP = $p; $bestT = $s.last
        }
    }
    return $best
}

# Pushes the aggregate state and subagent count to the device, sending only
# what changed. `force` re-sends everything (after the port was reopened).
function Sync-Device([bool]$force = $false) {
    if (-not $script:serial) { return }
    if ($script:sessions.Count -eq 0 -and -not $script:deviceState) { return }   # nothing known yet

    $eff = Get-EffectiveState
    if ($force -or $eff -ne $script:deviceState) {
        if (Send-Serial $eff) {
            Log "device <- $eff"
            $script:deviceState = $eff
            if ($eff -in 'idle', 'off', 'error', 'paused') { $script:deviceSubagents = 0 }   # the firmware resets too
        }
    }

    $total = 0
    foreach ($s in $script:sessions.Values) { $total += $s.subagents }
    while ($script:deviceSubagents -lt $total) {
        if (-not (Send-Serial 'subagent_start')) { break }
        $script:deviceSubagents++
        Log "device <- subagent_start ($($script:deviceSubagents))"
    }
    while ($script:deviceSubagents -gt $total) {
        if (-not (Send-Serial 'subagent_stop')) { break }
        $script:deviceSubagents--
        Log "device <- subagent_stop ($($script:deviceSubagents))"
    }
}

# ---------------- hook events -> states ----------------

function Map-Event($e) {
    switch ([string]$e.hook_event_name) {
        'SessionStart'     { return 'idle' }
        'UserPromptSubmit' { return 'processing' }
        'PreToolUse'       { if ([string]$e.tool_name -eq 'AskUserQuestion') { return 'question' }; return $null }
        'PostToolUse'      { return 'processing' }
        'Notification' {
            switch -Regex ([string]$e.notification_type) {
                '^(permission_prompt|quota_auto_resume_stale)$'                   { return 'waiting_user' }
                '^(elicitation_dialog|elicitation_url_dialog|agent_needs_input)$' { return 'question' }
                '^quota_auto_resume_fired$'                                       { return 'processing' }
                '^quota_auto_resume_disabled$'                                    { return 'error' }
            }
            return $null
        }
        'Stop'             { return 'idle' }
        'StopFailure'      { if ([string]$e.error_type -eq 'rate_limit') { return 'paused' }; return 'error' }
        'PreCompact'       { return 'compacting' }
        'PostCompact' {
            $t = if ($e.trigger) { $e.trigger } else { $e.compact_reason }
            if ([string]$t -eq 'manual') { return 'idle' }
            return 'processing'
        }
        'SubagentStart'    { return 'subagent_start' }
        'SubagentStop'     { return 'subagent_stop' }
        'SessionEnd'       { return 'off' }
    }
    return $null
}

function Process-Hook($e) {
    $sid   = if ($e.session_id) { [string]$e.session_id } else { 'unknown' }
    $short = $sid.Substring(0, [Math]::Min(8, $sid.Length))
    $detail = @($e.notification_type, $e.error_type, $e.tool_name, $e.trigger, $e.compact_reason) | Where-Object { $_ } | Select-Object -First 1
    $tag = if ($detail) { "$($e.hook_event_name)/$detail" } else { [string]$e.hook_event_name }
    $cmd = Map-Event $e
    if (-not $cmd) {
        Log "hook $tag session=$short : ignored"
        return
    }
    if ($e.hook_event_name -eq 'SessionEnd') {
        $script:sessions.Remove($sid)
    } else {
        $s = Get-Session $sid
        switch ($cmd) {
            'subagent_start' { $s.subagents++ }
            'subagent_stop'  { if ($s.subagents -gt 0) { $s.subagents-- } }
            default          { $s.state = $cmd }
        }
        $s.last = Get-Date
    }
    Log "hook $tag session=$short -> $cmd (sessions=$($script:sessions.Count))"
    Expire-Sessions
    Sync-Device
}

# ---------------- http ----------------

function Send-Response($ctx, [int]$code, [string]$body) {
    try {
        $ctx.Response.StatusCode  = $code
        $ctx.Response.ContentType = 'application/json'
        $bytes = [Text.Encoding]::UTF8.GetBytes($body)
        $ctx.Response.ContentLength64 = $bytes.Length
        $ctx.Response.OutputStream.Write($bytes, 0, $bytes.Length)
        $ctx.Response.Close()
    } catch {}
}

function Handle-Request($ctx) {
    $req  = $ctx.Request
    $path = $req.Url.AbsolutePath
    $body = ''
    if ($req.HasEntityBody) {
        $body = [IO.StreamReader]::new($req.InputStream, [Text.Encoding]::UTF8).ReadToEnd()
    }

    if ($path -eq '/hook') {
        try {
            $e = $body | ConvertFrom-Json
            Process-Hook $e
            Send-Response $ctx 200 '{}'
        } catch {
            Log "hook: bad payload: $($_.Exception.Message)"
            Send-Response $ctx 400 '{"error":"bad json"}'
        }
        return
    }
    if ($path -match '^/state/([a-z_]+)$') {
        $st = $Matches[1]
        if ($st -notin $States) { Send-Response $ctx 400 '{"error":"unknown state"}'; return }
        $ok = Send-Serial $st
        if ($ok) {
            $script:deviceState = $st
            if ($st -in 'idle', 'off', 'error', 'paused') { $script:deviceSubagents = 0 }
        }
        Log "manual -> $st (sent=$ok)"
        Send-Response $ctx 200 (@{ state = $st; sent = $ok } | ConvertTo-Json -Compress)
        return
    }
    if ($path -match '^/serial/([a-z_]+)$') {
        $c = $Matches[1]
        $reply = Send-SerialCommand $c
        Send-Response $ctx 200 (@{ sent = $c; reply = $reply } | ConvertTo-Json -Compress)
        return
    }
    if ($path -eq '/release') {
        $sec = 120
        if ($req.QueryString['seconds']) { $sec = [int]$req.QueryString['seconds'] }
        $script:releaseUntil = (Get-Date).AddSeconds($sec)
        Close-Serial "released for $sec s"
        Send-Response $ctx 200 (@{ released_seconds = $sec } | ConvertTo-Json -Compress)
        return
    }
    if ($path -eq '/reconnect') {
        $script:releaseUntil = [datetime]::MinValue
        $script:nextOpenTry  = [datetime]::MinValue
        Send-Response $ctx 200 '{}'
        return
    }
    if ($path -eq '/status') {
        $obj = [ordered]@{
            port             = $PortName
            serial_open      = [bool]$script:serial
            released_until   = $(if ($script:releaseUntil -gt (Get-Date)) { $script:releaseUntil.ToString('s') } else { $null })
            device_state     = $script:deviceState
            device_subagents = $script:deviceSubagents
            effective        = (Get-EffectiveState)
            sessions         = @($script:sessions.GetEnumerator() | ForEach-Object {
                [ordered]@{ id = $_.Key; state = $_.Value.state; subagents = $_.Value.subagents; last = $_.Value.last.ToString('s') }
            })
            log              = $LogPath
        }
        Send-Response $ctx 200 ($obj | ConvertTo-Json -Depth 5)
        return
    }
    if ($path -eq '/health') { Send-Response $ctx 200 '{"ok":true}'; return }
    Send-Response $ctx 404 '{"error":"not found"}'
}

# ---------------- main ----------------

$listener = [System.Net.HttpListener]::new()
$listener.Prefixes.Add("http://localhost:$HttpPort/")
try {
    $listener.Start()
} catch {
    Log "http: cannot listen on port ${HttpPort}: $($_.Exception.Message)"
    exit 1
}
Log "daemon started: port=$PortName http=http://localhost:$HttpPort/ log=$LogPath"

Open-Serial | Out-Null
$task = $listener.GetContextAsync()
$lastHousekeeping = Get-Date

try {
    while ($true) {
        $got = $false
        try { $got = $task.Wait(250) } catch { Log "http: listener error: $($_.Exception.Message)"; break }
        if ($got) {
            $ctx  = $task.Result
            $task = $listener.GetContextAsync()
            try {
                Handle-Request $ctx
            } catch {
                Log "http: handler error: $($_.Exception.Message)"
                Send-Response $ctx 500 '{"error":"internal"}'
            }
        }

        $now = Get-Date
        if (-not $script:serial -and $now -ge $script:releaseUntil -and $now -ge $script:nextOpenTry) {
            if (Open-Serial) {
                # The device may have rebooted: push everything again
                $script:deviceState = $null
                $script:deviceSubagents = 0
                Sync-Device $true
            } else {
                $script:nextOpenTry = $now.AddSeconds(2)
            }
        }
        foreach ($line in (Read-SerialLines)) {
            if ($line -match 'STATUS') { Log "device: $($line.Trim())" }
        }
        if (($now - $lastHousekeeping).TotalSeconds -ge 60) {
            Expire-Sessions
            $lastHousekeeping = $now
        }
    }
} finally {
    Close-Serial "shutdown"
    try { $listener.Stop() } catch {}
    Log "daemon stopped"
}
