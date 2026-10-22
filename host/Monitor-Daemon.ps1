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
    permission prompt from the first one. The device also gets the list of
    live sessions ("sessions pwi", one letter each) for its session dots, and
    a "ping" every PingSeconds so it can tell when the host is gone.

    A session whose Claude Code was killed without a SessionEnd would stay
    for SessionTimeoutMinutes; a processing/compacting session whose
    transcript file has not changed for DeadSessionMinutes is dropped early.

    Claude Code has no hook for the moment a permission is approved. The
    daemon learns each session's claude.exe from the hook's TCP connection
    and watches its child processes in snapshots of the process table: a
    tool process starting under a waiting session means "approved"
    (processing at once, not when the tool ends), a tool process alive means
    "tool running" (tool_start / tool_stop, a blue EXT badge on the device),
    and the session's process going away means the session is dead.

    A device whose VERSION lists "sessions" in features= (the Tab5) shows
    one tile per live session: it gets a "session <id> label=.. state=..
    subagents=.. tool=.." line whenever a session changes, "session_end"
    when one leaves, and "session_clear" followed by all of them when the
    port is reopened. The cube keeps getting the aggregate words.

    Every hook event, with all its fields, is also forwarded as one "event"
    line to a device whose VERSION lists "events" in features= (the Tab5's
    hook inspector); the AtomS3R never receives them. The line also names
    the session's project: the hooks send the X-Claude-Project header
    (${CLAUDE_PROJECT_DIR}); its folder name is used unless ProjectsPath
    (projects.json, { "root": "name" }) maps it to a nicer one. The last 50 are kept
    for GET /events, and with -LogEvents each raw payload is appended as
    one JSON line to EventLogPath (prompts and tool inputs included: mind
    what ends up in that file).

    The account's plan usage (the 5-hour session, the week, per-model weeks,
    what /usage shows) goes to a device whose features= lists "usage" (the
    Tab5's screensaver) as one "usage" line whenever it changes. It comes
    from the status line, when one is set up to POST its input to
    /statusline (documented, but only terminal sessions run a status line,
    not the VS Code extension), and with -AccountUsage from the endpoint
    /usage itself reads, polled every UsagePollSeconds with the access token
    Claude Code keeps in CredentialsPath. That endpoint is undocumented and
    may change; the token is only sent to api.anthropic.com, never logged.

    The Claude desktop app's Code tab fires the same hooks as the CLI, but
    Cowork and the app's scheduled tasks fire none: the app tells you
    through Windows notifications ("Scheduled task needs your approval").
    With -DesktopNotifications the daemon reads those, read-only, from the
    database Windows keeps for the Action Center (wpndatabase.db, through
    the system's winsqlite3.dll) and shows them as one more session,
    "Claude Desktop": the red sign for one that needs you, the green check
    for the others. Windows does not say when you acted on it, so the red
    sign goes when the notification leaves the Action Center, when the
    app's window has been in front for three seconds, or after
    DesktopWaitMinutes; the tile itself goes DesktopLingerMinutes later.

    Endpoints (all on http://localhost:<HttpPort>/):
      POST /hook            Claude Code hook input JSON (any event)
      POST /statusline      Claude Code status line input JSON; answers with the text to show
      GET  /usage           the plan usage last read, where from, and the polling state
      GET  /events[?n=20]   the last hook events as the device sees them (tag, line, time)
      POST /state/<state>   write one state straight to the device (tests)
      GET  /serial/<cmd>    write any protocol word; for "status" / "version" returns the reply
      GET  /version         ask the device who it is: board, model, chip, firmware, ESP-IDF, build, uptime, last reset
      POST /calibrate?rot=<0-3>&sign=<1|-1>&offset=<deg>   store the orientation on the device; ?reset=1 clears it
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
    [int]$SessionTimeoutMinutes = 240,
    [int]$DeadSessionMinutes = 15,
    [int]$PingSeconds = 30,
    [switch]$LogEvents,
    [string]$EventLogPath = (Join-Path ([Environment]::GetFolderPath('LocalApplicationData')) "claude-monitor\hooks.jsonl"),
    [int]$EventValueMax = 160,
    [string]$ProjectsPath = (Join-Path ([Environment]::GetFolderPath('LocalApplicationData')) "claude-monitor\projects.json"),
    [switch]$AccountUsage,            # poll the plan usage the way /usage does, with Claude Code's login
    [int]$UsagePollSeconds = 300,
    [string]$CredentialsPath = (Join-Path (Join-Path $HOME '.claude') '.credentials.json'),
    [switch]$DesktopNotifications,    # show the Claude desktop app's notifications (Cowork, scheduled tasks) as a session
    [int]$DesktopWaitMinutes = 30,
    [int]$DesktopLingerMinutes = 10
)

$ErrorActionPreference = 'Continue'

$States   = @('processing', 'waiting_user', 'question', 'error', 'paused', 'compacting', 'idle', 'off')
$Priority = @{ off = 0; idle = 1; processing = 2; compacting = 3; paused = 4; question = 5; waiting_user = 5; error = 6 }
$Code     = @{ processing = 'p'; waiting_user = 'w'; question = 'q'; error = 'e'; paused = 'h'; compacting = 'c'; idle = 'i' }

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
$script:deviceSessions  = $null   # session codes the device currently shows
$script:deviceTool      = $false  # the device shows the tool-running (EXT) badge
$script:lastPing        = [datetime]::MinValue
$script:deviceInfo      = $null   # fields of the device's VERSION line, read when the port opens
$script:lastInfoTry     = [datetime]::MinValue
$script:started         = Get-Date

function Open-Serial {
    try {
        $p = [System.IO.Ports.SerialPort]::new($PortName, 115200)
        $p.NewLine      = "`n"
        $p.Encoding     = [System.Text.Encoding]::UTF8   # event lines carry whatever the hooks carried
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

# Writes a protocol line; for "status", "version" and "calibrate" waits
# briefly for the device's reply (a STATUS or VERSION line, or ERROR for a
# rejected calibration)
function Send-SerialCommand([string]$cmd) {
    $expect = switch -Wildcard ($cmd) {
        'status'     { 'STATUS' }
        'version'    { 'VERSION' }
        'calibrate*' { 'STATUS|ERROR' }
        default      { $null }
    }
    if (-not (Send-Serial $cmd)) { return $null }
    if (-not $expect) { return $null }
    $deadline = (Get-Date).AddSeconds(1.5)
    $buf = ''
    while ((Get-Date) -lt $deadline) {
        Start-Sleep -Milliseconds 50
        try {
            if ($script:serial -and $script:serial.BytesToRead -gt 0) { $buf += $script:serial.ReadExisting() }
        } catch {}
        if ($buf -match "($expect)[^\r\n]*") { return $Matches[0] }
    }
    return $null
}

# "VERSION board=atoms3r fw=v1.2.0 ..." -> ordered hashtable of its fields
function ConvertFrom-KvLine([string]$line) {
    $fields = [ordered]@{}
    foreach ($tok in ($line -split ' ') | Select-Object -Skip 1) {
        $i = $tok.IndexOf('=')
        if ($i -gt 0) { $fields[$tok.Substring(0, $i)] = $tok.Substring($i + 1) }
    }
    return $fields
}

# Asks the device who it is; keeps the answer for /status and logs it
function Read-DeviceInfo {
    $reply = Send-SerialCommand 'version'
    if ($reply -and $reply -like 'VERSION *') {
        $script:deviceInfo = ConvertFrom-KvLine $reply
        Log "device: $reply"
    } else {
        $script:deviceInfo = $null
        Log "device: no VERSION reply (firmware older than 1.3?)"
    }
    return $reply
}

# ---------------- projects ----------------
#
# The project a session belongs to comes from the X-Claude-Project header
# the hooks send (${CLAUDE_PROJECT_DIR}, the root where the session
# started); without the header, the cwd of the first event seen. The name
# shown is the root's folder name unless projects.json maps the root to a
# nicer one: { "E:\\work\\claude-monitor": "Monitor" }.

$script:projectNames  = @{}
$script:projectsMtime = [datetime]::MinValue

function ConvertTo-RootKey([string]$root) {
    return ($root -replace '[\\/]+$', '').ToLowerInvariant()
}

# Reloads projects.json when it changed (called at start and by housekeeping)
function Update-ProjectNames {
    try {
        if (-not (Test-Path $ProjectsPath)) {
            if ($script:projectNames.Count) { $script:projectNames = @{}; Log "projects: $ProjectsPath removed" }
            return
        }
        $m = (Get-Item $ProjectsPath).LastWriteTime
        if ($m -eq $script:projectsMtime) { return }
        $obj = Get-Content $ProjectsPath -Raw | ConvertFrom-Json
        $map = @{}
        foreach ($p in $obj.PSObject.Properties) { $map[(ConvertTo-RootKey $p.Name)] = [string]$p.Value }
        $script:projectNames  = $map
        $script:projectsMtime = $m
        Log "projects: $($map.Count) name(s) from $ProjectsPath"
    } catch {
        Log "projects: cannot read ${ProjectsPath}: $($_.Exception.Message)"
    }
}

function Get-ProjectName([string]$root) {
    if (-not $root) { return $null }
    $key = ConvertTo-RootKey $root
    if ($script:projectNames.ContainsKey($key)) { return $script:projectNames[$key] }
    return Split-Path -Leaf ($root -replace '[\\/]+$', '')
}

# ---------------- Claude Code processes ----------------
#
# There is no hook for the moment a permission is approved: the red sign
# would stay until the tool finished. But the tool runs as a child process
# of the session's claude.exe, and that process is known: the hook's HTTP
# connection names it (Get-NetTCPConnection, once per session). A snapshot
# of the process table, taken by the main loop, then shows a tool starting
# under it (= approved, and "tool running" for the EXT badge), ending, and
# the session's own process going away.
#
# The snapshot is NtQuerySystemInformation(SystemProcessInformation) through
# a small C# helper (about 20 ms for 700 processes), filtered to the
# sessions' pids before it reaches PowerShell; twice a second while a
# session waits for an approval or has just called a tool, every two seconds
# otherwise. WMI __InstanceCreationEvent polling was used first and is a
# trap: the arbitrator keeps whole Win32_Process instances for a polling
# query within a 5 MB per-user quota, so with a few hundred processes (an
# ESP-IDF build) the subscription is cancelled without a word and new ones
# fail with "Quota violation"; and the flood of events before that stalled
# the daemon for minutes, with the hooks timing out meanwhile.

$script:procSnap   = $false               # the snapshot helper is available (Windows only)
$script:procNextAt = [datetime]::MinValue # next snapshot
$IgnoredChildren = @('conhost.exe')

# x64 layout of SYSTEM_PROCESS_INFORMATION: CreateTime at 32, ImageName
# (UNICODE_STRING) at 56, UniqueProcessId at 80, InheritedFromUniqueProcessId at 88
$ProcSnapSource = @'
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
public static class ProcSnap {
    [DllImport("ntdll.dll")] static extern int NtQuerySystemInformation(int cls, IntPtr buf, int len, out int retLen);
    public struct Entry { public int Pid; public int Parent; public string Name; public long Created; }
    static IntPtr buf = IntPtr.Zero; static int bufLen = 0;
    // Processes whose pid or parent pid is in `parents`
    public static List<Entry> Take(int[] parents) {
        int need;
        if (buf == IntPtr.Zero) { bufLen = 1 << 20; buf = Marshal.AllocHGlobal(bufLen); }
        int status = NtQuerySystemInformation(5, buf, bufLen, out need);   // SystemProcessInformation
        while (status == unchecked((int)0xC0000004)) {                      // STATUS_INFO_LENGTH_MISMATCH
            Marshal.FreeHGlobal(buf); bufLen = need + (256 << 10); buf = Marshal.AllocHGlobal(bufLen);
            status = NtQuerySystemInformation(5, buf, bufLen, out need);
        }
        if (status != 0) return null;
        var list = new List<Entry>();
        long p = buf.ToInt64();
        while (true) {
            int next = Marshal.ReadInt32((IntPtr)p, 0);
            int pid = (int)Marshal.ReadIntPtr((IntPtr)p, 80).ToInt64();
            int parent = (int)Marshal.ReadIntPtr((IntPtr)p, 88).ToInt64();
            if (Array.IndexOf(parents, parent) >= 0 || Array.IndexOf(parents, pid) >= 0) {
                int nameLen = Marshal.ReadInt16((IntPtr)p, 56);
                IntPtr namePtr = Marshal.ReadIntPtr((IntPtr)p, 64);
                string name = (namePtr != IntPtr.Zero && nameLen > 0) ? Marshal.PtrToStringUni(namePtr, nameLen / 2) : "";
                list.Add(new Entry { Pid = pid, Parent = parent, Name = name, Created = Marshal.ReadInt64((IntPtr)p, 32) });
            }
            if (next == 0) break;
            p += next;
        }
        return list;
    }
}
'@

function Initialize-ProcessSnapshots {
    if (-not $IsWindows) { Log "processes: not Windows, approval detection off"; return }
    try {
        Add-Type -TypeDefinition $ProcSnapSource -ErrorAction Stop
        $null = [ProcSnap]::Take([int[]]@(0))
        $script:procSnap = $true
        Log "processes: watching the sessions' child processes (approval detection on)"
    } catch {
        Log "processes: snapshot helper unavailable, approval detection off: $($_.Exception.Message)"
    }
}

function Get-SessionPids {
    return @($script:sessions.Values | Where-Object { $_.pid -gt 0 } | ForEach-Object { [int]$_.pid } | Sort-Object -Unique)
}

function Note-ToolStarted($s, [string]$name, [int]$childPid) {
    $s.tools[$childPid] = $name
    if ($s.state -eq 'waiting_user') {
        $s.state = 'processing'
        $s.last  = Get-Date
        Log "session $($s.short): tool $name started under pid $($s.pid): approved -> processing"
    } else {
        Log "session $($s.short): tool $name started (pid $childPid)"
    }
}

# Compares the live children of each session's process with what it had: a
# new child is a tool starting (the approval, when the session was waiting),
# a missing one a tool that ended, a missing session process a dead session.
# The first look after a session's pid becomes known only records what is
# already running: that predates any prompt. Returns true when something the
# device shows may have changed.
function Update-ProcessTree {
    if (-not $script:procSnap) { return $false }
    $pids = Get-SessionPids
    if ($pids.Count -eq 0) { return $false }
    $entries = $null
    try { $entries = [ProcSnap]::Take([int[]]$pids) } catch { return $false }
    if ($null -eq $entries) { return $false }
    $alive    = @{}
    $children = @{}
    foreach ($e in $entries) {
        $alive[[int]$e.Pid] = $true
        if ([string]$e.Name -in $IgnoredChildren) { continue }
        if (-not $children.ContainsKey([int]$e.Parent)) { $children[[int]$e.Parent] = @{} }
        $children[[int]$e.Parent][[int]$e.Pid] = [string]$e.Name
    }
    $changed = $false
    foreach ($s in @($script:sessions.Values)) {
        if ($s.pid -le 0) { continue }
        if (-not $alive.ContainsKey([int]$s.pid)) {
            if (-not $s.dead) { $s.dead = $true; $changed = $true }
            continue
        }
        $live = if ($children.ContainsKey([int]$s.pid)) { $children[[int]$s.pid] } else { @{} }
        foreach ($cpid in @($live.Keys)) {
            if ($s.tools.ContainsKey($cpid)) { continue }
            if ($s.seeded) {
                Note-ToolStarted $s $live[$cpid] $cpid
            } else {
                $s.tools[$cpid] = $live[$cpid]
                Log "session $($s.short): tool $($live[$cpid]) already running (pid $cpid)"
            }
            $changed = $true
        }
        foreach ($t in @($s.tools.Keys)) {
            if (-not $live.ContainsKey($t)) {
                Log "session $($s.short): tool $($s.tools[$t]) ended (pid $t)"
                $s.tools.Remove($t)
                $changed = $true
            }
        }
        $s.seeded = $true
    }
    return $changed
}

# The process on the other end of a hook request: the claude.exe of the
# session. Only valid while the request is being handled (the connection
# is closed right after the response).
function Resolve-ClientPid($req) {
    try {
        $port = [int]$req.RemoteEndPoint.Port
        $c = Get-NetTCPConnection -LocalPort $port -RemotePort $HttpPort -ErrorAction SilentlyContinue | Select-Object -First 1
        if ($c -and $c.OwningProcess -gt 0) { return [int]$c.OwningProcess }
    } catch {}
    return 0
}

function Test-ProcessAlive([int]$processId) {
    if ($processId -le 0) { return $false }
    return $null -ne (Get-Process -Id $processId -ErrorAction SilentlyContinue)
}

# ---------------- sessions ----------------

$script:sessions = @{}   # session_id -> @{ state; subagents; last; transcript; root; project; pid; tools; dead; short }

function Get-Session([string]$id) {
    if (-not $script:sessions.ContainsKey($id)) {
        $script:sessions[$id] = @{
            id = $id; state = 'idle'; subagents = 0; last = (Get-Date); first = (Get-Date); transcript = $null; root = $null; project = $null
            pid = 0; tools = @{}; seeded = $false; dead = $false; short = $id.Substring(0, [Math]::Min(8, $id.Length))
        }
    }
    return $script:sessions[$id]
}

# What a tile is called: the project name, "#n" appended when several
# live sessions share the project (numbered by age, so the numbers hold)
function Get-SessionLabel($s) {
    $name = if ($s.project) { [string]$s.project } else { [string]$s.short }
    if (-not $s.project) { return $name }
    $same = @($script:sessions.Values | Where-Object { $_.project -eq $s.project } | Sort-Object -Property first)
    if ($same.Count -gt 1) {
        for ($i = 0; $i -lt $same.Count; $i++) {
            if ($same[$i].id -eq $s.id) { return "$name #$($i + 1)" }
        }
    }
    return $name
}

# One line per live session for a board that shows tiles
function Format-SessionLine([string]$id, $s) {
    $tool = if ($s.tools.Count -gt 0) { 1 } else { 0 }
    return "session $id`tlabel=$(Format-EventValue (Get-SessionLabel $s))`tstate=$($s.state)`tsubagents=$($s.subagents)`ttool=$tool`tproject=$(Format-EventValue $s.project)`troot=$(Format-EventValue $s.root)"
}

$script:deviceSessionLines = @{}   # session id -> the line the device last got

# Keeps a "sessions"-capable device's tiles equal to the live sessions:
# session_end for the ones that left, a session line for each that changed.
# `force` starts from session_clear (the port was just reopened).
function Send-SessionLines([bool]$force) {
    if (-not $script:serial -or -not $script:deviceInfo) { return }
    if (([string]$script:deviceInfo.features) -notmatch '(^|,)sessions(,|$)') { return }
    if ($force) {
        if (Send-Serial 'session_clear') { Log 'device <- session_clear' }
        $script:deviceSessionLines = @{}
    }
    foreach ($id in @($script:deviceSessionLines.Keys)) {
        if (-not $script:sessions.ContainsKey($id)) {
            if (Send-Serial "session_end $id") {
                Log "device <- session_end $id"
                $script:deviceSessionLines.Remove($id)
            }
        }
    }
    foreach ($id in @($script:sessions.Keys)) {
        $line = Format-SessionLine $id $script:sessions[$id]
        if ($script:deviceSessionLines[$id] -ne $line) {
            if (Send-Serial $line) {
                Log "device <- $($line -replace "`t", ' ')"
                $script:deviceSessionLines[$id] = $line
            }
        }
    }
}

# Drops sessions that are silent for too long, and processing/compacting
# sessions whose transcript stopped changing: Claude Code appends to it
# while it works, so a stale one means the process is gone (or a tool call
# has run for longer than DeadSessionMinutes, in which case the next hook
# event simply recreates the session)
function Expire-Sessions {
    $now  = Get-Date
    $cut  = $now.AddMinutes(-$SessionTimeoutMinutes)
    $dead = $now.AddMinutes(-$DeadSessionMinutes)
    foreach ($id in @($script:sessions.Keys)) {
        $s = $script:sessions[$id]
        if ($s.last -lt $cut) {
            $script:sessions.Remove($id)
            Log "session ${id}: expired after $SessionTimeoutMinutes min without events"
            continue
        }
        # The exact signal when the process is known: it is gone
        if ($s.dead -or ($s.pid -gt 0 -and -not (Test-ProcessAlive $s.pid))) {
            $script:sessions.Remove($id)
            Log "session ${id}: dropped, its Claude Code process (pid $($s.pid)) is gone"
            continue
        }
        if ($s.pid -le 0 -and $s.state -in 'processing', 'compacting' -and $s.transcript -and $s.last -lt $dead) {
            $stale = $false
            try {
                if (-not (Test-Path -LiteralPath $s.transcript)) { $stale = $true }
                elseif ((Get-Item -LiteralPath $s.transcript).LastWriteTime -lt $dead) { $stale = $true }
            } catch {}
            if ($stale) {
                $script:sessions.Remove($id)
                Log "session ${id}: dropped, $($s.state) but its transcript has not changed for $DeadSessionMinutes min"
            }
        }
    }
}

# One code letter per live session, most urgent first, then most recent
function Get-SessionCodes {
    $ordered = $script:sessions.Values | Sort-Object -Property @{ Expression = { $Priority[$_.state] }; Descending = $true },
                                                              @{ Expression = { $_.last }; Descending = $true }
    $codes = ''
    foreach ($s in $ordered) {
        $c = $Code[$s.state]
        if ($c) { $codes += $c }
    }
    return $codes
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
    if ($script:sessions.Count -eq 0 -and -not $script:deviceState) {   # nothing known yet
        Send-SessionLines $force
        return
    }

    $eff = Get-EffectiveState
    if ($force -or $eff -ne $script:deviceState) {
        if (Send-Serial $eff) {
            Log "device <- $eff"
            $script:deviceState = $eff
            if ($eff -in 'idle', 'off', 'error', 'paused') {   # the firmware resets too
                $script:deviceSubagents = 0
                $script:deviceTool = $false
            }
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

    $codes = Get-SessionCodes
    if ($force -or $codes -ne $script:deviceSessions) {
        $line = if ($codes) { "sessions $codes" } else { 'sessions' }
        if (Send-Serial $line) {
            Log "device <- $line"
            $script:deviceSessions = $codes
        }
    }

    # Blue dot: a tool process is running in the session the device is showing
    $tool = $false
    foreach ($s in $script:sessions.Values) {
        if ($s.state -eq $eff -and $s.tools.Count -gt 0) { $tool = $true; break }
    }
    if ($force -or $tool -ne $script:deviceTool) {
        $line = if ($tool) { 'tool_start' } else { 'tool_stop' }
        if (Send-Serial $line) {
            Log "device <- $line"
            $script:deviceTool = $tool
        }
    }

    Send-SessionLines $force
}

# ---------------- hook events -> the device's inspector ----------------

$script:events     = [System.Collections.Generic.List[object]]::new()   # last 50 formatted events
$script:eventsSent = 0

# One value as a single line of at most EventValueMax characters
function Format-EventValue($v) {
    if ($null -eq $v) { return '' }
    $s = if ($v -is [string]) { $v }
         elseif ($v -is [bool] -or $v -is [ValueType]) { [string]$v }
         else { $v | ConvertTo-Json -Compress -Depth 6 }
    $s = $s -replace '[\t\r\n]+', ' '
    $s = $s -replace '[\x00-\x1F]', ''
    if ($s.Length -gt $EventValueMax) { $s = $s.Substring(0, $EventValueMax - 3) + '...' }
    return $s
}

# "event <name>\tsummary=..\tproject=..\tproject_root=..\t<key>=<value>..":
# the daemon's own fields first, then every top-level field of the hook
# payload, objects flattened one level ("tool_input.command"), in the order
# Claude Code sent them
function Format-EventLine($e, [string]$tag, [string]$project, [string]$root) {
    $parts = [System.Collections.Generic.List[string]]::new()
    $parts.Add("summary=$tag")
    if ($project) { $parts.Add("project=$(Format-EventValue $project)") }
    if ($root)    { $parts.Add("project_root=$(Format-EventValue $root)") }
    foreach ($p in $e.PSObject.Properties) {
        $v = $p.Value
        if ($v -is [System.Management.Automation.PSCustomObject]) {
            foreach ($q in $v.PSObject.Properties) {
                $parts.Add("$($p.Name).$($q.Name)=$(Format-EventValue $q.Value)")
            }
        } else {
            $parts.Add("$($p.Name)=$(Format-EventValue $v)")
        }
    }
    $line = "event $([string]$e.hook_event_name)`t" + ($parts -join "`t")
    if ($line.Length -gt 3800) { $line = $line.Substring(0, 3800) }
    return $line
}

function Add-Event([string]$tag, [string]$line) {
    $script:events.Add([ordered]@{ time = (Get-Date).ToString('HH:mm:ss.fff'); tag = $tag; line = $line })
    while ($script:events.Count -gt 50) { $script:events.RemoveAt(0) }
}

# Only a board that asked for them gets event lines: they are long, and a
# board without the feature would drop them anyway
function Send-Event([string]$line) {
    if (-not $script:serial -or -not $script:deviceInfo) { return }
    if (([string]$script:deviceInfo.features) -notmatch '(^|,)events(,|$)') { return }
    if (Send-Serial $line) { $script:eventsSent++ }
}

function Write-EventLog($e) {
    try {
        if ((Test-Path $EventLogPath) -and (Get-Item $EventLogPath).Length -gt 20MB) {
            Move-Item -Force $EventLogPath "$EventLogPath.1"
        }
        $obj = [ordered]@{ received = (Get-Date).ToString('o') }
        foreach ($p in $e.PSObject.Properties) { $obj[$p.Name] = $p.Value }
        Add-Content -Path $EventLogPath -Value ($obj | ConvertTo-Json -Compress -Depth 10) -Encoding utf8
    } catch {
        Log "event log: $($_.Exception.Message)"
    }
}

# ---------------- hook events -> states ----------------

function Map-Event($e) {
    switch ([string]$e.hook_event_name) {
        'SessionStart'     { return 'idle' }
        'UserPromptSubmit' { return 'processing' }
        'PreToolUse'       { if ([string]$e.tool_name -eq 'AskUserQuestion') { return 'question' }; return 'processing' }
        'PostToolUse'      { return 'processing' }
        'PostToolUseFailure' { return 'processing' }   # a failed tool: Claude carries on
        'PostToolBatch'    { return 'processing' }   # a batch of parallel tools resolved, next model call
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

function Process-Hook($e, [string]$rootHeader, [int]$clientPid) {
    $sid   = if ($e.session_id) { [string]$e.session_id } else { 'unknown' }
    $short = $sid.Substring(0, [Math]::Min(8, $sid.Length))
    $detail = @($e.notification_type, $e.error_type, $e.tool_name, $e.trigger, $e.compact_reason) | Where-Object { $_ } | Select-Object -First 1
    $tag = if ($detail) { "$($e.hook_event_name)/$detail" } else { [string]$e.hook_event_name }

    # The project: the header (the session's root) wins; else what the
    # session already had; else the cwd of this first event
    $known = if ($script:sessions.ContainsKey($sid)) { $script:sessions[$sid] } else { $null }
    $root = if ($rootHeader) { $rootHeader }
            elseif ($known -and $known.root) { $known.root }
            elseif ($e.cwd) { [string]$e.cwd }
            else { $null }
    $project = Get-ProjectName $root

    # Every event goes to the inspector, the ring and the JSON log, mapped or
    # not; the device gets it after its session line (Sync-Device below)
    $eventLine = Format-EventLine $e $tag $project $root
    Add-Event $tag $eventLine
    if ($LogEvents) { Write-EventLog $e }

    $cmd = Map-Event $e
    if (-not $cmd) {
        Log "hook $tag session=$short project=$project : ignored"
        Send-Event $eventLine
        return
    }
    $eventSent = $false
    if ($e.hook_event_name -eq 'SessionEnd') {
        Send-Event $eventLine   # the tile sees its last event before session_end takes it down
        $eventSent = $true
        $script:sessions.Remove($sid)
    } else {
        $s = Get-Session $sid
        switch ($cmd) {
            'subagent_start' { $s.subagents++ }
            'subagent_stop'  { if ($s.subagents -gt 0) { $s.subagents-- } }
            default          { $s.state = $cmd }
        }
        # Any activity from a session that was waiting means the permission went through
        if ($cmd -in 'subagent_start', 'subagent_stop' -and $s.state -eq 'waiting_user') { $s.state = 'processing' }
        $s.last = Get-Date
        if ($clientPid -gt 0 -and $clientPid -ne $s.pid) {
            $s.pid    = $clientPid
            $s.dead   = $false
            $s.tools  = @{}
            $s.seeded = $false   # the next snapshot records what already runs under it
            Log "session ${short}: Claude Code process pid $clientPid"
        }
        if ($e.hook_event_name -in 'PostToolUse', 'PostToolUseFailure' -and $s.tools.Count -gt 0) {
            # The tool is over: forget its processes without waiting for the next snapshot
            foreach ($t in @($s.tools.Keys)) { if (-not (Test-ProcessAlive $t)) { $s.tools.Remove($t) } }
        }
        if ($e.transcript_path) { $s.transcript = [string]$e.transcript_path }
        if ($root -and ($rootHeader -or -not $s.root)) {
            $s.root    = $root
            $s.project = $project
        }
    }
    Log "hook $tag session=$short project=$project -> $cmd (sessions=$($script:sessions.Count))"
    Expire-Sessions
    Sync-Device
    if (-not $eventSent) { Send-Event $eventLine }
}

# ---------------- account usage -> the device's screensaver ----------------

$script:usage          = [ordered]@{}            # key -> @{ label; percent; resetsAt (DateTimeOffset or $null); severity }
$script:usageSplit     = @()                     # the week by app: @{ label; percent }, from the account
$script:usageSource    = $null                   # 'statusline' or 'account'
$script:usageAt        = [datetime]::MinValue    # when the numbers were read
$script:usageNote      = $(if ($AccountUsage) { 'not polled yet' } else { 'account polling off (no -AccountUsage)' })
$script:usageNextPoll  = (Get-Date).AddSeconds(5)
$script:usageTask      = $null                   # the poll in flight
$script:usageHttp      = $null
$script:usageCheckAt   = [datetime]::MinValue
$script:deviceUsageKey = $null                   # what the device last got
$script:deviceUsageAt  = [datetime]::MinValue

function Set-UsageNote([string]$note) {
    if ($note -ne $script:usageNote) { Log "usage: $note" }
    $script:usageNote = $note
}

function Get-UsageLabel([string]$kind, [string]$model) {
    switch ($kind) {
        'session'       { return 'Current session' }
        'weekly_all'    { return 'This week' }
        'weekly_scoped' { if ($model) { return "This week ($model)" } else { return 'This week (scoped)' } }
        default         { return ($kind -replace '_', ' ') }
    }
}

# A reset time as the sources give it: ISO 8601 (ConvertFrom-Json may have
# made it a DateTime already) or Unix seconds (the status line)
function ConvertTo-ResetTime($v) {
    if ($null -eq $v -or "$v" -eq '') { return $null }
    if ($v -is [datetime]) { return [DateTimeOffset]$v }
    if ($v -is [ValueType]) { return [DateTimeOffset]::FromUnixTimeSeconds([int64]$v) }
    try { return [DateTimeOffset]::Parse([string]$v, [cultureinfo]::InvariantCulture) } catch { return $null }
}

# The endpoint's answer: its "limits" list (session, weekly_all,
# weekly_scoped per model), else the older five_hour / seven_day objects
function Set-UsageFromAccount($j) {
    $next = [ordered]@{}
    if ($j.limits) {
        foreach ($l in $j.limits) {
            if ($null -eq $l.percent) { continue }
            $model = [string]$l.scope.model.display_name
            $key = if ($model) { "$($l.kind):$model" } else { [string]$l.kind }
            $next[$key] = @{ label = (Get-UsageLabel $l.kind $model); percent = [double]$l.percent
                             resetsAt = (ConvertTo-ResetTime $l.resets_at); severity = [string]$l.severity }
        }
    } else {
        foreach ($p in @(@('five_hour', 'session'), @('seven_day', 'weekly_all'))) {
            $w = $j.($p[0])
            if ($w -and $null -ne $w.utilization) {
                $next[$p[1]] = @{ label = (Get-UsageLabel $p[1] ''); percent = [double]$w.utilization
                                  resetsAt = (ConvertTo-ResetTime $w.resets_at); severity = '' }
            }
        }
    }
    if ($next.Count -eq 0) { return $false }
    $script:usage = $next
    # How the week divides between the apps (Claude Code, chats, Cowork)
    $script:usageSplit = @(foreach ($r in @($j.seven_day_breakdown.rows)) {
        if ($r -and $null -ne $r.percent -and [double]$r.percent -gt 0) {
            @{ label = [string]$(if ($r.display_name) { $r.display_name } else { $r.key }); percent = [double]$r.percent }
        }
    })
    $script:usageSource = 'account'
    $script:usageAt = Get-Date
    return $true
}

# The status line's rate_limits: the session and the week only, so the
# per-model rows the account poll found stay as they are
function Set-UsageFromStatusLine($rl) {
    $any = $false
    foreach ($p in @(@('five_hour', 'session'), @('seven_day', 'weekly_all'))) {
        $w = $rl.($p[0])
        if (-not $w -or $null -eq $w.used_percentage) { continue }
        $script:usage[$p[1]] = @{ label = (Get-UsageLabel $p[1] ''); percent = [double]$w.used_percentage
                                  resetsAt = (ConvertTo-ResetTime $w.resets_at); severity = '' }
        $any = $true
    }
    if ($any) {
        $script:usageSource = 'statusline'
        $script:usageAt = Get-Date
    }
    return $any
}

# "usage <source>\tage=<s>\tlimit=<label>|<percent>|<resets in s>|<severity>..."
function Format-UsageLine {
    if ($script:usage.Count -eq 0) { return 'usage none' }
    $inv = [cultureinfo]::InvariantCulture
    $now = [DateTimeOffset]::Now
    $age = [int][Math]::Max(0, ((Get-Date) - $script:usageAt).TotalSeconds)
    $parts = @("usage $($script:usageSource)", "age=$age")
    foreach ($e in $script:usage.Values) {
        $reset = -1
        if ($e.resetsAt) { $reset = [int][Math]::Max(0, ($e.resetsAt - $now).TotalSeconds) }
        $label = $e.label -replace '[\t|]', ' '
        if ($label.Length -gt 28) { $label = $label.Substring(0, 28) }
        $parts += 'limit=' + [string]::Format($inv, '{0}|{1:0.#}|{2}|{3}', $label, $e.percent, $reset, $e.severity)
    }
    if ($script:usageSplit.Count) {
        $parts += 'split=' + (@($script:usageSplit | ForEach-Object {
            [string]::Format($inv, '{0}:{1:0.#}', ($_.label -replace '[\t|:]', ' '), $_.percent)
        }) -join '|')
    }
    return ($parts -join "`t")
}

# Sends the usage line when the numbers changed, when new ones arrived and
# the device's copy is five minutes old (its "updated ... ago"), or forced.
# A window whose reset time has passed is dropped, as Claude Code does.
function Send-Usage([bool]$force = $false) {
    if (-not $script:serial) { return }
    if (([string]$script:deviceInfo.features) -notmatch '(^|,)usage(,|$)') { return }
    $now = [DateTimeOffset]::Now
    foreach ($k in @($script:usage.Keys)) {
        $r = $script:usage[$k].resetsAt
        if ($r -and $r -lt $now) { $script:usage.Remove($k) }
    }
    $inv = [cultureinfo]::InvariantCulture
    $key = "$($script:usageSource)|" + (@($script:usage.Values | ForEach-Object {
        $min = if ($_.resetsAt) { [int]($_.resetsAt.ToUnixTimeSeconds() / 60) } else { '-' }
        [string]::Format($inv, '{0}={1:0.#}@{2}', $_.label, $_.percent, $min)
    }) -join ';') + '|' + (@($script:usageSplit | ForEach-Object { "$($_.label)=$($_.percent)" }) -join ';')
    $changed = $key -ne $script:deviceUsageKey
    $refresh = $script:usageAt -gt $script:deviceUsageAt -and ((Get-Date) - $script:deviceUsageAt).TotalSeconds -ge 300
    if (-not ($force -or $changed -or $refresh)) { return }
    $line = Format-UsageLine
    if (Send-Serial $line) {
        if ($force -or $changed) { Log "device <- $($line -replace "`t", ' ')" }
        $script:deviceUsageKey = $key
        $script:deviceUsageAt = Get-Date
    }
}

# Starts one poll of the endpoint /usage reads, without waiting for it: the
# main loop picks the answer up (Complete-UsagePoll)
function Start-UsagePoll {
    $script:usageNextPoll = (Get-Date).AddSeconds($UsagePollSeconds)
    try {
        $o = (Get-Content -LiteralPath $CredentialsPath -Raw -ErrorAction Stop | ConvertFrom-Json).claudeAiOauth
    } catch {
        Set-UsageNote "no Claude Code login in $CredentialsPath"
        return
    }
    if (-not $o -or -not $o.accessToken) { Set-UsageNote 'no claude.ai login in the credentials file'; return }
    if ($o.expiresAt -and [DateTimeOffset]::FromUnixTimeMilliseconds([int64]$o.expiresAt) -lt [DateTimeOffset]::Now.AddMinutes(1)) {
        Set-UsageNote 'access token expired: waiting for Claude Code to refresh it'
        $script:usageNextPoll = (Get-Date).AddMinutes(2)
        return
    }
    try {
        if (-not $script:usageHttp) {
            $script:usageHttp = [System.Net.Http.HttpClient]::new()
            $script:usageHttp.Timeout = [TimeSpan]::FromSeconds(15)
        }
        $req = [System.Net.Http.HttpRequestMessage]::new([System.Net.Http.HttpMethod]::Get, 'https://api.anthropic.com/api/oauth/usage')
        $req.Headers.Authorization = [System.Net.Http.Headers.AuthenticationHeaderValue]::new('Bearer', [string]$o.accessToken)
        $req.Headers.Add('anthropic-beta', 'oauth-2025-04-20')
        $req.Headers.UserAgent.ParseAdd('claude-monitor')
        $script:usageTask = $script:usageHttp.SendAsync($req)
    } catch {
        Set-UsageNote "poll failed: $($_.Exception.Message)"
    }
}

function Complete-UsagePoll {
    $t = $script:usageTask
    $script:usageTask = $null
    if ($t.IsFaulted -or $t.IsCanceled) {
        $msg = if ($t.Exception) { $t.Exception.GetBaseException().Message } else { 'timed out' }
        Set-UsageNote "poll failed: $msg"
        return
    }
    $resp = $t.Result
    try {
        $code = [int]$resp.StatusCode
        if ($code -eq 429) {
            $script:usageNextPoll = (Get-Date).AddMinutes(15)
            Set-UsageNote 'rate limited (HTTP 429): next poll in 15 min'
            return
        }
        if ($code -ne 200) { Set-UsageNote "poll failed: HTTP $code"; return }
        $j = $resp.Content.ReadAsStringAsync().GetAwaiter().GetResult() | ConvertFrom-Json
        if (Set-UsageFromAccount $j) {
            Set-UsageNote 'ok'
            Send-Usage
        } else {
            Set-UsageNote 'no limits in the answer'
        }
    } catch {
        Set-UsageNote "poll failed: $($_.Exception.Message)"
    } finally {
        $resp.Dispose()
    }
}

function Get-UsageObject {
    [ordered]@{
        source          = $script:usageSource
        read_at         = $(if ($script:usageAt -gt [datetime]::MinValue) { $script:usageAt.ToString('s') } else { $null })
        account_polling = [bool]$AccountUsage
        note            = $script:usageNote
        limits          = @($script:usage.GetEnumerator() | ForEach-Object {
            [ordered]@{ key = $_.Key; label = $_.Value.label; percent = $_.Value.percent
                        resets_at = $(if ($_.Value.resetsAt) { $_.Value.resetsAt.ToString('o') } else { $null }); severity = $_.Value.severity }
        })
        week_by_app     = @($script:usageSplit | ForEach-Object { [ordered]@{ label = $_.label; percent = $_.percent } })
    }
}

# What the status line shows: model, context, and the two windows
function Format-StatusLineText($j) {
    $parts = @()
    if ($j.model.display_name) { $parts += [string]$j.model.display_name }
    if ($null -ne $j.context_window.used_percentage) { $parts += 'ctx {0:0}%' -f [double]$j.context_window.used_percentage }
    $s = $script:usage['session']
    if ($s) {
        $t = '5h {0:0}%' -f $s.percent
        if ($s.resetsAt) { $t += ' (resets {0:HH:mm})' -f $s.resetsAt.LocalDateTime }
        $parts += $t
    }
    $w = $script:usage['weekly_all']
    if ($w) { $parts += '7d {0:0}%' -f $w.percent }
    return ($parts -join ' · ')
}

# ---------------- the Claude desktop app's notifications ----------------

$DesktopId = 'claude-desktop'
$script:desktopOn     = $false
$script:desktopNote   = $(if ($DesktopNotifications) { 'starting' } else { 'off (no -DesktopNotifications)' })
$script:desktopDb     = Join-Path ([Environment]::GetFolderPath('LocalApplicationData')) 'Microsoft\Windows\Notifications\wpndatabase.db'
$script:desktopSeen   = @{}   # notification id -> $true, for the ones still in the database
$script:desktopWait   = @{}   # notification id -> arrival, for the ones that need you
$script:desktopNextAt = [datetime]::MinValue
$script:desktopFocusSince = [datetime]::MinValue
$script:desktopIdleSince  = [datetime]::MinValue
$script:desktopLast   = $null # the last one: @{ time; title; state }

$desktopSource = @'
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Text;

// The Claude desktop app's toasts from the Action Center's database, read
// with the SQLite that ships with Windows, and the program owning the
// foreground window
public static class DesktopToasts {
    [DllImport("winsqlite3.dll", EntryPoint = "sqlite3_open_v2")]
    static extern int Open(byte[] filename, out IntPtr db, int flags, IntPtr vfs);
    [DllImport("winsqlite3.dll", EntryPoint = "sqlite3_close_v2")]
    static extern int Close(IntPtr db);
    [DllImport("winsqlite3.dll", EntryPoint = "sqlite3_busy_timeout")]
    static extern int BusyTimeout(IntPtr db, int ms);
    [DllImport("winsqlite3.dll", EntryPoint = "sqlite3_prepare_v2")]
    static extern int Prepare(IntPtr db, byte[] sql, int nByte, out IntPtr stmt, IntPtr tail);
    [DllImport("winsqlite3.dll", EntryPoint = "sqlite3_step")]
    static extern int Step(IntPtr stmt);
    [DllImport("winsqlite3.dll", EntryPoint = "sqlite3_finalize")]
    static extern int Finalize(IntPtr stmt);
    [DllImport("winsqlite3.dll", EntryPoint = "sqlite3_column_int64")]
    static extern long ColumnInt64(IntPtr stmt, int col);
    [DllImport("winsqlite3.dll", EntryPoint = "sqlite3_column_blob")]
    static extern IntPtr ColumnBlob(IntPtr stmt, int col);
    [DllImport("winsqlite3.dll", EntryPoint = "sqlite3_column_bytes")]
    static extern int ColumnBytes(IntPtr stmt, int col);

    [DllImport("user32.dll")]
    static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll")]
    static extern uint GetWindowThreadProcessId(IntPtr hWnd, out uint pid);
    [DllImport("kernel32.dll")]
    static extern IntPtr OpenProcess(uint access, bool inherit, uint pid);
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode)]
    static extern bool QueryFullProcessImageNameW(IntPtr process, int flags, StringBuilder name, ref int size);
    [DllImport("kernel32.dll")]
    static extern bool CloseHandle(IntPtr handle);

    public class Toast {
        public long Id;
        public long Arrival;      // FILETIME, UTC
        public string Payload;    // the toast's XML
    }

    // The Store app is "Claude_<publisher>!Claude"; the older installer's
    // ids carry "AnthropicClaude"
    const string Sql =
        "SELECT n.Id, n.ArrivalTime, n.Payload FROM Notification n " +
        "JOIN NotificationHandler h ON h.RecordId = n.HandlerId " +
        "WHERE h.PrimaryId LIKE 'Claude%!Claude' OR h.PrimaryId LIKE '%AnthropicClaude%' ORDER BY n.Id";

    public static List<Toast> Read(string path) {
        var list = new List<Toast>();
        IntPtr db;
        int rc = Open(Encoding.UTF8.GetBytes(path + "\0"), out db, 1 /* SQLITE_OPEN_READONLY */, IntPtr.Zero);
        if (rc != 0) {
            if (db != IntPtr.Zero) Close(db);
            throw new Exception("sqlite open failed (" + rc + ")");
        }
        try {
            BusyTimeout(db, 250);
            IntPtr stmt;
            rc = Prepare(db, Encoding.UTF8.GetBytes(Sql + "\0"), -1, out stmt, IntPtr.Zero);
            if (rc != 0) throw new Exception("sqlite prepare failed (" + rc + ")");
            try {
                while (Step(stmt) == 100 /* SQLITE_ROW */) {
                    var t = new Toast { Id = ColumnInt64(stmt, 0), Arrival = ColumnInt64(stmt, 1), Payload = "" };
                    int n = ColumnBytes(stmt, 2);
                    IntPtr p = ColumnBlob(stmt, 2);
                    if (n > 0 && p != IntPtr.Zero) {
                        var bytes = new byte[n];
                        Marshal.Copy(p, bytes, 0, n);
                        t.Payload = Encoding.UTF8.GetString(bytes).TrimStart('﻿', '\0');
                    }
                    list.Add(t);
                }
            } finally {
                Finalize(stmt);
            }
        } finally {
            Close(db);
        }
        return list;
    }

    // Full path of the program whose window is in front, or ""
    public static string ForegroundImage() {
        IntPtr w = GetForegroundWindow();
        if (w == IntPtr.Zero) return "";
        uint pid;
        GetWindowThreadProcessId(w, out pid);
        IntPtr h = OpenProcess(0x1000 /* PROCESS_QUERY_LIMITED_INFORMATION */, false, pid);
        if (h == IntPtr.Zero) return "";
        try {
            var sb = new StringBuilder(1024);
            int size = sb.Capacity;
            return QueryFullProcessImageNameW(h, 0, sb, ref size) ? sb.ToString() : "";
        } finally {
            CloseHandle(h);
        }
    }
}
'@

function Set-DesktopNote([string]$note) {
    if ($note -ne $script:desktopNote) { Log "desktop: $note" }
    $script:desktopNote = $note
}

# Starts watching, taking what is already in the Action Center as seen
# (except the last five minutes, so a restart does not miss a fresh one)
function Initialize-DesktopWatch {
    if (-not $DesktopNotifications) { return }
    if (-not $IsWindows) { Set-DesktopNote 'not Windows: off'; return }
    if (-not (Test-Path -LiteralPath $script:desktopDb)) { Set-DesktopNote "no notification database at $($script:desktopDb)"; return }
    try {
        Add-Type -TypeDefinition $desktopSource -ErrorAction Stop
        $recent = (Get-Date).AddMinutes(-5)
        foreach ($t in [DesktopToasts]::Read($script:desktopDb)) {
            if ([DateTime]::FromFileTimeUtc($t.Arrival).ToLocalTime() -lt $recent) { $script:desktopSeen[$t.Id] = $true }
        }
        $script:desktopOn = $true
        Set-DesktopNote "watching the Claude app's notifications ($($script:desktopSeen.Count) already there)"
    } catch {
        Set-DesktopNote "cannot read notifications: $($_.Exception.Message)"
    }
}

function Get-ToastTexts([string]$payload) {
    $out = @()
    try {
        $x = [xml]$payload
        foreach ($n in $x.SelectNodes('//text')) { $t = $n.InnerText.Trim(); if ($t) { $out += $t } }
    } catch {}
    if (-not $out) { $out = @('(notification)') }
    return ,$out
}

# One notification as an event line for the tile's inspector
function Format-DesktopEvent([string]$title, [string]$body, [string]$state, [datetime]$arrival) {
    $e = [pscustomobject][ordered]@{
        hook_event_name = 'DesktopNotification'; session_id = $DesktopId
        title = $title; body = $body; state = $state; arrived = $arrival.ToString('HH:mm:ss'); app = 'Claude Desktop'
    }
    return (Format-EventLine $e 'Notification/desktop' 'Claude Desktop' '')
}

# Keeps the "Claude Desktop" session in step with the notifications: red
# while one needs you, then the green check, gone after DesktopLingerMinutes
function Sync-DesktopSession([bool]$fresh) {
    $now = Get-Date
    $exists = $script:sessions.ContainsKey($DesktopId)
    if (-not $exists -and -not $fresh) { return }
    $s = Get-Session $DesktopId
    if (-not $exists) { $s.project = 'Claude Desktop'; $s.root = ''; $s.first = $now }
    $want = if ($script:desktopWait.Count -gt 0) { 'waiting_user' } else { 'idle' }
    if ($fresh -or $want -ne $s.state) {
        if ($want -eq 'idle') { $script:desktopIdleSince = $now }
        $s.state = $want
        $s.last = $now
        Sync-Device
    } elseif ($want -eq 'idle' -and ($now - $script:desktopIdleSince).TotalMinutes -ge $DesktopLingerMinutes) {
        $script:sessions.Remove($DesktopId)
        $script:desktopLast = $null
        Log 'desktop: tile removed'
        Sync-Device
    }
}

function Update-DesktopToasts {
    $now = Get-Date
    try {
        $toasts = [DesktopToasts]::Read($script:desktopDb)
    } catch {
        Set-DesktopNote "read failed: $($_.Exception.Message)"
        return
    }
    $present = @{}
    $lines = @()
    foreach ($t in $toasts) {
        $present[$t.Id] = $true
        if ($script:desktopSeen.ContainsKey($t.Id)) { continue }
        $script:desktopSeen[$t.Id] = $true
        $arrival = [DateTime]::FromFileTimeUtc($t.Arrival).ToLocalTime()
        $texts = Get-ToastTexts $t.Payload
        $title = $texts[0]
        $body = (@($texts | Select-Object -Skip 1)) -join ' '
        $needsYou = "$title $body" -match 'approv|permission|needs? your|waiting for you|input needed|requires your'
        $state = if ($needsYou) { 'waiting_user' } else { 'idle' }
        if ($needsYou) {
            $script:desktopWait[$t.Id] = $arrival
            $script:desktopFocusSince = [datetime]::MinValue
        }
        $script:desktopLast = @{ time = $arrival.ToString('s'); title = $title; state = $state }
        Log "desktop: '$title' -> $state"
        $line = Format-DesktopEvent $title $body $state $arrival
        Add-Event 'Notification/desktop' $line
        $lines += $line
    }
    foreach ($id in @($script:desktopSeen.Keys)) { if (-not $present.ContainsKey($id)) { $script:desktopSeen.Remove($id) } }

    # The red sign goes when its notification leaves the Action Center
    # (clicked, dismissed), after DesktopWaitMinutes, or once the app's
    # window has been in front for three seconds
    foreach ($id in @($script:desktopWait.Keys)) {
        if (-not $present.ContainsKey($id) -or ($now - $script:desktopWait[$id]).TotalMinutes -ge $DesktopWaitMinutes) {
            $script:desktopWait.Remove($id)
        }
    }
    if ($script:desktopWait.Count -gt 0) {
        if ([DesktopToasts]::ForegroundImage() -match '\\WindowsApps\\Claude_|\\AnthropicClaude\\') {
            if ($script:desktopFocusSince -eq [datetime]::MinValue) { $script:desktopFocusSince = $now }
            elseif (($now - $script:desktopFocusSince).TotalSeconds -ge 3) {
                $script:desktopWait.Clear()
                Log 'desktop: the app has been in front: taken as seen'
            }
        } else {
            $script:desktopFocusSince = [datetime]::MinValue
        }
    }

    Sync-DesktopSession ($lines.Count -gt 0)
    foreach ($l in $lines) { Send-Event $l }   # after the session line: the tile exists
}

# ---------------- http ----------------

function Send-Response($ctx, [int]$code, [string]$body, [string]$type = 'application/json') {
    try {
        $ctx.Response.StatusCode  = $code
        $ctx.Response.ContentType = $type
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
            $rootHeader = [string]$req.Headers['X-Claude-Project']
            # The client process, looked up only while the session has none (or a dead one)
            $clientPid = 0
            $sid = [string]$e.session_id
            if ($sid) {
                $known = if ($script:sessions.ContainsKey($sid)) { $script:sessions[$sid] } else { $null }
                if (-not $known -or $known.pid -le 0 -or -not (Test-ProcessAlive $known.pid)) { $clientPid = Resolve-ClientPid $req }
            }
            Process-Hook $e $rootHeader $clientPid
            Send-Response $ctx 200 '{}'
        } catch {
            Log "hook: bad payload: $($_.Exception.Message)"
            Send-Response $ctx 400 '{"error":"bad json"}'
        }
        return
    }
    if ($path -eq '/statusline') {
        # The status line command POSTs its input here and prints the answer
        $text = ''
        try {
            $j = $body | ConvertFrom-Json
            if ($j.rate_limits -and (Set-UsageFromStatusLine $j.rate_limits)) { Send-Usage }
            $text = Format-StatusLineText $j
        } catch {
            Log "statusline: bad payload: $($_.Exception.Message)"
        }
        Send-Response $ctx 200 $text 'text/plain; charset=utf-8'
        return
    }
    if ($path -eq '/usage') {
        Send-Response $ctx 200 ((Get-UsageObject) | ConvertTo-Json -Depth 4)
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
    if ($path -eq '/events') {
        $n = 20
        if ($req.QueryString['n']) { $n = [int]$req.QueryString['n'] }
        $take = [Math]::Min($n, $script:events.Count)
        $items = @(if ($take -gt 0) { $script:events.GetRange($script:events.Count - $take, $take) })
        [array]::Reverse($items)
        $obj = [ordered]@{ count = $script:events.Count; sent_to_device = $script:eventsSent; events = $items }
        Send-Response $ctx 200 ($obj | ConvertTo-Json -Depth 4)
        return
    }
    if ($path -eq '/version') {
        $reply = Read-DeviceInfo
        $obj = [ordered]@{
            port   = $PortName
            reply  = $reply
            device = $script:deviceInfo
            daemon = [ordered]@{ script = $PSCommandPath; started = $script:started.ToString('s'); pwsh = $PSVersionTable.PSVersion.ToString() }
        }
        Send-Response $ctx 200 ($obj | ConvertTo-Json -Depth 4)
        return
    }
    if ($path -eq '/calibrate') {
        $q = $req.QueryString
        $fields = @()
        if ($q['reset']) { $fields = @('reset') }
        else {
            foreach ($k in 'rot', 'sign', 'offset') { if ($null -ne $q[$k] -and $q[$k] -ne '') { $fields += "$k=$($q[$k])" } }
        }
        if ($fields.Count -eq 0) { Send-Response $ctx 400 '{"error":"give rot, sign, offset or reset"}'; return }
        $c = "calibrate $($fields -join ' ')"
        $reply = Send-SerialCommand $c
        Log "calibrate -> $c (reply: $reply)"
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
            started          = $script:started.ToString('s')
            device_info      = $script:deviceInfo
            events_kept      = $script:events.Count
            events_sent      = $script:eventsSent
            event_log        = $(if ($LogEvents) { $EventLogPath } else { $null })
            device_state     = $script:deviceState
            device_subagents = $script:deviceSubagents
            device_sessions  = $script:deviceSessions
            effective        = (Get-EffectiveState)
            codes            = (Get-SessionCodes)
            last_ping        = $(if ($script:lastPing -gt [datetime]::MinValue) { $script:lastPing.ToString('s') } else { $null })
            process_watch    = $script:procSnap
            process_pids     = (Get-SessionPids)
            device_tool      = $script:deviceTool
            usage            = (Get-UsageObject)
            desktop          = [ordered]@{ watching = $script:desktopOn; note = $script:desktopNote; waiting = $script:desktopWait.Count; last = $script:desktopLast }
            sessions         = @($script:sessions.GetEnumerator() | ForEach-Object {
                [ordered]@{ id = $_.Key; state = $_.Value.state; subagents = $_.Value.subagents; last = $_.Value.last.ToString('s'); project = $_.Value.project; root = $_.Value.root; pid = $_.Value.pid; tools = @($_.Value.tools.GetEnumerator() | ForEach-Object { "$($_.Value) ($($_.Key))" }) }
            })
            projects         = $ProjectsPath
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

# Started by the scheduled task as `conhost --headless pwsh ...`: stopping the
# task ends that conhost only, so follow it out instead of running on as an
# orphan that keeps the port
$script:launcher = $null
try {
    $parent = (Get-Process -Id $PID).Parent
    if ($parent -and $parent.ProcessName -eq 'conhost') { $script:launcher = $parent }
} catch {}

Update-ProjectNames
Initialize-ProcessSnapshots
Initialize-DesktopWatch
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

        if ($script:launcher -and $script:launcher.HasExited) {
            Log "launcher conhost (pid $($script:launcher.Id)) has exited: stopping"
            break
        }

        $now = Get-Date
        if ($script:procSnap -and $now -ge $script:procNextAt) {
            # Twice a second while an approval may be pending or a tool was just
            # called (its process is about to appear), every two seconds otherwise
            $busy = @($script:sessions.Values | Where-Object {
                $_.pid -gt 0 -and ($_.state -eq 'waiting_user' -or ($now - $_.last).TotalSeconds -lt 5) }).Count -gt 0
            $script:procNextAt = $now.AddMilliseconds($(if ($busy) { 500 } else { 2000 }))
            if (Update-ProcessTree) {
                Expire-Sessions
                Sync-Device
            }
        }
        if (-not $script:serial -and $now -ge $script:releaseUntil -and $now -ge $script:nextOpenTry) {
            if (Open-Serial) {
                # The device may have rebooted: push everything again
                $script:deviceState = $null
                $script:deviceSubagents = 0
                $script:deviceSessions = $null
                $script:deviceTool = $false
                $script:deviceSessionLines = @{}
                $script:deviceUsageKey = $null
                $script:lastInfoTry = $now
                Read-DeviceInfo | Out-Null
                Sync-Device $true
                if (Send-Serial 'ping') { $script:lastPing = $now }
            } else {
                $script:nextOpenTry = $now.AddSeconds(2)
            }
        }
        if ($script:serial -and ($now - $script:lastPing).TotalSeconds -ge $PingSeconds) {
            if (Send-Serial 'ping') { $script:lastPing = $now }
        }
        # A device that was still booting when the port opened (the Tab5
        # takes ~2.5 s to bring its panel up) answered nothing: ask again
        if ($script:serial -and -not $script:deviceInfo -and ($now - $script:lastInfoTry).TotalSeconds -ge 10) {
            $script:lastInfoTry = $now
            Read-DeviceInfo | Out-Null
        }
        foreach ($line in (Read-SerialLines)) {
            if ($line -match 'STATUS|VERSION|ERROR') { Log "device: $($line.Trim())" }
        }
        # Plan usage: the account poll (in flight without blocking the loop),
        # and the device's copy kept current (a device that announced its
        # features late, a window that reset)
        if ($AccountUsage -and -not $script:usageTask -and $now -ge $script:usageNextPoll) { Start-UsagePoll }
        if ($script:usageTask -and $script:usageTask.IsCompleted) { Complete-UsagePoll }
        if ($now -ge $script:usageCheckAt) {
            $script:usageCheckAt = $now.AddSeconds(5)
            Send-Usage
        }
        # The desktop app's notifications, every three seconds
        if ($script:desktopOn -and $now -ge $script:desktopNextAt) {
            $script:desktopNextAt = $now.AddSeconds(3)
            Update-DesktopToasts
        }
        if (($now - $lastHousekeeping).TotalSeconds -ge 60) {
            $before = $script:sessions.Count
            Expire-Sessions
            if ($script:sessions.Count -ne $before) { Sync-Device }
            Update-ProjectNames
            $lastHousekeeping = $now
        }
    }
} finally {
    Close-Serial "shutdown"
    try { $listener.Stop() } catch {}
    Log "daemon stopped"
}
